/*! @file cobebooks.c

*  @brief Variable Length Coding tools
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

#ifndef _CODEBOOKS
#define _CODEBOOKS		1
#endif

#include <assert.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif
#include "codebooks.h"
#include "codec.h"
#include "vlc.h"
#include "allocator.h"

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif

#define TIMING (1 && _TIMING)

#if _CODEBOOKS

#include "table9.inc"
#include "fsm9.inc"
//#include "fsm9x.inc"

static RMCBOOK rmctable9 = { (RLCBOOK *)&table9z, (VLCBOOK *)&table9m };
//static RMCBOOK rmctable9computed = { NULL, NULL };

CODESET cs9 = {
	"Codebook set 9 from data by David with tables automatically generated for FSM decoder",
	(VLCBOOK *)&table9m,
	(RLCBOOK *)&table9z,
	(unsigned int *)&table9s,
	&rmctable9,
	//&rmctable9computed,
	 NULL,NULL,
	(RLVBOOK *)&table9r,
	NULL,
	NULL,
	(FSMTABLE *)&fsm9,
	(FSMARRAY *)&fsm9_init,
	0
};


#if CODEC_NUM_CODESETS >= 2

#include "table17.inc"
#include "fsm17.inc"

static RMCBOOK rmctable17 = { (RLCBOOK *)&table17z, (VLCBOOK *)&table17m };
//static RMCBOOK rmctable17computed = { NULL, NULL };

CODESET cs17 = {
	"Codebook set 17 from data by David with tables automatically generated for FSM decoder",
	(VLCBOOK *)&table17m,
	(RLCBOOK *)&table17z,
	(unsigned int *)&table17s,
	&rmctable17,
	//&rmctable17computed,
	NULL, NULL,
	(RLVBOOK *)&table17r,
	NULL,
	NULL,
	(FSMTABLE *)&fsm17,
	(FSMARRAY *)&fsm17_init,
//	FSMTABLE_FLAGS_COMPANDING_NOT_NEEDED
	FSMTABLE_FLAGS_COMPANDING_CUBIC
};


#endif //CODEC_NUM_CODESETS >= 2


#if CODEC_NUM_CODESETS >= 3

#include "table18.inc" // 17 used in linear form
#include "fsm18.inc"

static RMCBOOK rmctable18 = { (RLCBOOK *)&table18z, (VLCBOOK *)&table18m };
//static RMCBOOK rmctable18computed = { NULL, NULL };

CODESET cs18 = {
	"Codebook set 18 from data by David with tables automatically generated for FSM decoder",
	(VLCBOOK *)&table18m,
	(RLCBOOK *)&table18z,
	(unsigned int *)&table18s,
	&rmctable18,
	//&rmctable18computed,
	NULL, NULL,
	(RLVBOOK *)&table18r,
	NULL,
	NULL,
	(FSMTABLE *)&fsm18,
	(FSMARRAY *)&fsm18_init,
	FSMTABLE_FLAGS_COMPANDING_NOT_NEEDED
//	FSMTABLE_FLAGS_COMPANDING_CUBIC
};


#endif //CODEC_NUM_CODESETS == 3

// Certain markers must use codebook bit patterns that are reserved for markers so
// that the marker is not confused for the encoded run lengths in the bitstream.
// Must change the following codebook of markers when the codeset is changed.

// Replacement for the run length codebook
#ifndef NEW_CODEBOOK_LENGTH
#define NEW_CODEBOOK_LENGTH 3072
//#define NEW_CODEBOOK_LENGTH 1024
//#define NEW_CODEBOOK_LENGTH 512
#endif

//DAN20150731
typedef struct newcodestruct {
	int length;
	RLC entries[NEW_CODEBOOK_LENGTH];
} newcodes;

/* DAN20150731
static struct {
	int length;
	RLC entries[NEW_CODEBOOK_LENGTH];
} newcodes[CODEC_NUM_CODESETS];//DAN20041024 = {NEW_CODEBOOK_LENGTH};
*/

// Lookup table for faster decoding
#define LOOKUP_TABLE_SIZE 12
/* DAN20150731
static struct {
	int size;
	int length;
	FLC entries[1 << LOOKUP_TABLE_SIZE];
} fastbook[CODEC_NUM_CODESETS];//DAN20041024 = {LOOKUP_TABLE_SIZE, (1 << LOOKUP_TABLE_SIZE)};
*/

//DAN20150731
typedef struct fastbookstruct {
	int size;
	int length;
	FLC entries[1 << LOOKUP_TABLE_SIZE];
} fastbook;


// Indexable table for signed values
/* DAN20150731
static struct {
	int size;
	int length;
	VLC entries[VALUE_TABLE_LENGTH];
} valuebook[CODEC_NUM_CODESETS];//DAN20041024 = {VALUE_TABLE_SIZE, VALUE_TABLE_LENGTH};
*/
//DAN20150731
typedef struct valuebookstruct {
	int size;
	int length;
	VLC entries[VALUE_TABLE_LENGTH];
} valuebook;


#endif

// Forward reference
void DumpFSM(DECODER *decoder);

// Compute the number of leading zeros
static int NumLeadingZeros(int number, int size)
{
	int num_zeros;

	if (number < 0) return 0;

	for (num_zeros = size; number > 0; num_zeros--) {
		if (num_zeros == 0) break;
		number = (number >> 1);
	}

	return num_zeros;
}

// Initialize the codebooks in a codeset
#if _ALLOCATOR
bool InitCodebooks(ALLOCATOR *allocator, CODESET *cs)
#else
bool InitCodebooks(CODESET *cs)
#endif
{
	int i=0;
	
	for(i=0; i<CODEC_NUM_CODESETS; i++)
	{		
		// Has the sparse runs codebook been replaced by the indexable codebook?
		if (cs[i].codebook_magbook == NULL || cs[i].codebook_runbook == NULL)
		{
#if _ALLOCATOR
			newcodes *newcodes = (struct newcodestruct *)Alloc(allocator, sizeof(struct newcodestruct));
			fastbook *fastbook =  (struct fastbookstruct *)Alloc(allocator, sizeof(struct fastbookstruct));
			valuebook *valuebook = (struct valuebookstruct *)Alloc(allocator, sizeof(struct valuebookstruct));
#else
			newcodes *newcodes = (struct newcodestruct *)MEMORY_ALLOC(sizeof(struct newcodestruct));
			fastbook *fastbook =  (struct fastbookstruct *)MEMORY_ALLOC(sizeof(struct fastbookstruct));
			valuebook *valuebook = (struct valuebookstruct *)MEMORY_ALLOC(sizeof(struct valuebookstruct));
#endif

			if (newcodes == NULL || fastbook == NULL || valuebook == NULL)
				return false;
			{
				// Initialize the indexable table of run length codes
				RLE *old_codes = (RLE *)(((char *)cs[i].src_codebook->runbook) + sizeof(RLCBOOK));
				RLC *new_codes = &newcodes->entries[0];
				int old_length = cs[i].src_codebook->runbook->length;
				int new_length = newcodes->length = NEW_CODEBOOK_LENGTH; //DAN20041024

				// Get the codebook entry for an isolated zero
				VLC *code = (VLC *)(((char *)cs[i].magsbook) + sizeof(VLCBOOK));
				uint32_t  zero = code->bits;
				int size = code->size;

				// Create a more efficient codebook for encoding runs of zeros
#if _ALLOCATOR
				ComputeRunLengthCodeTable(allocator,
					old_codes, old_length,
					new_codes, new_length,
					zero, size);
#else
				ComputeRunLengthCodeTable(old_codes, old_length, new_codes, new_length, zero, size);
#endif
				// Replace the old codebook in the codeset
				cs[i].codebook_magbook = (VLCBOOK *)cs[i].src_codebook->magbook;
				cs[i].codebook_runbook = (RLCBOOK *)newcodes;
			}

	
			memset((void *)fastbook, 0, sizeof(fastbook)); //DAN20150818
			{
				int length = cs[i].runsbook->length;
				RLV *codebook = (RLV *)(((char *)cs[i].runsbook) + sizeof(RLVBOOK));
				//fastbook *fastbook = (struct fastbookstruct *)cs[i]->fastbook;

				//DAN20150731  fastbook[i].size = LOOKUP_TABLE_SIZE;//DAN20041024
				//DAN20150731  fastbook[i].length = (1 << LOOKUP_TABLE_SIZE);//DAN20041024

				fastbook->size = LOOKUP_TABLE_SIZE;
				fastbook->length = (1 << LOOKUP_TABLE_SIZE);

				// Create the fast lookup table from the codebook
#if _OLD_FAST_LOOKUP
				// Use the old fast lookup table algorithms
				FillCodeLookupTable(codebook, length, fastbook->entries, LOOKUP_TABLE_SIZE);
#else
				// Use the new fast lookup table algorithms
				FillScanLookupTable(codebook, length, fastbook->entries, LOOKUP_TABLE_SIZE);
#endif
			}

			{
				//valuebook *valuebook = (struct valuebookstruct *)cs[i]->valuebook;
#if USE_UNPACKED_VLC
				VLCBOOK *codebook = cs[i]->magsbook;
				VLC *table = (VLC *)(((char *)valuebook) + sizeof(VALBOOK));
				int size = valuebook->size = VALUE_TABLE_SIZE;
				valuebook->length = VALUE_TABLE_LENGTH;

				FillVlcTable(codebook, table, size, cs[i]->flags);
#else
				VLCBOOK *codebook = cs[i].magsbook;
				VLE *table = (VLE *)(((char *)valuebook) + sizeof(VALBOOK));
				int size = valuebook->size = VALUE_TABLE_SIZE;
				valuebook->length = VALUE_TABLE_LENGTH;

				FillVleTable(codebook, table, size, cs[i].flags);
#endif
			}

			cs[i].fastbook = (FLCBOOK *)fastbook;
			cs[i].valuebook = (VALBOOK *)valuebook;
		}
	}
	// The codebooks have been initialized successfully
	return true;
}

// Initialize the codebooks in a codeset
bool InitDecoderFSM(DECODER *decoder, CODESET *cs)
{
	// Initialize the finite state machine tables for this codeset
	// Note that the finite state machine is reset by the decoder
	int i;

	for (i = 0; i<CODEC_NUM_CODESETS; i++)
	{
		if (decoder)
		{
			if (decoder->fsm[i].table.flags >= 0)
			{
				// Should be zero states if the table has not been initialized
				assert(decoder->fsm[i].table.num_states == 0);

	#if _INDIVIDUAL_LUT
				if (!FillFSM(decoder, &decoder->fsm[i].table, cs[i].fsm_array)) {
					//decoder->error = CODEC_ERROR_INIT_FSM;
					// The subroutine has already set the error code
					return false;
				}
				//cs->fsm->next_state = cs->fsm->entries[0];
	#else
				if (!FillFSM(decoder, &decoder->fsm[i].table, cs[i].fsm_array)) {
					//decoder->error = CODEC_ERROR_INIT_FSM;
					// The subroutine has already set the error code
					return false;
				}
				//cs->fsm->next_state = cs->fsm->entries;
	#endif

				decoder->fsm[i].table.flags |= cs[i].flags;

	#if _COMPANDING
				ScaleFSM(&decoder->fsm[i].table);
	#endif
				// Indicate that the table was initialized
				decoder->fsm[i].table.flags |= FSMTABLE_FLAGS_INITIALIZED;
			}

			//
			{
				int pos = cs[i].tagsbook[0]-1;// The last code in the tagsbook in the band_end_code
				decoder->band_end_code[i] = (unsigned int)cs[i].tagsbook[pos*2+2];
				decoder->band_end_size[i] = (int)cs[i].tagsbook[pos*2+1];
			}

			// Check that the finite state machine table was initialized
			assert(decoder->fsm[i].table.num_states > 0);
		}
		}
#if 0
	if (decoder) {
		DumpFSM(decoder);
	}
#endif

	return true;
}


// Free all data structures allocated for the codebooks
void FreeCodebooks(DECODER *decoder /*, CODESET *cs */)
{
	int i;

	for (i = 0; i < CODEC_NUM_CODESETS; i++)
	{
		FSMTABLE *fsm_table = &decoder->fsm[i].table;
		//int num_states = fsm_table->num_states;
		//int j;

		// Check the number of finite state machine states
		assert(0 < fsm_table->num_states && fsm_table->num_states <= FSM_NUM_STATES_MAX);

#if _FSM_NO_POINTERS==0
		// Free the finite state machine tables
		for (j = 0; j < num_states; j++)
		{
			if (fsm_table->entries[j] != NULL)
			{

#if _ALLOCATOR
				Free(decoder->allocator, fsm_table->entries[j]);
#else
				MEMORY_ALIGNED_FREE(fsm_table->entries[j]);
#endif
				fsm_table->entries[j] = NULL;
			}
		}
#endif

		// Indicate that the finite state machine has not been initialized
		fsm_table->flags = 0;
	}
}

#if _ALLOCATOR
void ComputeRunLengthCodeTable(ALLOCATOR *allocator,
							   RLE *input_codes, int input_length,
							   RLC *output_codes, int output_length,
							   uint32_t  zero_code, int zero_size)
#else
void ComputeRunLengthCodeTable(RLE *input_codes, int input_length,
							   RLC *output_codes, int output_length,
							   uint32_t  zero_code, int zero_size)
#endif
{
	// Need enough space for the codebook and the code for a single value
	int length = input_length + 1;
	size_t size = length * sizeof(RLC);
#if _ALLOCATOR
	RLC *codebook = (RLC *)Alloc(allocator, size);
#else
	RLC *codebook = (RLC *)MEMORY_ALLOC(size);
#endif
	bool onerun = false;
	int i;
	
	// Copy the codes into the temporary codebook for sorting
	length = input_length;
	for (i = 0; i < length; i++) {
		int count = input_codes[i].count;
		if (count == 1) onerun = true;

		codebook[i].size = input_codes[i].size;
		codebook[i].bits = input_codes[i].bits;
		codebook[i].count = count;

		// Check the codebook entry
		assert(codebook[i].size > 0);
		assert(codebook[i].count > 0);
	}

	// Need to add a code for a single run?
	if (!onerun) {
		codebook[length].size = zero_size;
		codebook[length].bits = zero_code;
		codebook[length].count = 1;
		length++;
	}

	// Sort the codewords into decreasing run length
	SortDecreasingRunLength(codebook, length);

	// The last code must be for a single run
	assert(codebook[length - 1].count == 1);

	// Fill the lookup table with codes for runs indexed by the run length
	FillRunLengthCodeTable(codebook, length, output_codes, output_length);

	// Free the space used for the sorted codewords
#if _ALLOCATOR
	Free(allocator, codebook);
#else
	MEMORY_FREE(codebook);
#endif
}

// Sort codebook into decreasing length of the run
void SortDecreasingRunLength(RLC *codebook, int length)
{
	int i;
	int j;

	// Perform a simple bubble sort since the codebook may already be sorted
	for (i = 0; i < length; i++)
	{
		for (j = i+1; j < length; j++)
		{
			// Should not have more than one codebook entry with the same run length
			assert(codebook[i].count != codebook[j].count);

			// Exchange codebook entries if the current entry is smaller
			if (codebook[i].count < codebook[j].count)
			{
				int size = codebook[i].size;
				uint32_t  bits = codebook[i].bits;
				int count = codebook[i].count;

				codebook[i].size = codebook[j].size;
				codebook[i].bits = codebook[j].bits;
				codebook[i].count = codebook[j].count;

				codebook[j].size = size;
				codebook[j].bits = bits;
				codebook[j].count = count;
			}
		}

		// After two iterations that last two items should be in the proper order
		assert(i == 0 || codebook[i-1].count > codebook[i].count);
	}
}

// Use a sparse run length code table to create an indexable table for faster encoding
void FillRunLengthCodeTable(RLC *codebook, int codebook_length, RLC *table, int table_length)
{
	int i;		// Index into the lookup table
	int j;		// Index into the codebook

	// Use all of the bits except the sign bit for the codewords
	int max_code_size = BITSTREAM_LONG_SIZE - 1;

	// Check that the input codes are sorted into decreasing run length
	for (j = 1; j < codebook_length; j++) {
		RLC *previous = &codebook[j-1];
		RLC *current = &codebook[j];

		assert(previous->count > current->count);
		if (!(previous->count > current->count)) return;
	}

	// The last input code should be the code for a single zero
	assert(codebook[codebook_length - 1].count == 1);

	// Create the shortest codeword for each table entry
	for (i = 0; i < table_length; i++)
	{
		int length = i;			// Length of the run for this table entry
		uint32_t  codeword = 0;	// Composite codeword for this run length
		int codesize = 0;		// Number of bits in the composite codeword
		int remaining;			// Remaining run length not covered by the codeword

		remaining = length;

		for (j = 0; j < codebook_length; j++)
		{
			int repetition;		// Number of times the codeword is used
			int k;

			// Nothing to do if the remaining run length is zero
			if (remaining == 0) break;

			// The number of times that the codeword is used is the number
			// of times that it divides evenly into the remaining run length
			repetition = remaining / codebook[j].count;

			// Append the codes to the end of the composite codeword
			for (k = 0; k < repetition; k++)
			{
				// Terminate the loop if the codeword will not fit
				if (codebook[j].size > (max_code_size - codesize))
				{
					if(codesize)
					// 2/12/02 - DAN - code change so that longer runs aren't filled with single zeros to fill the table bits - slightly inefficent
					{
						remaining -= (k * codebook[j].count);
						goto next;
					}
					else
					{
						break;
					}
				}

				// Shift the codeword to make room for the appended codes
				codeword <<= codebook[j].size;

				// Insert the codeword from the codebook at the end of the composite codeword
				codeword |= codebook[j].bits;

				// Increment the number of bits in the composite codeword
				codesize += codebook[j].size;
			}

			// Reduce the run length by the amount that was consumed by the repeated codeword
			remaining -= (k * codebook[j].count);
		}

next:
		// Should have covered the entire run if the last codeword would fit
		//assert(remaining == 0 || (max_code_size - codesize) < codebook[codebook_length - 1].size);

		// Store the composite run length in the lookup table
		table[i].bits = codeword;
		table[i].size = codesize;
		table[i].count = length - remaining;
	}
}

// Compute a fast lookup table for decoding the bitstream
void FillCodeLookupTable(RLV *codebook, int length, FLC *table, int size)
{
	// Compute the number of entries in the lookup table
	int num_entries = 1 << size;
	int i, j;
	
	// Initialize the table with the null entry
	for (i = 0; i < num_entries; i++) {
		table[i].count = 0;
		table[i].shift = 0;
		table[i].value = 0;
	}

	// Fill each table entry using the codeword for that index
	for (i = 0; i < num_entries; i++)
	{
		// Find the shortest codeword for each table index
		for (j = 0; j < length; j++)
		{
			uint32_t  codeword = codebook[j].bits;
			int codesize = codebook[j].size;
			uint32_t  prefix;
			int shift;

			// Skip this entry if the codeword length exceeds the table size
			if (codesize > size) continue;

			// Get the leading bits corresponding to the current index
			shift = size - codesize;
			assert(shift >= 0);
			if (shift > 0) prefix = (i >> shift);
			else prefix = i;

			// Does the prefix match the codeword?
			if (prefix == codeword)
			{
				// Does this table index correspond to a run of zeros?
				if (codeword == 0 &&
					codesize == 1 &&
					codebook[j].count == 1 &&
					codebook[j].value == 0)
				{
					int num_zeros = NumLeadingZeros(i, size);
					table[i].count = num_zeros;
					table[i].value = 0;
					table[i].shift = num_zeros;
				}
				else
				{
					// Fill the lookup table entry using the codebook entry
					table[i].count = codebook[j].count;
					table[i].value = codebook[j].value;
					table[i].shift = codesize;
				}

				// Finished searching the codebook
				break;
			}
		}

		// Advance to the next entry in the lookup table
	}
	
	// Fill the unused entries with information for faster decoding
	for (i = 0; i < num_entries; i++) {
		if (table[i].count == 0)
		{
			// Table entry should be unfilled before the following loop
			assert(table[i].shift == 0 || table[i].shift == size);

			// Find the first code that has this code as a prefix
			for (j = 0; j < length; j++)
			{
				uint32_t  codeword = codebook[j].bits;
				int codesize = codebook[j].size;
				uint32_t  prefix;
				int unseen;			// Codeword bits after the look ahead bits
				int index = i;		// Codeword bits in the lookup table index

				// Look for the codeword that was too int32_t for the lookup table
				if (codesize <= size) continue;

				// Get the leading bits corresponding to the current index
				unseen = codesize - size;
				assert(unseen > 0);
				prefix = (codeword >> unseen);

				// First codeword prefix equal to the index?
				if (prefix == (uint32_t)index) {
					table[i].value = j;			// Save the index to this codeword
					table[i].shift = size;		// Remember the length of the prefix
					break;
				}
			}

			// Should have exited loop after filling an unused entry
			assert(table[i].shift == size);
		}
	}
}

// Scan a bit string right justified in the word for a match in the codebook
int MatchBitPattern(uint32_t  word, int width, RLV *codebook, int length, FLC *match)
{
	uint32_t  bits;		// Bits that have been scanned
	int size = 0;		// Number of bits scanned
	int i = 0;			// Index into the codebook

	// A null bit string always fails to match the codebook
	if (width == 0) goto failure;

	// Remove any excess bits on the left
	word &= BITMASK(width);

	// Search the codebook for a match to the leading bits in the pattern
	while (i < length) {
		int codesize = codebook[i].size;

		// Not enough bits in the word?
		if (codesize > width) goto failure;

		// Need to get more bits from the word?
		if (size < codesize) {
			bits = word >> (width - codesize);
			size = codesize;
		}

		// Examine the run length table entries that have the same bit field length
		for (; (i < length) && (size == codebook[i].size); i++) {
			if (bits == codebook[i].bits)
			{
				int value = codebook[i].value;

				if (value != 0) {
					int sign;

					// Something is wrong if the value is already negative
					assert(value > 0);

					// Check that the sign codes have the same length
					assert(VLC_POSITIVE_SIZE == VLC_NEGATIVE_SIZE);

					// Compute the size of the matching bit string including the sign
					size += VLC_NEGATIVE_SIZE;

					// Enough bits left to determine the sign?
					if (size > width) goto failure;

					// Get the sign
					sign = (word >> (width - size)) & BITMASK(VLC_NEGATIVE_SIZE);

					// Change the sign if the sign bit was negative
					if (sign == VLC_NEGATIVE_CODE)
						value = (-value);
				}

				match->count = codebook[i].count;
				match->value = value;
				match->shift = size;

				goto success;
			}
		}
	}

failure:

	// Did not find a matching code in the codebook
	match->count = 0;
	match->value = 0;
	match->shift = 0;

	return VLC_ERROR_NOTFOUND;

success:

	// Found a matching code and already set the output values
	return VLC_ERROR_OKAY;
}

// Compute a fast lookup table for finding signed values in the bitstream
void FillScanLookupTable(RLV *codebook, int length, FLC *table, int size)
{
	// Compute the number of entries in the lookup table
	int num_entries = 1 << size;
	int i, j;
	
	// Initialize the table with the null entry
	for (i = 0; i < num_entries; i++) {
		table[i].count = 0;
		table[i].shift = 0;
		table[i].value = 0;
	}

	// Fill each table entry using the codeword for that index
	for (i = 0; i < num_entries; i++)
	{
		uint32_t  codeword = i;		// Bit pattern corresponding to this table index
		int codesize = size;		// Size of the bit pattern
		int count = 0;				// Number of leading zeros plus the signed value
		int shift = 0;				// Number of bits including the signed value
		int value = 0;				// First signed value in the bit pattern

		while (value == 0 && codesize > 0)
		{
			FLC match;		// Information on the matching codeword

			// Test the bit pattern for a match in the codebook
			int result = MatchBitPattern(codeword, codesize, codebook, length, &match);

			// Failed to find a match in the codebook?
			if (result != VLC_ERROR_OKAY) break;

			// Cannot handle runs of nonzero values
			assert(match.count == 1 || match.value == 0);

			// Reduce the codeword size by the number of bits scanned
			codesize -= match.shift;

			// Increase the count of the number of bits until a value is found
			shift += match.shift;

			// Increase the count (run length) of values matched in the bit pattern
			count += match.count;

			// Remember the value that was matched
			value = match.value;
		}

		// Found a value in the bit pattern?
		if (shift > 0) {
			table[i].count = count;
			table[i].value = value;
			table[i].shift = shift;
		}

		// Advance to the next entry in the lookup table
	}

	// Fill the unused entries with information for faster decoding
	for (i = 0; i < num_entries; i++) {
		if (table[i].count == 0)
		{
			// Table entry should be unfilled before the following loop
			assert(table[i].shift == 0);

			// Find the first code that has this code as a prefix
			for (j = 0; j < length; j++)
			{
				uint32_t  prefix;
				int unseen;			// Codeword bits after the look ahead bits
				int index = i;		// Codeword bits in the lookup table index

				// Look for the codeword that was too int32_t for the lookup table
				// but in the new algorithm must include the sign that follows
				//if (codesize <= size) continue;

				// Is this codeword for the magnitude of a signed quantity?
				if (codebook[j].value == 0)
				{
					uint32_t  codeword = codebook[j].bits;
					int codesize = codebook[j].size;

					// The codeword for a run of zeros should be too int32_t for the lookup table
					if (codesize <= size) continue;

					// Get the leading bits corresponding to the current index
					unseen = codesize - size;
					assert(unseen > 0);
					prefix = (codeword >> unseen);

					// First codeword prefix equal to the index?
					if (prefix == (uint32_t)index) {
						table[i].value = j;			// Save the index to this codeword
						table[i].shift = size;		// Remember the length of the prefix
						break;
					}
				}
				else
				{
					// Try the codeword with a positive sign appended
					if ((codebook[j].size + VLC_POSITIVE_SIZE) > size)
					{
						uint32_t  codeword = (codebook[j].bits << VLC_POSITIVE_SIZE) | VLC_POSITIVE_CODE;
						int codesize = codebook[j].size + VLC_POSITIVE_SIZE;

						// The codeword for the signed value should be too int32_t for the lookup table
						if (codesize <= size) continue;

						// Get the leading bits corresponding to the current index
						unseen = codesize - size;
						assert(unseen > 0);
						prefix = (codeword >> unseen);

						// First codeword prefix equal to the index?
						if (prefix == (uint32_t)index) {
							table[i].value = j;			// Save the index to this codeword
							table[i].shift = size;		// Remember the length of the prefix
							break;
						}
					}

					// Try the codeword with a negative sign appended
					if ((codebook[j].size + VLC_NEGATIVE_SIZE) > size)
					{
						uint32_t  codeword = (codebook[j].bits << VLC_NEGATIVE_SIZE) | VLC_NEGATIVE_CODE;
						int codesize = codebook[j].size + VLC_NEGATIVE_SIZE;

						// The codeword for the signed value should be too int32_t for the lookup table
						if (codesize <= size) continue;

						// Get the leading bits corresponding to the current index
						unseen = codesize - size;
						assert(unseen > 0);
						prefix = (codeword >> unseen);

						// First codeword prefix equal to the index?
						if (prefix == (uint32_t)index) {
							table[i].value = j;			// Save the index to this codeword
							table[i].shift = size;		// Remember the length of the prefix
							break;
						}
					}
				}
			}

			// Should have exited loop after filling an unused entry
			assert(table[i].shift == size);
		}
	}
}

// Fill lookup table indexed by a signed value that is used as an unsigned index
void FillVlcTable(VLCBOOK *codebook, VLC *table, int size, int flags)
{
	// Get the length of the codebook and a pointer to the entries
	int codebook_length = codebook->length;
	int32_t maximum_codebook_value = codebook_length - 1;
	VLC *code = (VLC *)((char *)codebook + sizeof(VLCBOOK));

	// Convert the index size (in bits) to the number of entries in the lookup table
	int table_length = (size > 0) ? (1 << size) : 0;
	int sign_mask = (1 << (size - 1));
	int twos_complement = -sign_mask;
	int magnitude_mask = sign_mask - 1;
	int index;
	int lastmag = 0;
	int cubictable[1025];

	if(flags & FSMTABLE_FLAGS_COMPANDING_CUBIC)
	{
		for(index = 0; index<1025; index++)
			cubictable[index] = 0;

		for(index = 1; index<256; index++)
		{
			double cubic = index;
			int mag = 0;
			//int i;

			mag = index;
			cubic *= index;
			cubic *= index;
			cubic *= 768;
			cubic /= 256*256*256;

			mag += (int)cubic;
			if(mag > 1023) mag = 1023;

			cubictable[mag] = index;
		}

		lastmag = 0;
		for(index = 0; index<1025; index++)
		{
			if(cubictable[index])
				lastmag = cubictable[index];
			else
				cubictable[index] = lastmag;
		}
	}


	// Fill each table entry with the codeword for that (signed) value
	for (index = 0; index < table_length; index++)
	{
		// Compute the signed value that corresponds to this index
		int value = (index & sign_mask) ? (twos_complement + (index & magnitude_mask)) : index;
		int magnitude = abs(value);
		uint32_t  codeword;
		int codesize;

		if(flags & FSMTABLE_FLAGS_COMPANDING_CUBIC)
		{
			magnitude = cubictable[magnitude];
		}
		else if(flags & FSMTABLE_FLAGS_COMPANDING_NOT_NEEDED)
		{
		}
		else // old style
		{
#if _COMPANDING
			if (magnitude >= 40)
			{

				magnitude -= 40;
				magnitude += 2; // mid point rounding
				magnitude >>= 2;
				magnitude += 40;

 #if _COMPANDING_MORE
				if(magnitude >= _COMPANDING_MORE)
				{
					magnitude -= _COMPANDING_MORE;
					magnitude += 2; // mid point rounding
					magnitude >>= 2;
					magnitude += _COMPANDING_MORE;
				}
 #endif
			}
#endif
		}

		// Is the magnitude larger that the number of entries in the codebook?
		if (magnitude > maximum_codebook_value) {
			magnitude = maximum_codebook_value;
		}

		codeword = code[magnitude].bits;
		codesize = code[magnitude].size;

		// Add the code for the sign to the magnitude
		if (value > 0) {
			codeword = (codeword << VLC_POSITIVE_SIZE) | VLC_POSITIVE_CODE;
			codesize += VLC_POSITIVE_SIZE;
		}
		else if (value < 0) {
			codeword = (codeword << VLC_NEGATIVE_SIZE) | VLC_NEGATIVE_CODE;
			codesize += VLC_NEGATIVE_SIZE;
		}

		table[index].bits = codeword;
		table[index].size = codesize;
	}
}

// Fill lookup table indexed by a signed value that is used as an unsigned index
void FillVleTable(VLCBOOK *codebook, VLE *table, int size, int flags)
{
	// Get the length of the codebook and a pointer to the entries
	int codebook_length = codebook->length;
	int32_t maximum_codebook_value = codebook_length - 1;
	VLC *code = (VLC *)((char *)codebook + sizeof(VLCBOOK));

	// Convert the index size (in bits) to the number of entries in the lookup table
	int table_length = (size > 0) ? (1 << size) : 0;
	int sign_mask = (1 << (size - 1));
	int twos_complement = -sign_mask;
	int magnitude_mask = sign_mask - 1;
	int index;
	int lastmag;
	int cubictable[1025];

	if(flags & FSMTABLE_FLAGS_COMPANDING_CUBIC)
	{
		for(index = 0; index<1025; index++)
			cubictable[index] = 0;

		for(index = 1; index<256; index++)
		{
			double cubic = index;
			int mag = 0;
			//int i;

			mag = index;
			cubic *= index;
			cubic *= index;
			cubic *= 768;
			cubic /= 256*256*256;

			mag += (int)cubic;
			if(mag > 1023) mag = 1023;

			cubictable[mag] = index;
		}

		lastmag = 0;
		for(index = 0; index<1025; index++)
		{
			if(cubictable[index])
				lastmag = cubictable[index];
			else
				cubictable[index] = lastmag;
		}
	}

	// Fill each table entry with the codeword for that (signed) value
	for (index = 0; index < table_length; index++)
	{
		// Compute the signed value that corresponds to this index
		int value = (index & sign_mask) ? (twos_complement + (index & magnitude_mask)) : index;
		int magnitude = abs(value);
		uint32_t  codeword;
		int codesize;

		if(flags & FSMTABLE_FLAGS_COMPANDING_CUBIC)
		{
			magnitude = cubictable[magnitude];
		}
		else if(flags & FSMTABLE_FLAGS_COMPANDING_NOT_NEEDED)
		{
		}
		else // old style
		{
#if _COMPANDING
			if (magnitude >= 40)
			{

				magnitude -= 40;
				magnitude += 2; // mid point rounding
				magnitude >>= 2;
				magnitude += 40;

 #if _COMPANDING_MORE
				if(magnitude >= _COMPANDING_MORE)
				{
					magnitude -= _COMPANDING_MORE;
					magnitude += 2; // mid point rounding
					magnitude >>= 2;
					magnitude += _COMPANDING_MORE;
				}
 #endif
			}
#endif
		}

		// Is the magnitude larger that the number of entries in the codebook?
		if (magnitude > maximum_codebook_value) {
			magnitude = maximum_codebook_value;
		}

		codeword = code[magnitude].bits;
		codesize = code[magnitude].size;

		// Add the code for the sign to the magnitude
		if (value > 0) {
			codeword = (codeword << VLC_POSITIVE_SIZE) | VLC_POSITIVE_CODE;
			codesize += VLC_POSITIVE_SIZE;
		}
		else if (value < 0) {
			codeword = (codeword << VLC_NEGATIVE_SIZE) | VLC_NEGATIVE_CODE;
			codesize += VLC_NEGATIVE_SIZE;
		}

		assert((codesize & VLE_CODESIZE_MASK) == codesize);
		assert((codeword & VLE_CODEWORD_MASK) == codeword);
		table[index].entry = (codesize << VLE_CODESIZE_SHIFT) | (codeword & VLE_CODEWORD_MASK);
	}
}



// TODO: Need to change FillFSM to return an error code if
// the finite state machine tables could not be initialized


#if _INDIVIDUAL_LUT

#if !_INDIVIDUAL_ENTRY
// Fill the finite state machine with lookup tables generated by Huffman program
bool FillFSM(DECODER *decoder, FSMTABLE *fsm_table, const FSMARRAY *fsm_array)
{
	size_t lut_size;		// Size of each lookup table (in bytes)
	int i,j;

	// Must have an array of data for the finite state machine
	assert(fsm_array != NULL);
	if (! (fsm_array != NULL)) {
		decoder->error = CODEC_ERROR_INIT_FSM;
		return false;
	}

	// Check that all entries will fit in the table
	assert(fsm_array->num_states <= FSM_NUM_STATES_MAX);
	if (! (fsm_array->num_states <= FSM_NUM_STATES_MAX)) {
		decoder->error = CODEC_ERROR_NUM_STATES;
		return false;
	}

	// Set the number of lookup tables
	fsm_table->num_states = fsm_array->num_states;

	// Compute the size of each lookup table
	lut_size = sizeof(FSMENTRY) << FSM_INDEX_SIZE;

	//DPRINTF("FSM states: %d, lookup table size: %d", fsm_table->num_states, lut_size);

	for (i = 0; i < fsm_table->num_states; i++)
	{
		FSMENTRY *lut;
		FSMENTRY_UNPACKED *unpacked_lut = (FSMENTRY_UNPACKED *)fsm_array->entries+(i << FSM_INDEX_SIZE);
#if	_FSM_NO_POINTERS
		lut = &fsm_table->entries[i][0];
#else
		// Allocate a lookup table for the current state
		lut = (FSMENTRY *)MEMORY_ALIGNED_ALLOC(lut_size, 128);
		assert(lut != NULL);
		if (! (lut != NULL)) {
			decoder->error = CODEC_ERROR_FSM_ALLOC;
			return false;
		}

		// Set the pointer to the lookup table
		fsm_table->entries[i] = lut;
#endif

		//DPRINTF("FSM entry: %d, lookup table: 0x%p", i, lut);

		if (lut != NULL && unpacked_lut != NULL)
		{
			// Copy the finite state machine entries into the lookup table
			//memcpy(lut, fsm_array->entries+(i << FSM_INDEX_SIZE), lut_size);
			int table_length = (1 << FSM_INDEX_SIZE);
			for (j = 0; j < table_length; j++)
			{
				lut->next_state = unpacked_lut->next_state;
				lut->pre_post_skip = unpacked_lut->pre_skip | (unpacked_lut->post_skip<<12);
				lut->value0 = unpacked_lut->values[0];
				lut->value1 = unpacked_lut->values[1];

				lut++;
				unpacked_lut++;
			}
		}
	}

	// The decoding finite state machine was initialized successfully
	return true;
}

#elif _SINGLE_FSM_TABLE

#include "codec.h"
void FillFSM(FSMTABLE *fsm_table, const FSMARRAY *fsm_array)
{
	size_t lut_size;			// Size of each lookup table (in bytes)
	int i;
	FSMENTRY *firstentry; //DAN

	{
		int total_entry_num = (fsm_array->num_states << FSM_INDEX_SIZE);

		// Check that all entries will fit in the table
		assert(fsm_array->num_states <= FSM_NUM_STATES_MAX);

		// Set the number of lookup tables
		fsm_table->num_states = fsm_array->num_states;

		// Compute the size of each lookup table
		lut_size = sizeof(FSMENTRY);

		firstentry = (FSMENTRY *)MEMORY_ALIGNED_ALLOC(lut_size * total_entry_num, 16); //DAN
		fsm_table->firstentry = firstentry;

		for (i = 0; i < total_entry_num; i++)
		{
			FSMENTRY *entry;
			FSMENTRY old_entry = fsm_array->entries[i];

			entry = firstentry+i; //DAN

			entry->values[0] = old_entry.values[0];
			entry->values[1] = old_entry.values[1];
			entry->pre_skip = old_entry.pre_skip;
			entry->post_skip = old_entry.post_skip;
			entry->next_state = old_entry.next_state;
		}
	}
}

#else

#include "codec.h"

void FillFSM(FSMTABLE *fsm_table, const FSMARRAY *fsm_array)
{
	size_t lut_size;			// Size of each lookup table (in bytes)
	int i;

	int total_entry_num = (fsm_array->num_states << FSM_INDEX_SIZE);

	// Check that all entries will fit in the table
	assert(fsm_array->num_states <= FSM_NUM_STATES_MAX);

	// Set the number of lookup tables
	fsm_table->num_states = fsm_array->num_states;

	// Compute the size of each lookup table
	lut_size = sizeof(FSMENTRY);

	for (i = 0; i < total_entry_num; i++)
	{
		FSMENTRY *entry;
		FSMENTRY old_entry = fsm_array->entries[i];

		// Allocate a lookup table entry for the current state
		if(old_entry.values[0] != BAND_END_TRAILER)
		{
			entry = (FSMENTRY *)MEMORY_ALIGNED_ALLOC(lut_size, 16);
			assert(entry != NULL);

			entry->values[0] = old_entry.values[0];
			entry->values[1] = old_entry.values[1];
			entry->pre_skip = old_entry.pre_skip;
			entry->post_skip = old_entry.post_skip;
			entry->next_state = old_entry.next_state;
		}
		else
		{
			entry = NULL;
		}

		fsm_table->entries_ind[i] = entry;
	}
}

#endif

#else

// Fill the finite state machine with look-up tables generated by Huffman program
void FillFSM(FSMTABLE *fsm_table, const FSMARRAY *fsm_array)
{
	size_t lut_size;			// Size of all lookup tables (in bytes)
	FSMENTRY *lut;

	// Check that all entries will fit in the table
	assert(fsm_array->num_states <= FSM_NUM_STATES_MAX);

	// Set the number of lookup tables
	fsm_table->num_states = fsm_array->num_states;

	// Compute the size of the collection of lookup tables
	lut_size = fsm_array->num_states * (sizeof(FSMENTRY) << FSM_INDEX_SIZE);

	// Allocate a lookup table for the current state
	lut = (FSMENTRY *)MEMORY_ALIGNED_ALLOC(lut_size, 128);
	assert(lut != NULL);

	// Copy the finite state machine entries into the lookup table
	memcpy(lut, fsm_init->entries, lut_size);

	// Set the pointer to the lookup tables
	fsm_table->entries = lut;
}

#endif

#if _COMPANDING

void ScaleFSM(FSMTABLE *fsm_table)
{
	int i, j;

	// Has companding already been applied to this table?
	if(fsm_table->flags & FSMTABLE_FLAGS_COMPANDING_DONE) return;

	// no companding for this table
	if(fsm_table->flags & FSMTABLE_FLAGS_COMPANDING_NOT_NEEDED) return;



#if _INDIVIDUAL_LUT
#if !_INDIVIDUAL_ENTRY

	if(fsm_table->flags & FSMTABLE_FLAGS_COMPANDING_CUBIC)
	{
		for (i = 0; i < fsm_table->num_states; i++)
		{
			FSMENTRY *entry = fsm_table->entries[i];
			for (j = 0; j < (1 << FSM_INDEX_SIZE); j++)
			{
				if(entry[j].value0 < 264)
				{
					double cubic;
					int mag = abs(entry[j].value0);

					cubic = mag;
					cubic *= mag;
					cubic *= mag;
					cubic *= 768;
					cubic /= 256*256*256;

					mag += (int)cubic;

					if(entry[j].value0 < 0)
					{
						mag = -mag;
					}

					entry[j].value0 = mag;
				}
			}
		}
	}
	else
	{

		for (i = 0; i < fsm_table->num_states; i++)
		{
			FSMENTRY *entry = fsm_table->entries[i];
			for (j = 0; j < (1 << FSM_INDEX_SIZE); j++)
			{
				if (entry[j].value0 >= 40 && entry[j].value0 < 264)
				{
	#if _COMPANDING_MORE
					if(entry[j].value0 >= _COMPANDING_MORE)
					{
						entry[j].value0 -= _COMPANDING_MORE;
						entry[j].value0 <<= 2;
						entry[j].value0 += _COMPANDING_MORE;
					}
	#endif
					entry[j].value0 -= 40;
					entry[j].value0 <<= 2;
					entry[j].value0 += 40;
				}
				else if(entry[j].value0 <= -40)
				{
				//	entry[j].value0 += 40;
				//	entry[j].value0 <<= 2;
				//	entry[j].value0 -= 40;

					entry[j].value0 = -entry[j].value0;

	#if _COMPANDING_MORE
					if(entry[j].value0 >= _COMPANDING_MORE)
					{
						entry[j].value0 -= _COMPANDING_MORE;
						entry[j].value0 <<= 2;
						entry[j].value0 += _COMPANDING_MORE;
					}
	#endif

					entry[j].value0 -= 40;
					entry[j].value0 <<= 2;
					entry[j].value0 += 40;
					entry[j].value0 = -entry[j].value0;
				}
			}
		}
	}
#else

	for (i = 0; i < (fsm_table->num_states << FSM_INDEX_SIZE); i++)
	{
		FSMENTRY *entry = fsm_table->entries_ind[i];

		if(entry)
		{
			if (entry->value0 >= 40 && entry->value0 < 264)
			{
				entry->value0 -= 40;
				entry->value0 <<= 2;
				entry->value0 += 40;
			}
			else if(entry->value0 <= -40)
			{
			//	entry->value0 += 40;
			//	entry->value0 <<= 2;
			//	entry->value0 -= 40;

				entry[j].value0 = -entry[j].value0;
				entry->value0 -= 40;
				entry->value0 <<= 2;
				entry->value0 += 40;
				entry[j].value0 = -entry[j].value0;
			}
		}
	}

#endif
#else
	FSMENTRY *entry = fsm_table->entries;
	int i;

	for (i = 0; i < (fsm_table->num_states << FSM_INDEX_SIZE); i++)
	{
		if (entry->value0 >= 40 && entry->value0 < 264)
		{
			entry->value0 -= 40;
			entry->value0 <<= 2;
			entry->value0 += 40;
		}
		else if (entry->value0 <= -40)
		{
		//	entry->value0 += 40;
		//	entry->value0 <<= 2;
		//	entry->value0 -= 40;

			entry[j].value0 = -entry[j].value0;
			entry->value0 -= 40;
			entry->value0 <<= 2;
			entry->value0 += 40;
			entry[j].value0 = -entry[j].value0;
		}

		//if(abs(entry->value0) < 64)
		//	entry->value0 <<= 1;

		entry++;
	}
#endif

	// Indicate that companding has been applied
	fsm_table->flags |= FSMTABLE_FLAGS_COMPANDING_DONE;
}

#endif


// Initialize a finite state machine to work with the specified table
void InitFSM(FSM *fsm, FSMTABLE *table)
{
	fsm->next_state = fsm->table.entries[0];
  #if _INDIVIDUAL_ENTRY
	fsm->next_state_index = 0;
  #endif

  #if _DEQUANTIZE_IN_FSM
  if(fsm->LastQuant == 0) //uninitized

  {
	fsm->InitizedRestore = 0;
	fsm->LastQuant = 1;
  }
  #endif

}

#if 0
void DumpFSM(DECODER *decoder)
{
	int i;

	for (i = 0; i < CODEC_NUM_CODESETS; i++)
	{
		FSM *fsm = &decoder->fsm[i];
		int j;

		DPRINTF("FSM %d, flags: 0x%04X, states: %d", i, (WORD)fsm->table.flags, fsm->table.num_states);

		for (j = 0; j < fsm->table.num_states; j++)
		{
			FSMENTRY *entry = fsm->table.entries[j];
			int table_length = (1 << FSM_INDEX_SIZE);
			int k;

			DPRINTF("FSM state: %d", j);

			for (k = 0; k < table_length; k++)
			{
				DPRINTF("LUT %d, next state: %d, skip: %d, value0: %d, value1: %d",
					k, entry->next_state, entry->pre_post_skip, entry->value0, entry->value1);
				entry++;
			}
		}
	}
}
#endif

#if _DEBUG

void PrintCodeLookupTable(FILE *logfile, FLC *table, int size)
{
	int num_entries = 1 << size;
	int i;

	for (i = 0; i < num_entries; i++) {
		int shift = table[i].shift;
		uint32_t prefix = i >> (size - shift);
		fprintf(logfile,
				"0x%08X 0x%08X %4d %4d %4d\n",
				i, prefix, table[i].count, table[i].value, table[i].shift);
	}
}

// Print the codebook used for fast encoding of run lengths
void PrintRunLengthTable(FILE *logfile, RLCBOOK *runsbook)
{
	RLC *code = (RLC *)(((char *)runsbook) + sizeof(RLCBOOK));
	int length = runsbook->length;
	int32_t total_lookups = 0;
	int32_t total_bitcount = 0;
	float average_lookups;
	float average_bitcount;
	int i;

	for (i = 0; i < length; i++) {
		int size = code[i].size;
		int32_t bits = code[i].bits;
		int count = code[i].count;
		int remainder = i - count;
		int bitcount = size;
		int num_lookups = 1;

		// Count the number of table lookups required to cover this run length
		// and sum the total number of bits that will be used to encode this run
		for (; remainder > 0; num_lookups++) {
			bitcount += code[remainder].size;
			remainder -= code[remainder].count;
		}

		// Sum the total number of table lookups for computing the average
		total_lookups += num_lookups;

		// Sum the total number of bits for computing the average
		total_bitcount += bitcount;

		fprintf(logfile, "%5d %5d 0x%08X %-2d %3d %3d\n", i, count, bits, size, num_lookups, bitcount);
	}

	average_lookups = ((float)total_lookups)/length;
	fprintf(logfile, "\nAverage number of table lookups: %.3f\n", average_lookups);

	average_bitcount = ((float)total_bitcount)/length;
	fprintf(logfile, "\nAverage number of bits for encoding each run: %.3f\n", average_bitcount);
}

void PrintFastLookupTable(FILE *logfile, FLCBOOK *fastbook)
{
	// Get the length of the lookup table and initialize a pointer to its entries
	int length = fastbook->length;
	int size = fastbook->size;
	FLC *table = (FLC *)((char *)fastbook + sizeof(FLCBOOK));
	int32_t total_shift = 0;
	float efficiency;
	int i;

	for (i = 0; i < length; i++) {
		uint32_t  bits = i;
		fprintf(logfile, "0x%03X %-3d %5d %4d\n", bits, table[i].shift, table[i].count, table[i].value);

		// Tabulate the number of bits that were used
		total_shift += table[i].shift;
	}

	// Compute the efficiency is using bits read from the bitstream
	efficiency = (float)total_shift / ((float)length * size);

	fprintf(logfile, "\n");
	fprintf(logfile, "Efficiency: %.2f percent\n", 100.0 * efficiency);
}

void PrintValueCodebook(FILE *logfile, VALBOOK *codebook)
{
	VLC *table = (VLC *)((char *)codebook + sizeof(VALBOOK));
	int length = codebook->length;
	int index;

	// Check that the value codebook was designed for 8-bit values
	assert(codebook->size == 8);

	// Print out the entries in the value codebook
	for (index = 0; index < length; index++) {
		int value = (char)index;
		fprintf(logfile, "Value book entry %d, value: %d, codeword: 0x%08X, size: %d\n",
			index, value, table[index].bits, table[index].size);
	}
}

#endif

#if _TEST

static BITSTREAM buffer;
static int lengths[] = {5, 2,  3, 1,  5, 2, 4, 3, 1,  1,  3, 10,  1, 1, 7, 8,  4};
static int32_t values[] = {0, 3, -2, 1, -3, 4, 7, 0, 1, -7, -1, -4, -5, 5, 6, 2, -6};
static int32_t result[1024];

bool TestCodeSet(CODESET *cs)
{
	BITSTREAM *stream = &buffer;
	int i;

	// Check the validity of the codebooks
	if (!IsValidCodebook(cs->magsbook)) return false;

	// Initialize the bitstream
	InitBitstream(stream);

	// Output some sample values
	for (i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
		PutVlcSigned(stream, values[i], cs->magsbook);
	}

	// Rewind the bitstream
	RewindBitstream(stream);

	// Initialize the array that will hold the decoded results
	memset(result, 0, sizeof(result));

	// Read the sample values from the bitstream
	for (i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
		int32_t sample = GetVlcSigned(stream, cs->magsbook);
		result[i] = sample;
		//assert(sample == values[i]);
	}

	// Compare the results with the input values
	for (i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
		if (result[i] != values[i]) return false;
	}

	// Initialize the bitstream for the second test
	InitBitstream(stream);

	// Test the run length coding
	for (i = 0; i < sizeof(lengths)/sizeof(lengths[0]); i++) {
		PutRlcSigned(stream, lengths[i], values[i], cs->codebook);
	}

	// Rewind the bitstream
	RewindBitstream(stream);

	// Initialize the array that will hold the decoded results
	//memset(result, 0, sizeof(result));

	// Read the sample run lengths and values from the bitstream
	for (i = 0; i < sizeof(lengths)/sizeof(lengths[0]); i++) {
		RUN sample;
		int count = lengths[i];
		int32_t value = values[i];

		// Read samples until the correct number have been read
		while (count > 0)
		{
			// Get the next run length and value
			int result = GetRlcSigned(stream, &sample, cs->runsbook);
			if (result != VLC_ERROR_OKAY) return false;

			// Check the value
			if (sample.value != value) return false;

			// Check the count
			if (sample.count > count) return false;

			// Decrement the count of values that must be read
			count -= sample.count;
		}

		// Should have exited the loop after reading exactly the correct number of values
		if (count != 0) return false;
	}

	return true;
}

#endif
