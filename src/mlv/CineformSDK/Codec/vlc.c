/*! @file vlc.c

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
#include "config.h"
#include "timing.h"

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif
#define TIMING (1 && _TIMING)
#define XMMOPT (1 && _XMMOPT)


#include "vlc.h"
#include "codec.h"
#include "bitstream.h"
#include <limits.h>
#include <assert.h>

/*
	The coding routines assume that the codebook is sorted into increasing
	order of the number of codeword bits and that the non-negative value that
	corresponds to the codeword can be used as an index into the codebook to
	map the value into its codeword.  The reverse mapping is done by linear
	search through the codebook, reading successively more bits from the stream
	as the codeword size increases with later entries in the table.
*/
struct {
	VLCBOOK header;
	VLC entries[];
} table1a = {
    {8},
	{
        {1, 0x0000},
        {2, 0x0002},
        {3, 0x0006},
        {4, 0x000E},
        {6, 0x003D},
        {9, 0x01F1},
        {12, 0x0FD7},
        {14, 0x3F52}
   }
};

VLCBOOK *coeff1a = (VLCBOOK *)&table1a;


#define SIGN(x) (((x) > 0) ? 1 : (((x) < 0) ? -1 : 0))


#if _TIMING

extern COUNTER decode_lookup_count;		// Number of codewords decoded by direct table lookup
extern COUNTER decode_search_count;		// Number of codewords decoded by secondary search
extern COUNTER putvlcbyte_count;		// Number of calls to PutVlcByte()
extern COUNTER putzerorun_count;		// Number of calls to PutZeroRun()

#endif


bool IsValidCodebook(VLCBOOK *codebook)
{
	// Get the length of the codebook and a pointer to the entries
	int length = codebook->length;
	//int32_t maximum_value = length - 1;
	VLC *code = (VLC *)((char *)codebook + sizeof(VLCBOOK));

	// Check that no code is a prefix of a later code in the book
	int i, j;
	for (i = 0; i < length; i++)
	{
		// Use this codeword as a prefix
		uint32_t  word = code[i].bits;
		int size = code[i].size;

		for (j = i+1; j < length; j++)
		{
			// Get the prefix of this codeword
			uint32_t  prefix = code[j].bits >> (code[j].size - size);

			// If the first codeword is a prefix of a later codeword,
			// then the later code table entry will never be matched.
			if (word == prefix) return false;
		}

		// Check that the codeword lengths are non-decreasing
		if ((i > 0) && (code[i].size < code[i-1].size)) return false;
	}

	return true;
}

// Output the variable length code for a single value
int32_t PutVlc(BITSTREAM *stream, int32_t value, VLCBOOK *codebook)
{
	// Get the length of the codebook and a pointer to the entries
	int length = codebook->length;
	int32_t maximum_value = length - 1;
	VLC *code = (VLC *)((char *)codebook + sizeof(VLCBOOK));

	// Check that the value is in range
	//assert(0 <= value && value <= maximum_value);

	// Saturate the value to fit the size of the codebook
	if (value > maximum_value) {
		value = maximum_value;

#if (0 && DEBUG)
		// Count the number of coefficients that were saturated
		stream->nSaturated++;
#endif
	}

	// Lookup the code in the book
	code = &code[value];

	// Output the bits for the magnitude of the value
	PutBits(stream, code->bits, code->size);

	return code->size;
}

// Output the code for the magnitude of a value and the sign
int32_t PutVlcSigned(BITSTREAM *stream, int32_t value, VLCBOOK *codebook)
{
	// Get the magnitude of the value
	int32_t magnitude = abs(value);

	// Get the length of the codebook and a pointer to the entries
	int length = codebook->length;
	int32_t maximum_value = length - 1;
	VLC *code = (VLC *)((char *)codebook + sizeof(VLCBOOK));

	uint32_t  bits;
	int size;

	// Check that the value is in range
	//assert(0 <= magnitude && magnitude <= maximum_value);

	// Saturate the value to fit the size of the codebook
	if (magnitude > maximum_value)
	{
		magnitude = maximum_value;

#if (0 && DEBUG)
		// Count the number of coefficients that were saturated
		stream->nSaturated++;
#endif
	}

	// Lookup the code in the book
	code = &code[magnitude];
	bits = code->bits;
	size = code->size;

	// Combine the magnitude and sign into a single codeword
	assert(VLC_NEGATIVE_CODE == 0x01 && VLC_NEGATIVE_SIZE == 1);
	if (value != 0) {
		bits = (bits << 1);
		if (value < 0) bits++;
		size++;
	}

	// Output the bits for the magnitude of the value
	PutBits(stream, bits, size);

	return size;
}

// Output the code for a signed eight bit coefficient
void PutVlcByte(BITSTREAM *stream, int value, VALBOOK *codebook)
{
	// Get a pointer into the codebook and the index size (in bits)
	//int size = codebook->size;
	int length = codebook->length;
#if USE_UNPACKED_VLC
	VLC *table = (VLC *)((char *)codebook + sizeof(VALBOOK));
#else
	VLE *table = (VLE *)((char *)codebook + sizeof(VALBOOK));
	uint32_t codeword;
	uint32_t codesize;
#endif
	int index;

	// Check that the lookup table is correct for this algorithm
#if !_COMPANDING_MORE
	assert(size == 8);
	// Check that the value is within the range of the lookup table
	assert(SCHAR_MIN <= value && value <= SCHAR_MAX);
#endif

	// Convert the value to an unsigned byte index into the codebook
#if !_COMPANDING_MORE
	index = (unsigned char)value;
#else //10 bit table
	if(value < 0)
		index = 1024 + value;
	else
		index = value;
#endif

	if (index < 0) index = 0;
	else if (index >= length) index = length - 1;

	// Check that the index is within the range of the lookup table
	assert(0 <= index && index < length);

#if USE_UNPACKED_VLC
	// Use the original version of the codebook entry
	PutBits(stream, table[index].bits, table[index].size);
#else
	// Use the packed version of the codebook entry
	codeword = table[index].entry & VLE_CODEWORD_MASK;
	codesize = table[index].entry >> VLE_CODESIZE_SHIFT;
	assert((codesize & ~((uint32_t)VLE_CODESIZE_MASK)) == 0);
	PutBits(stream, codeword, codesize);
#endif

#if (0 && DEBUG)
	// Check for output values that were saturated by the codebook
	if (value > 0) {
		// Is the codebook entry equal to the entry for the maximum value?
		if (table[index].bits == table[SCHAR_MAX].bits && table[index].size == table[SCHAR_MAX].size) {
			// Increment the count of output values that were saturated
			stream->nSaturated++;
		}
	}
	else if (value < 0) {
		// Is the codebook entry equal to the entry for the minumum value?
		if (table[index].bits == table[SCHAR_MAX+1].bits && table[index].size == table[SCHAR_MAX+1].size) {
			// Increment the count of output values that were saturated
			stream->nSaturated++;
		}
	}
#endif

#if (1 && TIMING)
	putvlcbyte_count++;
#endif
}

// Get the value corresponding to a variable length code from the bitstream
int32_t GetVlc(BITSTREAM *stream, VLCBOOK *codebook)
{
	int size = 0;			// Number of bits read so far
	uint32_t  bits = 0;		// Bit string read from the stream

	// Get a pointer into the codebook and the codebook length
	int32_t length = codebook->length;
	VLC *code = (VLC *)((char *)codebook + sizeof(VLCBOOK));
	int32_t value;

	for (value = 0; value < length; value++)
	{
		// Need to read more bits from the bitstream?
		if (size < code->size)
		{
			// Compute the number of bits to read from the stream
			bits = AddBits(stream, bits, code->size - size);

			// Update the count of bits read from the stream
			size = code->size;
		}

#if (1 && DEBUG)
		// Check that the codeword does not contain extra bits
		if (size < BITSTREAM_LONG_SIZE) {
			assert((bits & ~BITMASK(size)) == 0);
			assert((code->bits & ~BITMASK(size)) == 0);
		}
#endif
		// Break out of the loop if we have found the codeword
		if (bits == code->bits) break;

		// Advance to the next codebook entry
		code++;
	}

	// Was a codeword found?
	if (value < length)
		return value;
	else
		return VLC_ERROR_NOTFOUND;
}

// Get a signed quantity from the bitstream
int32_t GetVlcSigned(BITSTREAM *stream, VLCBOOK *codebook)
{
	// Get the magnitude of the number from the bitstream
	int32_t value = GetVlc(stream, codebook);

	// Error while parsing the bitstream?
	if (value < 0) {
		stream->error = value;
		return value;
	}

	// Signed quantity?
	if (value != 0) {
		int sign;

		// Check that the sign codes have the same length
		assert(VLC_POSITIVE_SIZE == VLC_NEGATIVE_SIZE);

		// Something is wrong if the value is already negative
		assert(value > 0);

		// Get the sign bit
		sign = GetBits(stream, VLC_NEGATIVE_SIZE);

		// Change the sign if the sign bit was negative
		if (sign == VLC_NEGATIVE_CODE)
			value = (-value);
	}

	return value;
}

int32_t PutRun(BITSTREAM *stream, int count, RLCBOOK *codebook, int *remainder)
{
	// Get the length of the codebook and a pointer to the entries
	int length = codebook->length;
	RLC *rlc = (RLC *)((char *)codebook + sizeof(RLCBOOK));
	int i;
	uint32_t bitcount = 0;		// Count of bits output to the stream

	// Output one or more run lengths until the run is finished
	while (count > 0)
	{
		// Index into the codebook to get a run length code that covers most of the run
		i = (count < length) ? count : length - 1;

		// Output the run length code
		PutBits(stream, rlc[i].bits, rlc[i].size);
		bitcount += rlc[i].size;

		// Reduce the length of the run by the amount output
		count -= rlc[i].count;
	}

	// Return the remaining length of the run
	*remainder = count;

	// Return the number of bits output to the stream
	return bitcount;
}

// Simplified version of the general routine above for encoding a run
void PutZeroRun(BITSTREAM *stream, int count, RLCBOOK *codebook)
{
	// Get the length of the codebook and a pointer to the entries
	int length = codebook->length;
	RLC *rlc = (RLC *)((char *)codebook + sizeof(RLCBOOK));
	int index;

	// Output one or more run lengths until the run is finished
	while (count > 0)
	{
		// Index into the codebook to get a run length code that covers most of the run
		index = (count < length) ? count : length - 1;

		// Output the run length code
		PutBits(stream, rlc[index].bits, rlc[index].size);

#if (0 && DEBUG)
		if (stream->putbits_flag) {
			fprintf(stream->logfile, "%d %08X\n", rlc[index].bits, rlc[index].size);
		}
#endif
		// Reduce the length of the run by the amount output
		count -= rlc[index].count;
	}

	// Should have output enough runs to cover the run of zeros
	assert(count == 0);

#if (1 && TIMING)
	putzerorun_count++;
#endif
}

// Further simplified version of the routine above for encoding a run
void PutFastRun(BITSTREAM *stream, int count, RLCBOOK *codebook)
{
	// Get the length of the codebook and a pointer to the entries
	//int length = codebook->length;
	RLC *rlc = (RLC *)((char *)codebook + sizeof(RLCBOOK));
	//int index;

	// Check the run length is in the range of the table
	//assert(0 < count && count < length);

	// Output one or more run lengths until the run is finished
	//while (count > 0)
	{
		// Index into the codebook to get a run length code that covers most of the run
		//index = (count < length) ? count : length - 1;

		// Output the run length code
		//PutBits(stream, rlc[index].bits, rlc[index].size);
		PutBits(stream, rlc[count].bits, rlc[count].size);

		// Reduce the length of the run by the amount output
		//count -= rlc[index].count;

		assert(count == rlc[count].count);
	}

	// Should have output enough runs to cover the run of zeros
	//assert(count == 0);

#if (1 && TIMING)
	putzerorun_count++;
#endif
}

// Output the variable length codes for a run of values
int32_t PutRlc(BITSTREAM *stream, int count, int32_t value, RMCBOOK *codebook)
{
	RLCBOOK *runbook = codebook->runbook;
	VLCBOOK *magbook = codebook->magbook;
	uint32_t bitcount = 0;

	// Check the range of the run length count
	assert(0 < count && count < runbook->length);

	// This routine should only be called with non-negative values
	assert(0 <= value && value < magbook->length);

	// Only zero values are run length coded
	if (value == 0) {
		int remainder;
		bitcount += PutRun(stream, count, runbook, &remainder);

		// May not have output the entire run using run length codes
		count = remainder;
	}

	// Duplicate the value for the specified number of times
	if (count > 0) {
		int i;
		for (i = 0; i < count; i++)
			bitcount += PutVlc(stream, value, magbook);
	}

	return bitcount;
}

// Output the variable length codes for a run of signed values
int32_t PutRlcSigned(BITSTREAM *stream, int count, int32_t value, RMCBOOK *codebook)
{
	RLCBOOK *runbook = codebook->runbook;
	VLCBOOK *magbook = codebook->magbook;
	uint32_t bitcount = 0;

	// Output a run of zeros
	if (value == 0) {
		int remainder;
		bitcount += PutRun(stream, count, runbook, &remainder);

		// May not have output the entire run using run length codes
		count = remainder;
	}

	// Output remaining zeros or run of non-zero values
	if (count > 0) {
		int i;

		// Get the sign and magnitude of the value
		int sign = SIGN(value);
		value = abs(value);

		// Duplicate the magnitude and sign
		for (i = 0; i < count; i++) {
			bitcount += PutVlc(stream, value, magbook);
			if (sign > 0) {
				PutBits(stream, VLC_POSITIVE_CODE, VLC_POSITIVE_SIZE);
				bitcount += VLC_POSITIVE_SIZE;
			}
			else if (sign < 0) {
				PutBits(stream, VLC_NEGATIVE_CODE, VLC_NEGATIVE_SIZE);
				bitcount += VLC_NEGATIVE_SIZE;
			}
		}
	}

	// Return the number of bits output to the stream
	return bitcount;
}

// New version that uses a single codebook for runs and magnitudes
int GetRlc(BITSTREAM *stream, RUN *run, RLVBOOK *codebook)
{
	int size = 0;			// Number of bits read so far
	uint32_t  bits = 0;		// Bit string read from the stream

	// Get the length of the codebook and initialize a pointer to its entries
	int32_t length = codebook->length;
	RLV *rlc = (RLV *)((char *)codebook + sizeof(RLVBOOK));

	// Index into the codebook
	int i = 0;

	// Search the codebook for the run length and value
	while (i < length) {
		int codesize = rlc[i].size;

		// Need to read more bits from the stream?
		if (size < codesize) {
			int n = codesize - size;
			bits = AddBits(stream, bits, n);
			size = codesize;
		}

		// Examine the run length table entries that have the same bit field length
		for (; (i < length) && (size == rlc[i].size); i++) {
			if (bits == rlc[i].bits) {
				run->count = rlc[i].count;
				run->value = rlc[i].value;
				goto found;
			}
		}
	}

	// Did not find a matching code in the codebook
	return VLC_ERROR_NOTFOUND;

found:
	return VLC_ERROR_OKAY;
}

int GetRlcSigned(BITSTREAM *stream, RUN *run, RLVBOOK *codebook)
{
	int32_t value;
	int error;

	// Get the magnitude of the number from the bitstream
	error = GetRlc(stream, run, codebook);

	// Error while parsing the bitstream?
	if (error < 0) {
		stream->error = error;
		return error;
	}

	// Trace the progress in reading run lengths from the bitstream
	//_RPT2(_CRT_WARN, "GetRlcSigned length: %d, value: %d\n", run->count, run->value);

	// Restore the sign to the magnitude of the run value
	value = run->value;

	// Signed quantity?
	if (value != 0) {
		int sign;

		// Check that the sign codes have the same length
		assert(VLC_POSITIVE_SIZE == VLC_NEGATIVE_SIZE);

		// Something is wrong if the value is already negative
		assert(value > 0);

		// Get the sign bit
		sign = GetBits(stream, VLC_NEGATIVE_SIZE);

		// Change the sign if the sign bit was negative
		if (sign == VLC_NEGATIVE_CODE)
			run->value = (-value);
	}

	return VLC_ERROR_OKAY;
}

// Lookup the code in the standard codebook
int GetRlcIndexed(BITSTREAM *stream, RUN *run, RLVBOOK *codebook, int index)
{
	int size = 0;			// Number of bits read so far
	uint32_t  bits = 0;		// Bit string read from the stream
	int i;					// Index into the codebook

	// Get the length of the codebook and initialize a pointer to its entries
	int32_t length = codebook->length;
	RLV *rlc = (RLV *)((char *)codebook + sizeof(RLVBOOK));

	// Check the index provided as a argument
	assert(0 <= index && index < length);

	// Search the codebook from the beginning if the starting index is wrong
	if (!(0 <= index && index < length)) index = 0;

	// Search the codebook starting at the index provided as an argument
	for (i = index; i < length; ) {
		int codesize = rlc[i].size;

		// The codebook entries must be sorted into increasing codeword length
		assert(size <= codesize);

		// Need to read more bits from the stream?
		if (size < codesize) {
			int n = codesize - size;
			bits = AddBits(stream, bits, n);
			size = codesize;
		}

		// Examine the run length table entries that have the same bit field length
		for (; (i < length) && (size == rlc[i].size); i++) {
			if (bits == rlc[i].bits) {
				run->count = rlc[i].count;
				run->value = rlc[i].value;
#if _TIMING
				decode_search_count++;
#endif
				goto found;
			}
		}
	}

	// Did not find a matching code in the codebook
	return VLC_ERROR_NOTFOUND;

found:
	return VLC_ERROR_OKAY;
}

// Fast run length code lookup
int LookupRlc(BITSTREAM *stream, RUN *run, FLCBOOK *fastbook, RLVBOOK *codebook)
{
	// Get the length of the lookup table and initialize a pointer to its entries
	//int length = fastbook->length;
	FLC *table = (FLC *)((char *)fastbook + sizeof(FLCBOOK));

	// Get the size of word to read from the bitstream
	int size = fastbook->size;

	// Bit string read from the stream
	int index;

	// Check that the fast lookup table was initialized
	assert(size > 0);
	if (size == 0) return VLC_ERROR_NOTFOUND;

	// Read a word from the bitstream to index the fast lookup table
	index = PeekBits(stream, size);

	if (stream->error != BITSTREAM_ERROR_OKAY)
		return VLC_ERROR_NOTFOUND;

	// Is there an entry at that index?
	if (table[index].count > 0)
	{
		// Return the run length and value from the table entry
		run->count = table[index].count;
		run->value = table[index].value;

		// Advance the bitstream by the amount of bits actually used
		//GetBits(stream, table[index].shift);
		SkipBits(stream, table[index].shift);

#if _TIMING
		decode_lookup_count++;
#endif
		return VLC_ERROR_OKAY;
	}
	// Did not find an entry in the fast lookup table

	// Search the codebook starting at the index in the lookup table entry
	assert(table[index].value >= 0);
	return GetRlcIndexed(stream, run, codebook, table[index].value);
}

int LookupRlcSigned(BITSTREAM *stream, RUN *run, FLCBOOK *fastbook, RLVBOOK *codebook)
{
	int32_t value;
	int error;

	// Get the magnitude of the number from the bitstream
	error = LookupRlc(stream, run, fastbook, codebook);

	// Error while parsing the bitstream?
	if (error < 0) {
		if (error != VLC_ERROR_NOTFOUND) {
			stream->error = error;
		}
		return error;
	}

	// Restore the sign to the magnitude of the run value
	value = run->value;

	// Signed quantity?
	if (value != 0) {
		int sign;

		// Check that the sign codes have the same length
		assert(VLC_POSITIVE_SIZE == VLC_NEGATIVE_SIZE);

		// Something is wrong if the value is already negative
		assert(value > 0);

		// Get the sign bit
		sign = GetBits(stream, VLC_NEGATIVE_SIZE);

		// Change the sign if the sign bit was negative
		if (sign == VLC_NEGATIVE_CODE)
			run->value = (-value);
	}

	return VLC_ERROR_OKAY;
}

// Use the new fast lookup table algorithms
int LookupRlcValue(BITSTREAM *stream, RUN *run, FLCBOOK *fastbook, RLVBOOK *codebook)
{
	// Get the length of the lookup table and initialize a pointer to its entries
	//int length = fastbook->length;
	FLC *table = (FLC *)((char *)fastbook + sizeof(FLCBOOK));
	int error;

	// Get the size of word to read from the bitstream
	int size = fastbook->size;

	// Bit string read from the stream
	int index;

	// Check that the fast lookup table was initialized
	assert(size > 0);
	if (size == 0) return VLC_ERROR_NOTFOUND;

	// Read a word from the bitstream to index the fast lookup table
	index = PeekBits(stream, size);

	if (stream->error != BITSTREAM_ERROR_OKAY)
		return VLC_ERROR_NOTFOUND;

	// Is there an entry at that index?
	if (table[index].count > 0)
	{
		// Return the run length and value from the table entry
		run->count = table[index].count;
		run->value = table[index].value;

		// Note that the value in the fast lookup table includes the sign

		// Advance the bitstream by the amount of bits actually used
		SkipBits(stream, table[index].shift);

#if _TIMING
		decode_lookup_count++;
#endif
		return VLC_ERROR_OKAY;
	}
	// Did not find an entry in the fast lookup table

	// Search the codebook starting at the index in the lookup table entry
	assert(table[index].value >= 0);
	error = GetRlcIndexed(stream, run, codebook, table[index].value);
	if (error == VLC_ERROR_OKAY)
	{
		// Need to get the sign from the bitstream if the value is nonzero
		int value = run->value;

		// Signed quantity?
		if (value != 0) {
			int sign;

			// Restore the sign to the magnitude of the run value

			// Check that the sign codes have the same length
			assert(VLC_POSITIVE_SIZE == VLC_NEGATIVE_SIZE);

			// Something is wrong if the value is already negative
			assert(value > 0);

			// Get the sign bit
			sign = GetBits(stream, VLC_NEGATIVE_SIZE);

			// Change the sign if the sign bit was negative
			if (sign == VLC_NEGATIVE_CODE)
				run->value = (-value);
		}
	}

	return error;
}

// Skip runs of zeros and find the next signed value in the bitstream
int ScanRlcValue(BITSTREAM *stream, RUNSTATE *scan, FLCBOOK *fastbook, RLVBOOK *codebook)
{
	int32_t value = 0;
	//int sign;
	int column = scan->column;
	int width = scan->width;
	int error;

	// This routine uses a run state structure to record the position within the row
	// so that the routine does not search past the end of the row.  It returns the
	// signed value of the last run processed and updates the column within the row.

	// Do not read values or runs past the end of the row
	while (column < width && value == 0)
	{
		RUN run;

		// Get the magnitude of the number from the bitstream
#if _OLD_FAST_LOOKUP
		// Use the old fast lookup table algorithm
		error = LookupRlc(stream, &run, fastbook, codebook);
#else
		// Use the new fast lookup table algorithms
		error = LookupRlcValue(stream, &run, fastbook, codebook);
#endif
		// Error while parsing the bitstream?
		if (error < 0) {
			if (error != VLC_ERROR_NOTFOUND) {
				stream->error = error;
			}
			return error;
		}

		// Update the column
		column += run.count;

		// Get the value read from the bitstream
		value = run.value;
	}

	// The new fast lookup table includes the sign in the value

#if _OLD_FAST_LOOKUP
	// Restore the sign to the magnitude of the run value
	if (value != 0)
	{
		int sign;

		// Check that the sign codes have the same length
		assert(VLC_POSITIVE_SIZE == VLC_NEGATIVE_SIZE);

		// Something is wrong if the value is already negative
		assert(value > 0);

		// Get the sign bit
		sign = GetBits(stream, VLC_NEGATIVE_SIZE);

		// Change the sign if the sign bit was negative
		if (sign == VLC_NEGATIVE_CODE)
			value = (-value);
	}
#endif

	// Update the run state
	scan->column = column;
	scan->value = value;

	return VLC_ERROR_OKAY;
}

#if (0 && _DEBUG)

// Return true if the value would be saturated by the codebook
bool IsVlcByteSaturated(VALBOOK *codebook, int value)
{
	// Get a pointer into the codebook and the index size (in bits)
	int size = codebook->size;
	int length = codebook->length;
	VLC *table = (VLC *)((char *)codebook + sizeof(VALBOOK));
	int index;

	// Check that the lookup table is correct for this algorithm
	assert(size == 8);

	// Check that the value is within the range of the lookup table
	assert(SCHAR_MIN <= value && value <= SCHAR_MAX);

	// Convert the value to an unsigned byte index into the codebook
	index = (unsigned char)value;

	// Check that the index is within the range of the lookup table
	assert(0 <= index && index < length);

	// Compare the codebook entry at this index with the maximum and minimum entries
	if (value > 0) {
		// Is the codebook entry equal to the entry for the maximum value?
		if (table[index].bits == table[SCHAR_MAX].bits && table[index].size == table[SCHAR_MAX].size)
			return true;
	}
	else if (value < 0) {
		// Is the codebook entry equal to the entry for the minumum value?
		if (table[index].bits == table[SCHAR_MAX+1].bits && table[index].size == table[SCHAR_MAX+1].size)
			return true;
	}

	return false;
}

#endif
