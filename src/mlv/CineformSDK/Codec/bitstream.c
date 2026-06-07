/*! @file 

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

#include "stdafx.h"
#include "config.h"
#include "timing.h"
#include "allocator.h"

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif

#define TIMING (1 && _TIMING)
#define XMMOPT (1 && _XMMOPT)
#define ASMOPT (1 && _ASMOPT)

//#include <stddef.h>
//#include <stdio.h>
//#include <assert.h>
//#include <memory.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif


#include "bitstream.h"
#include "codec.h"
#include "debug.h"

#include "swap.h"

// Performance measurement
#if TIMING
extern TIMER tk_putbits;
#endif

// Global Data

#define _BITMASK(n)		((((uint32_t )1 << (n))) - 1)

const uint32_t  _bitmask[] =
{
	 _BITMASK(0),  _BITMASK(1),  _BITMASK(2),  _BITMASK(3),
	 _BITMASK(4),  _BITMASK(5),  _BITMASK(6),  _BITMASK(7),
	 _BITMASK(8),  _BITMASK(9), _BITMASK(10), _BITMASK(11),
	_BITMASK(12), _BITMASK(13), _BITMASK(14), _BITMASK(15),
	_BITMASK(16), _BITMASK(17), _BITMASK(18), _BITMASK(19),
	_BITMASK(20), _BITMASK(21), _BITMASK(22), _BITMASK(23),
	_BITMASK(24), _BITMASK(25), _BITMASK(26), _BITMASK(27),
	_BITMASK(28), _BITMASK(29), _BITMASK(30), _BITMASK(31),
	0xFFFFFFFF
};
#undef _BITMASK

// Local Functions
void PutWord(BITSTREAM *stream, uint8_t  word);
//void PutLong(BITSTREAM *stream, uint32_t  word);

// Must declare the byte swap function even though it is an intrinsic
//int _bswap(int);


// Initialize the bitstream and bind it to a buffer
void InitBitstreamBuffer(BITSTREAM *stream, uint8_t  *buffer, size_t length, uint32_t access)
{
	InitBitstream(stream);
	SetBitstreamBuffer(stream, buffer, length, access);
}

// Initialize the bitstream
void InitBitstream(BITSTREAM *stream)
{
	// Initialize the block of words
	stream->dwBlockLength = 0;//BITSTREAM_BLOCK_LENGTH;
	stream->lpCurrentWord = 0;//stream->block;
	stream->lpCurrentBuffer = 0;//stream->block;
	stream->nWordsUsed = 0;

	// Initialize the current bit buffer
	stream->nBitsFree = BITSTREAM_BUFFER_SIZE;
	stream->wBuffer = 0;

	// Initialize the count of the number of bits written to this stream
	stream->cntBits = 0;
	
	// Indicate that we are not writing to a file
#if _POSIX
	stream->file = NULL;
	stream->access = BITSTREAM_ACCESS_NONE;
#else
	stream->file = BITSTREAM_FILE_INVALID;
	stream->access = BITSTREAM_ACCESS_NONE;
#endif

#if _AVIFILES
	// Indicate that the bitstream is not attached to an AVI file
	stream->pavi = BITSTREAM_AVI_INVALID;
#endif

	// No error (refer to error codes in vlc.h)
	stream->error = 0;

	// Assume that the bitstream is four byte aligned within the sample
	stream->alignment = 0;


	{
		int i;
		for(i=0;i<NESTING_LEVELS;i++)
		{
			stream->ChunkSizeOffset[i] = 0;
		}
	}
}

static void FlushStream(BITSTREAM *stream)
{
	int nBitsFree = stream->nBitsFree;

	// Does the buffer contain any data?
	if (nBitsFree < BITSTREAM_BUFFER_SIZE)
	{
		BITSTREAM_BUFFER_T wBuffer = stream->wBuffer;

		// Fill the rest of the buffer with zeros
		wBuffer <<= nBitsFree;

		// Write the buffer to the output
#if _BITSTREAM_BUFFER_BYTE
		PutWord(stream, wBuffer);
#else
		PutLong(stream, wBuffer);
#endif
		assert(stream->error == BITSTREAM_ERROR_OKAY);

		// Indicate that the bitstream buffer is empty
		stream->nBitsFree = BITSTREAM_BUFFER_SIZE;
		stream->wBuffer = 0;
	}
}

void FlushBitstream(BITSTREAM *stream)
{
	FlushStream(stream);
}

void FlushBitstreamAlign(BITSTREAM *stream, int align)
{
	FlushStream(stream);
	{
		int alignment = ((uintptr_t)stream->lpCurrentWord) & (align-1);

		if(alignment == 0) alignment = align;
		while(align-alignment)
		{
			*stream->lpCurrentWord++ = 0;
			stream->nWordsUsed++;
			alignment++;
		}
	}
}

void ResetBitstream(BITSTREAM *stream)
{
	stream->lpCurrentWord = stream->lpCurrentBuffer;
	stream->nWordsUsed = 0;

	// Initialize the current bit buffer
	stream->nBitsFree = BITSTREAM_BUFFER_SIZE;
	stream->wBuffer = 0;
}

// Reset output bitstream for reading
void RewindBitstream(BITSTREAM *stream)
{
	//int nBitsFree = stream->nBitsFree;

	// Flush any bits in the word buffer
	FlushStream(stream);

#if 0
	// Reset the block pointer to the beginning of the buffer
	stream->lpCurrentWord = stream->lpCurrentBuffer;

	// Clear the buffer
	stream->nBitsFree = BITSTREAM_BUFFER_SIZE;
	stream->wBuffer = 0;
#else
	ResetBitstream(stream);
#endif
}

// Get the current position of the pointer into the block
uint8_t  *GetBitstreamPosition(BITSTREAM *stream)
{
	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);
	return (stream->lpCurrentWord);
}

// Set the current position of the pointer into the block
void SetBitstreamPosition(BITSTREAM *stream, uint8_t  *position)
{
#ifdef _WIN32
	LONGLONG skip = ((LONGLONG)position) - ((LONGLONG)stream->lpCurrentWord);
#else
	int64_t skip = ((int64_t)position) - ((int64_t)stream->lpCurrentWord);
#endif
	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);
	stream->lpCurrentWord = position;
	stream->nWordsUsed -= (int)skip;
}

#if 0
// Get word from bitstream
uint8_t  GetWord(BITSTREAM *stream)
{
	// Return the initial value if a word could not be read from the bitstream
	uint8_t  word = 0x0C;

	if (stream->nWordsUsed == 0)
	{
		// The following code assumes that the bit buffer is one byte
		assert(sizeof(uint8_t ) == 1);

		if (stream->file != BITSTREAM_FILE_INVALID &&
			stream->access == BITSTREAM_ACCESS_READ)
		{
#ifdef _WIN32
			// Read a block from the file
			BOOL bReadOkay = ReadFile(stream->file, stream->block, stream->dwBlockLength, &stream->nWordsUsed, NULL);

			// Check that the block was read correctly
			assert(bReadOkay && stream->nWordsUsed > 0);
			if (!bReadOkay || stream->nWordsUsed == 0) {
				// Signal an error but continue for debugging
				stream->error = BITSTREAM_ERROR_READ;
			}
#else
			size_t read_count = fread(stream->block, stream->dwBlockLength, 1, stream->file);

			// Check that the block was read correctly
			assert(read_count > 0);
			if (! (read_count > 0)) {
				// Signal an error but continue for debugging
				stream->error = BITSTREAM_ERROR_READ;
			}
#endif
		}
#if _AVIFILES
		else if (stream->pavi != BITSTREAM_AVI_INVALID &&
				 stream->access == BITSTREAM_AVI_READ)
		{
			int32_t lSampleCount;
			int32_t lByteCount;
			int result;

			result = AVIStreamRead(stream->pavi, stream->sample, AVISTREAMREAD_CONVENIENT,
								   stream->block, sizeof(stream->block), &lByteCount, &lSampleCount);
			assert(result == 0);
			if (result != 0) {
				// Indicate that a sample could not be read from the AVI stream
				stream->nWordsUsed = 0;
				stream->error = BITSTREAM_ERROR_AVISAMPLE;
			}
			else {
				// Record the number of bytes read into the stream buffer
				stream->nWordsUsed = lByteCount;

				// Update the number of samples that have been read from the AVI stream
				stream->sample += lSampleCount;
			}
		}
#endif
		// Update bitstream buffer parameters
		stream->lpCurrentWord = stream->block;
		stream->nBitsFree = BITSTREAM_WORD_SIZE;
		stream->wBuffer = 0;
	}

	// Is there a word in the block?
	assert(stream->nWordsUsed > 0);
	if (stream->nWordsUsed > 0) {
		// Get the next word from the block
		word = *(stream->lpCurrentWord++);
		stream->nWordsUsed--;
	}
	else {
		// Signal that a bitstream error has occurred
		stream->error = BITSTREAM_ERROR_UNDERFLOW;
	}

	return word;
}
#endif

// Read a double word from the bitstream
uint32_t  GetLong(BITSTREAM *stream)
{
	const int nWordsPerLong = sizeof(uint32_t )/sizeof(uint8_t );
	int nWordsUsed = stream->nWordsUsed - nWordsPerLong;
	uint32_t  *lpCurrentWord = (uint32_t  *)(stream->lpCurrentWord);
	uint32_t  longword = 0x0C0C0C0C;

	// This routine assumes that the buffer is empty
	assert(stream->nBitsFree == BITSTREAM_LONG_SIZE);

	// Is there a longword in the block?
	//assert(nWordsUsed >= 0);
	if (nWordsUsed >= 0)
	{
		// Get the int32_t word from the bitstream block
		longword = *(lpCurrentWord++);

		// Byte swap the int32_t word into native endian order (little endian)
		//longword = _bswap(longword);
		longword = SwapInt32BtoN(longword);

		// Update the number of words in the stream
		stream->nWordsUsed = nWordsUsed;

		// Update the pointer into the block
		stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
	}
	else
	{
		// Signal that a bitstream error has occurred
		stream->error = BITSTREAM_ERROR_UNDERFLOW;
	}

	return longword;
}

#if _BITSTREAM_BUFFER_BYTE

// Read the specified number of bits from the stream
uint32_t  GetBits(BITSTREAM *stream, int nBits)
{
	uint8_t  wBuffer = stream->wBuffer;
	int nBitsFree = stream->nBitsFree;
	int nBitsInBuffer = BITSTREAM_WORD_SIZE - nBitsFree;
	uint32_t  output = 0;

	// Make sure that we are not asking for too many bits or too few
	assert(0 < nBits && nBits <= BITSTREAM_LONG_SIZE);

	// Fill the buffer if it is empty
	if (nBitsInBuffer == 0) {
		wBuffer = GetWord(stream);
		if (stream->error != BITSTREAM_ERROR_OKAY)
			return BITSTREAM_UNDEFINED_VALUE;
		nBitsInBuffer = BITSTREAM_WORD_SIZE;
	}

	while (nBits > nBitsInBuffer)
	{
		// Shift bits from the buffer into the output
		output <<= nBitsInBuffer;
		output |= wBuffer;

		// Reduce the number of bits requested by the amount inserted into the output
		nBits -= nBitsInBuffer;

		// Fill the bit buffer with the next word from the bitstream
		wBuffer = GetWord(stream);
		if (stream->error != BITSTREAM_ERROR_OKAY)
			return BITSTREAM_UNDEFINED_VALUE;
		nBitsInBuffer = BITSTREAM_WORD_SIZE;
	}

	// Check that the loop terminated with enough bits in the buffer
	assert(nBits <= nBitsInBuffer);

	// Need more bits?
	if (nBits > 0)
	{
		// Compute number of extra bits in the buffer
		nBitsInBuffer -= nBits;

		// Copy bits from the buffer into the output
		output <<= nBits;
		output |= (wBuffer >> nBitsInBuffer) & BITMASK(nBits);

		// Remove the output bits from the buffer
		wBuffer &= BITMASK(nBitsInBuffer);
	}

	// Update the buffer in the bitstream
	stream->wBuffer = wBuffer;
	stream->nBitsFree = BITSTREAM_WORD_SIZE - nBitsInBuffer;

	return output;
}

#else

// Plain C version suitable for GCC and other compilers

uint32_t  GetBits(BITSTREAM *stream, int nBits)
{
	uint32_t  dwBuffer = (uint32_t )stream->wBuffer;
	int nBitsInBuffer = BITSTREAM_BUFFER_SIZE - stream->nBitsFree;
	uint8_t  *lpCurrentWord = stream->lpCurrentWord;
	uint32_t  dwOutput;
	uint32_t  dwOverflow = 0;

	while (nBits > nBitsInBuffer)
	{
		// Save the high byte in the buffer
		dwOverflow = (dwOverflow << 8) | (dwBuffer >> 24);

		// Shift in the next byte from the bitstream
		dwBuffer <<= 8;
		dwBuffer |= (uint32_t )(*(lpCurrentWord++));

		// Increment the number of bits in the buffer
		nBitsInBuffer += 8;
	}

	nBitsInBuffer -= nBits;
	dwOutput = (dwOverflow << (BITSTREAM_LONG_SIZE - nBitsInBuffer)) | (dwBuffer >> nBitsInBuffer);

	// Eliminate extra bits on the left
	dwBuffer &= BITMASK(nBitsInBuffer);

	// Update the state of the bitstream
	stream->wBuffer = dwBuffer;
	stream->nBitsFree = BITSTREAM_BUFFER_SIZE - nBitsInBuffer;
	stream->lpCurrentWord = lpCurrentWord;

	// Return the bits right justified in the longword with leading zeros
	assert((dwOutput & ~BITMASK(nBits)) == 0);

	return dwOutput;
}

#endif


TAGVALUE GetTagValue(BITSTREAM *stream)
{
	TAGVALUE segment;

	for (;;)
	{
		// Get the next tag value pair
		segment.longword = GetLong(stream);

		// Discard optional tag value pairs
		if (segment.tuple.tag > 0) break;
	}

	return segment;
}

TAGVALUE GetTagOptional(BITSTREAM *stream)
{
	TAGVALUE segment;

	// Check the bitstrean for an optional tag
	segment.longword = PeekLong(stream);

	// Optional tags have the sign bit set
	if (segment.tuple.tag < 0)
	{
		// Clear the option bit
		//segment.tuple.tag &= ~((TAGWORD)CODEC_TAG_OPTIONAL);
		segment.tuple.tag = NEG(segment.tuple.tag);

		// Skip the segment read from the bitstream
		SkipLong(stream);
	}
	else
		segment.longword = 0;

	// Return the tag value pair
	return segment;
}

TAGVALUE GetTagValueAny(BITSTREAM *stream)
{
	TAGVALUE segment;

	segment.longword = GetLong(stream);

	return segment;
}

// Alternate name for routine that returns any segment (required or optional)
TAGVALUE GetSegment(BITSTREAM *stream)
{
	TAGVALUE segment;

	segment.longword = GetLong(stream);

	return segment;
}

// Read the specified tag from the bitstream and return the value
TAGWORD GetValue(BITSTREAM *stream, int tag)
{
	TAGVALUE segment = GetTagValue(stream);

	assert(stream->error == BITSTREAM_ERROR_OKAY);
	if (stream->error == BITSTREAM_ERROR_OKAY) {
		assert(segment.tuple.tag == tag);
		if (segment.tuple.tag == tag) {
			return segment.tuple.value;
		}
		else {
			stream->error = BITSTREAM_ERROR_BADTAG;
		}
	}

	// An error has occurred so return zero
	return 0;
}

void SkipSubband(BITSTREAM *stream)
{
	TAGVALUE segment;

	// Align the bitstream to the tag value pairs
	AlignBitsTag(stream);

	// Scan the bitstream for the band trailer tag word
	do
	{
		segment = GetTagValue(stream);
	}
	while (segment.longword != 0x00380000/*CODEC_TAG_BAND_TRAILER*/ && stream->error == 0);

	// Backup to before the band trailer tag so it can be read again
	stream->lpCurrentWord -= 4;
	stream->nWordsUsed += 4;
}

// Read a 16-bit value from the bitstream
int GetWord16s(BITSTREAM *stream)
{
	const int nWordsPerValue = sizeof(PIXEL16S)/sizeof(uint8_t );
	uint8_t  *lpCurrentWord = stream->lpCurrentWord;
	int nWordsUsed = stream->nWordsUsed + nWordsPerValue;
	short value;

	// This routine assumes that the buffer is empty
	assert(stream->nBitsFree == BITSTREAM_LONG_SIZE);

	// Check that there is something in the block
	assert(nWordsUsed >= nWordsPerValue);
	if (nWordsUsed >= nWordsPerValue)
	{
		// Get the first byte from the bitstream
		value = *(lpCurrentWord++);
		value <<= BITSTREAM_WORD_SIZE;

		// Get the second byte from the bitstream
		value |= *(lpCurrentWord++);

		// Update the number of words in the stream
		stream->nWordsUsed = nWordsUsed;

		// Update the pointer into the block
		stream->lpCurrentWord = lpCurrentWord;
	}
	else {
		// Signal that a bitstream error has occurred
		stream->error = BITSTREAM_ERROR_UNDERFLOW;
		value = 0;
	}

	// Return the signed extended value
	return ((int)value);
}

// Was a valid tag read from the bitstream?
bool IsValidSegment(BITSTREAM *stream, TAGVALUE segment, TAGWORD tag)
{
	return (stream->error == BITSTREAM_ERROR_OKAY &&
			segment.tuple.tag == tag);
}

// Does the tag value pair have the specified tag code and value?
bool IsTagValue(TAGVALUE segment, int tag, TAGWORD value)
{
	return (segment.tuple.tag == tag && segment.tuple.value == value);
}

uint32_t  AddBits(BITSTREAM *stream, uint32_t  dwBitString, int nBitCount)
{
	uint32_t  dwNewBits = GetBits(stream, nBitCount);
	assert((dwNewBits & ~BITMASK(nBitCount)) == 0);

	dwBitString = (dwBitString << nBitCount) | dwNewBits;

	return dwBitString;
}

void SkipBits(BITSTREAM *stream, int nBits)
{
	uint32_t  wBuffer = (uint32_t )stream->wBuffer;
	int nBitsInBuffer = BITSTREAM_BUFFER_SIZE - stream->nBitsFree;

	while(nBits > nBitsInBuffer)
	{
		wBuffer <<= 8;
		wBuffer |= (uint32_t )(*stream->lpCurrentWord++);
		nBitsInBuffer += 8;
	}

	nBitsInBuffer -= nBits;
	wBuffer &= BITMASK(nBitsInBuffer);

	stream->wBuffer = wBuffer;
	stream->nBitsFree = BITSTREAM_BUFFER_SIZE - nBitsInBuffer;

	return;
}

uint32_t  PeekBits(BITSTREAM *stream, int nBits)
{
#if (1 && ASMOPT)

	// Check that the offsets to the bitstream fields have not been changed
	//assert(offsetof(BITSTREAM, nBitsFree) == 4);
	//assert(offsetof(BITSTREAM, lpCurrentWord) == 8);
	//assert(offsetof(BITSTREAM, wBuffer) == 0x14);

	__asm
	{
		mov		esi,dword ptr [stream]		// Address of stream struct
		mov		eax,dword ptr [esi+14h]		// Load the buffer
		mov		ecx,dword ptr [esi+4]		// Number of bits used in buffer
		neg		ecx
		add		ecx,20h
		mov		edx,dword ptr [nBits]		// Number of bits requested

		cmp		ecx,edx						// Enough bits in buffer?
		jge		output						// Do not need to load more bits

		mov		edi,dword ptr [esi+8]		// Get pointer to bytes in bitstream

		shl		eax,16						// Make room for two bytes

		mov		ax,word ptr [edi]			// Load the next two bytes
		xchg	al,ah						// Swap to big endian order

		add		ecx,16

output:
		sub		ecx,edx						// Compute number of excess bits
		shr		eax,cl						// Shift off the excess bits

		// The output is ready in the eax register
	}

#else

	uint8_t  *lpCurrent = stream->lpCurrentWord;
	int nBitsInBuffer = BITSTREAM_BUFFER_SIZE - stream->nBitsFree;
	uint32_t  wBuffer = stream->wBuffer;

	assert(nBits < 17);

	if (nBitsInBuffer < nBits) {
		nBitsInBuffer += 16;
		wBuffer <<= 16;
		wBuffer |= (uint32_t )((*lpCurrent++)<<8);
		wBuffer |= (uint32_t )(*lpCurrent++);
	}

	wBuffer >>= (nBitsInBuffer - nBits);

	return wBuffer;

#endif
}

// Read one byte from the bitstream
uint8_t GetByte(BITSTREAM *stream)
{
#if 0

	uint32_t  dwOutput = GetBits(stream, 8);

#elif 1
	// DecodeBandFSMCombined requires this case to be turned ON.
	// DecodeBandFSMCombined calls this case to empty out the bitstream buffer
	// before it inlines the code in case three
	uint32_t  dwBuffer = stream->wBuffer;
	int nBitsInBuffer = BITSTREAM_BUFFER_SIZE - stream->nBitsFree;
	uint8_t  *lpCurrentWord = stream->lpCurrentWord;
	uint32_t  dwOutput;
	int nBits = 8;

	if (nBits > nBitsInBuffer)
	{
		// Shift in the next byte from the bitstream
		dwBuffer <<= 8;
		dwBuffer |= (uint32_t )(*(lpCurrentWord++));

		// Increment the number of bits in the buffer
		nBitsInBuffer += 8;
	}

	nBitsInBuffer -= nBits;
	dwOutput = (dwBuffer >> nBitsInBuffer);

	// Eliminate extra bits on the left
	dwBuffer &= BITMASK(nBitsInBuffer);

	// Update the state of the bitstream
	stream->wBuffer = dwBuffer;
	stream->nBitsFree = BITSTREAM_BUFFER_SIZE - nBitsInBuffer;
	stream->lpCurrentWord = lpCurrentWord;

	// Return the bits right justified in the longword with leading zeros
	assert((dwOutput & ~BITMASK(nBits)) == 0);

#else	// This third case is inlined in DecodeBandFSMCombined

	uint8_t  *lpCurrentWord = stream->lpCurrentWord;

	// Assume that the bitstream buffer is empty
	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	// Get the next byte from the bitstream
	dwOutput = (uint32_t )(*(lpCurrentWord++));

	// Update the state of the bitstream
	stream->lpCurrentWord = lpCurrentWord;

	// Check that the high bits are zero
	assert((dwOutput & ~BITMASK(8)) == 0);

#endif

	return dwOutput;
}

#if 0
void PutWord(BITSTREAM *stream, uint8_t  word)
{
	assert(stream != NULL);
	if (stream == NULL) return;

#if _AVIFILES
	// This routine has not been updated to write to an AVI stream
	assert(stream->pavi == BITSTREAM_AVI_INVALID);
#endif

	// Is the output block of words full?
	if (stream->nWordsUsed == stream->dwBlockLength)
	{
		// Is the bitstream attached to a file?
		if (stream->file != BITSTREAM_FILE_INVALID &&
			stream->access == BITSTREAM_ACCESS_WRITE)
		{
			DWORD dwByteCount = stream->nWordsUsed * sizeof(uint8_t );
			DWORD dwBytesWritten;
#ifdef _WIN32
			// Write the block to the output file
			bool bWriteOkay = WriteFile(stream->file, stream->block, dwByteCount, &dwBytesWritten, NULL);

			// Check that the block was written correctly
			assert(bWriteOkay && dwByteCount == dwBytesWritten);
			if (!bWriteOkay || dwByteCount != dwBytesWritten) {
				// Signal an error but reset the buffer and continue processing
				// so that more of the program behavior can be seen for debugging
				stream->error = BITSTREAM_ERROR_WRITE;
			}
#else
			// Write the blobk to the output file
			size_t write_count = fwrite(stream->block, dwByteCount, 1, stream->file);

			// Check that the block was written correctly
			assert(write_count > 0);
			if (! (write_count > 0)) {
				// Signal an error but reset the buffer and continue processing
				// so that more of the program behavior can be seen for debugging
				stream->error = BITSTREAM_ERROR_WRITE;
			}
#endif
			// Reset buffer pointers to the beginning of the buffer
			ResetBitstream(stream);
		}
	}

	// Check that there is room in the block for the new word
	assert(stream->nWordsUsed < stream->dwBlockLength);
	if (stream->nWordsUsed < stream->dwBlockLength) {
		*(stream->lpCurrentWord++) = word;
		stream->nWordsUsed++;
	}
	else
	{
		stream->error = BITSTREAM_ERROR_OVERFLOW;
	}
}
#endif

#if 0

// Write bits to a bitstream
void PutBits(BITSTREAM *stream, uint32_t  wBits, int nBits)
{
	uint8_t  wBuffer;
	int nBitsFree;
	int nBitsUsed;

	//_RPT2(_CRT_WARN, "PutBits(0x%X, %d)\n", wBits, nBits);

	// Can pass a null stream to indicate that the bits should be discarded
	if (stream == NULL) return;

	if (nBits == 0) return;

	//START(tk_putbits);

//#if _TIMING
	// Count the number of bits written to the bitstream
//	stream->cntBits += nBits;
//#endif

	// Force leading bits to be zero
	if (nBits < BITSTREAM_LONG_SIZE)
		wBits &= ((1 << nBits) - 1);

	// Get the current word
	wBuffer = stream->wBuffer;

	// Number of bits remaining in the current word
	nBitsFree = stream->nBitsFree;

	// Insert the current word into the buffer if it is full
	if (nBitsFree == 0) {
		PutWord(stream, wBuffer);
		assert(stream->error == BITSTREAM_ERROR_OKAY);
		nBitsFree = BITSTREAM_WORD_SIZE;
	}

	// Insert bits into the current word until the remaining bits fit in one word
	while (nBits >= nBitsFree)
	{
		// Fill the rest of the current word
		if (nBitsFree < BITSTREAM_WORD_SIZE) {
			wBuffer <<= nBitsFree;
		}

		// Reduce the number of bits left to insert into the buffer
		nBits -= nBitsFree;

		// Insert bits into the current buffer
		if (nBits > 0)
			wBuffer |= (wBits >> nBits) & BITMASK(nBitsFree);
		else
			wBuffer |= wBits & BITMASK(nBitsFree);

		// Insert the full word into the bitstream block
		PutWord(stream, wBuffer);
		assert(stream->error == BITSTREAM_ERROR_OKAY);

		// Reset the number of bits available in the current word
		nBitsFree = BITSTREAM_WORD_SIZE;
		wBuffer = 0;
	}

	// Check that there is room in the current buffer
	assert(nBits < nBitsFree);

	// Are there more bits to place into the current word?
	if (nBits > 0)
	{
		// Insert the bits into the current word
		wBuffer <<= nBits;
		wBuffer |= wBits & BITMASK(nBits);

		// Reduce the number of bits available
		nBitsFree -= nBits;
	}

	// Save the new current word and bit count in the stream
	nBitsUsed = BITSTREAM_WORD_SIZE - nBitsFree;
	stream->wBuffer = (uint8_t )(wBuffer & BITMASK(nBitsUsed));
	stream->nBitsFree = nBitsFree;

	//STOP(tk_putbits);
}

#else


#if _PROCESSOR_DISPATCH

__declspec(cpu_dispatch(Pentium_4,Generic))

void PutLong(BITSTREAM *stream, uint32_t  word)
{
	// Stub routine for processor specific dispatch
}
#endif


#if _PROCESSOR_GENERIC

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Generic))
#endif

// Insert a longword into the bitstream
void PutLong(BITSTREAM *stream, uint32_t  word)
{
	const int nWordsPerLong = sizeof(uint32_t )/sizeof(uint8_t );
	uint32_t  *lpCurrentWord = (uint32_t  *)(stream->lpCurrentWord);
	int nWordsUsed = stream->nWordsUsed + nWordsPerLong;

	// Check that there is room in the block for the int32_t word
	assert(nWordsUsed <= stream->dwBlockLength);
	if (nWordsUsed <= stream->dwBlockLength) {
		//*(lpCurrentWord++) = _bswap(word);
		*(lpCurrentWord++) = SwapInt32NtoB(word);
		stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
		stream->nWordsUsed = nWordsUsed;
	}
	else
	{
		stream->error = BITSTREAM_ERROR_OVERFLOW;
	}
}

#endif


#if _PROCESSOR_PENTIUM_4

#if _PROCESSOR_DISPATCH
__declspec(cpu_specific(Pentium_4))
#endif

// Insert a longword into the bitstream
void PutLong(BITSTREAM *stream, uint32_t  word)
{
	const int nWordsPerLong = sizeof(uint32_t )/sizeof(uint8_t );
	uint32_t  *lpCurrentWord = (uint32_t  *)(stream->lpCurrentWord);
	int nWordsUsed = stream->nWordsUsed + nWordsPerLong;

	// Check that there is room in the block for the int32_t word
	assert(nWordsUsed <= stream->dwBlockLength);
	if (nWordsUsed <= stream->dwBlockLength)
	{
#if 1
		//*(lpCurrentWord++) = _bswap(word);
		*(lpCurrentWord++) = SwapInt32NtoB(word);
#else
		//_mm_stream_si32(lpCurrentWord++, _bswap(word));
		_mm_stream_si32(lpCurrentWord++, SwapInt32NtoB(word));
#endif
		stream->lpCurrentWord = (uint8_t  *)lpCurrentWord;
		stream->nWordsUsed = nWordsUsed;
	}
	else
	{
		stream->error = BITSTREAM_ERROR_OVERFLOW;
	}
}

#endif


#if _BITSTREAM_BUFFER_BYTE

// Map the number of bits used to the amount of shift required to get the leading byte
static const unsigned char _shift[] =
{
	 0,  0,  0,  0,  0,  0,  0,  0,  0,
	 1,  2,  3,  4,  5,  6,  7,  8,  9,
	10, 11, 12, 13, 14, 15, 16, 17, 18,
	19, 20, 21, 22, 23, 24
};

// Write bits to a bitstream
void PutBits(BITSTREAM *stream, uint32_t  wBits, int nBits)
{
	uint32_t  wBuffer;
	int nBitsFree;
	int nBitsUsed;

#if 0
	// Can pass a null stream to indicate that the bits should be discarded
	if (stream == NULL) return;
#else
	// Change the conditional if the function might be passed a null stream
	assert(stream != NULL);
#endif

	// Should not call this routine with no bits to insert in the bitstream
	assert(nBits > 0);

	// Routine should not be passed a word with leading bits that are nonzero
	assert(nBits == BITSTREAM_LONG_SIZE || (wBits & ~BITMASK(nBits)) == 0);

	// Move the current word into a int32_t buffer
	wBuffer = (uint32_t )stream->wBuffer;
	assert((wBuffer & ~BITMASK(8)) == 0);

	// Number of bits remaining in the current word
	nBitsFree = BITSTREAM_LONG_SIZE - BITSTREAM_WORD_SIZE + stream->nBitsFree;

#if 0
	// Insert the current word into the buffer if it is full
	if (nBitsFree == 0) {
		PutWord(stream, wBuffer);
		nBitsFree = BITSTREAM_WORD_SIZE;
	}
#endif

	// Will the bits fit in the int32_t buffer?
	if (nBitsFree == BITSTREAM_LONG_SIZE) {
		wBuffer = wBits & BITMASK(nBits);
		nBitsFree -= nBits;
	}
	else if (nBits <= nBitsFree) {
		wBuffer <<= nBits;
		wBuffer |= (wBits & BITMASK(nBits));
		nBitsFree -= nBits;
	}
	else {
		// Fill the buffer with as many bits as will fit
		wBuffer <<= nBitsFree;
		nBits -= nBitsFree;

		// Should have some bits remaining
		assert(nBits > 0);

		// Insert as many bits as will fit into the buffer
		wBuffer |= (wBits >> nBits) & BITMASK(nBitsFree);

		// Insert all of the bytes in the buffer into the bitstream
		PutLong(stream, wBuffer);

		wBuffer = wBits & BITMASK(nBits);
		nBitsFree = BITSTREAM_LONG_SIZE - nBits;
	}

	// Reduce the number of bits in the buffer to less than one byte
	nBitsUsed = BITSTREAM_LONG_SIZE - nBitsFree;
	for (; nBitsUsed >= BITSTREAM_WORD_SIZE; nBitsUsed -= BITSTREAM_WORD_SIZE) {
		int shift = _shift[nBitsUsed];
		PutWord(stream, (wBuffer >> shift) & BITMASK(8));
	}

	// Save the new current word and bit count in the stream
	stream->wBuffer = (uint8_t )(wBuffer & BITMASK(nBitsUsed));
	stream->nBitsFree = BITSTREAM_WORD_SIZE - nBitsUsed;

	// Count the number of bits written to the bitstream
	//stream->cntBits += nBits;
}

#else

#if (1 && ASMOPT)

// Write bits to a bitstream (optimized with doubleword shift instructions)
void PutBits(BITSTREAM *stream, uint32_t  wBits, int nBits)
{
	// Check that the offsets to the bitstream fields have not been changed
	//assert(offsetof(BITSTREAM, nBitsFree) == 4);
	//assert(offsetof(BITSTREAM, lpCurrentWord) == 8);
	//assert(offsetof(BITSTREAM, nWordsUsed) == 12);
	//assert(offsetof(BITSTREAM, wBuffer) == 0x14);

	__asm
	{
		mov		ebx,dword ptr [wBits]			// Load the bit field to insert
		mov		ecx,dword ptr [nBits]			// Load the number of bits to insert
#if 0
		mov		edi,dword ptr [_bitmask+4*ecx]	// Load the bit mask
		and		ebx,edi							// Mask off the unused bits
#endif
		mov		edx,ecx							// Compute the number of unused bits
		neg		ecx								// Subtract the number of bits to insert
		add		ecx,20h							// from the number of bits per doubleword
		shl		ebx,cl							// Shift the new bits to the left end
		mov		ecx,edx							// Get the number of bits to insert

		mov		esi,dword ptr [stream]			// Base address of the stream struct
		mov		eax,dword ptr [esi+14h]			// Load the bitstream buffer
		mov		edx,dword ptr [esi+4]			// Load the buffer bit count

		cmp		ecx,edx							// Enough room in the buffer?
		jle		insert_bit_field				// Go insert the bits into the buffer

		mov		edi,dword ptr [esi+8]			// Load the bitstream pointer
		xchg	ecx,edx							// Use the buffer bit count as the shift
		shld	eax,ebx,cl						// Insert as many bits as will fit
		shl		ebx,cl							// Shift off the bits that were inserted
		bswap	eax								// Convert to big endian
//#if _PROCESSOR_PENTIUM_4
//		movnti	dword ptr [edi],eax	//not for P3/Althon			// Stream write the buffer to the bitstream
//#else
		mov		dword ptr [edi],eax				// Write the buffer to the bitstream
//#endif
		add		edi,4							// Increment the bitstream pointer
		sub		edx,ecx							// Subtract the number of bits inserted
		xchg	ecx,edx							// Get the number of bits to insert
		mov		dword ptr [esi+8],edi			// Save the bitstream pointer

		mov		edi,dword ptr [esi+12]			// Load the bitstream byte count
		add		edi,4							// Increment the byte count
		mov		dword ptr [esi+12],edi			// Save the updated byte count

		xor		eax,eax							// Clear the bitstream buffer
		mov		edx,20h							// Reset the buffer bit count

insert_bit_field:

		shld	eax,ebx,cl						// Shift the bits into the buffer
		sub		edx,ecx							// Reduce the free bit count by the insertion
		mov		dword ptr [esi+14h],eax			// Save the updated bitstream buffer
		mov		dword ptr [esi+4],edx			// Save the number of free bits in the buffer

		//sfence								// Serialize memory accesses
	}

#if (1 && DEBUG)
	stream->cntBits += nBits;
#endif
}

#else

// Write bits to a bitstream
void PutBits(BITSTREAM *stream, uint32_t wBits, int nBits)
{
	uint32_t  wBuffer;
	int nBitsFree;
	//int nBitsUsed;

#if 0
	// Can pass a null stream to indicate that the bits should be discarded
	if (stream == NULL) return;
#else
	// Change the conditional if the function might be passed a null stream
	assert(stream != NULL);
#endif

	// Should not call this routine with no bits to insert in the bitstream
	assert(nBits > 0);

	// Routine should not be passed a word with leading bits that are nonzero
	assert(nBits == BITSTREAM_LONG_SIZE || (wBits & ~BITMASK(nBits)) == 0);

	// Move the current word into a int32_t buffer
	wBuffer = stream->wBuffer;

	// Number of bits remaining in the current word
	nBitsFree = stream->nBitsFree;

	// Will the bits fit in the int32_t buffer?
	if (nBitsFree == BITSTREAM_LONG_SIZE) {
		wBuffer = wBits & BITMASK(nBits);
		nBitsFree -= nBits;
#if (1 && TRACE_PUTBITS)
		TracePutBits(nBits);
#endif
	}
	else if (nBits <= nBitsFree) {
		wBuffer <<= nBits;
		wBuffer |= (wBits & BITMASK(nBits));
		nBitsFree -= nBits;
#if (1 && TRACE_PUTBITS)
		TracePutBits(nBits);
#endif
	}
	else {
		// Fill the buffer with as many bits as will fit
		wBuffer <<= nBitsFree;
		nBits -= nBitsFree;

		// Should have some bits remaining
		assert(nBits > 0);

		// Insert as many bits as will fit into the buffer
		wBuffer |= (wBits >> nBits) & BITMASK(nBitsFree);

#if (1 && TRACE_PUTBITS)
		TracePutBits(nBitsFree);
#endif
		// Insert all of the bytes in the buffer into the bitstream
		PutLong(stream, wBuffer);

		wBuffer = wBits & BITMASK(nBits);
		nBitsFree = BITSTREAM_LONG_SIZE - nBits;
	}

#if 0
	// Reduce the number of bits in the buffer to less than one byte
	nBitsUsed = BITSTREAM_LONG_SIZE - nBitsFree;
	for (; nBitsUsed >= BITSTREAM_WORD_SIZE; nBitsUsed -= BITSTREAM_WORD_SIZE) {
		int shift = _shift[nBitsUsed];
		PutWord(stream, (wBuffer >> shift) & BITMASK(8));
	}
#endif

	// Save the new current word and bit count in the stream
	stream->wBuffer = wBuffer;
	stream->nBitsFree = nBitsFree;

	// Count the number of bits written to the bitstream
	//stream->cntBits += nBits;
}

#endif
#endif
#endif


// Output a tagged value
void PutTagPair(BITSTREAM *stream, int tag, int value)
{
	// The bitstream should be aligned on a tag word boundary
	assert(IsAlignedTag(stream));

	// The value must fit within a tag word
	assert(((uint32_t)value & ~(uint32_t)CODEC_TAG_MASK) == 0);

	PutLong(stream, ((uint32_t )tag << 16) | (value & CODEC_TAG_MASK));
}

// Output an optional tagged value
void PutTagPairOptional(BITSTREAM *stream, int tag, int value)
{
	// The bitstream should be aligned on a tag word boundary
	assert(IsAlignedTag(stream));

	// The value must fit within a tag word
#if DEBUG
	assert(((uint32_t)value & ~(uint32_t)CODEC_TAG_MASK) == 0);
#endif

	// Set the optional tag bit
	//tag |= CODEC_TAG_OPTIONAL;
	tag = NEG(tag);

	PutLong(stream, ((uint32_t )tag << 16) | (value & CODEC_TAG_MASK));
}

// Possibly a more efficient call for outputting a tagged value
void PutTagValue(BITSTREAM *stream, TAGVALUE segment)
{
	// The bitstream should be aligned on a tag word boundary
	assert(IsAlignedTag(stream));

	// Output the segment
	PutLong(stream, segment.longword);
}

// Output a tag that marks a place in the bitstream for debugging
void PutTagMarker(BITSTREAM *stream, uint32_t  marker, int size)
{
	// The marker must fit within the tag value
	assert(0 < size && size <= 16);

	// Output a tag and marker value for debugging
	PutTagPair(stream, CODEC_TAG_MARKER, marker);
}


// Write a 16-bit word to the bitstream
void PutWord16s(BITSTREAM *stream, int value)
{
	const int nWordsPerValue = sizeof(PIXEL16S)/sizeof(uint8_t );
	uint8_t  *lpCurrentWord = stream->lpCurrentWord;
	int nWordsUsed = stream->nWordsUsed + nWordsPerValue;

	// This routine assumes that the buffer is empty
	assert(stream->nBitsFree == BITSTREAM_LONG_SIZE);

	// Check that there is room in the block for the 16-bit value
	assert(nWordsUsed <= stream->dwBlockLength);
	if (nWordsUsed <= stream->dwBlockLength)
	{
		// Write the two halves in big endian order
		*(lpCurrentWord++) = (value >> BITSTREAM_WORD_SIZE) & BITSTREAM_WORD_MASK;
		*(lpCurrentWord++) = value & BITSTREAM_WORD_MASK;

		// Update the bitstream pointer and word count
		stream->lpCurrentWord = lpCurrentWord;
		stream->nWordsUsed = nWordsUsed;
	}
	else
	{
		stream->error = BITSTREAM_ERROR_OVERFLOW;
	}
}

// Force 32 bits to be written to a bitstream buffer
void WriteLong(BITSTREAM *stream, uint32_t  wBits, int nBits)
{
	uint32_t  wBuffer;
	int nBitsFree;
	//int nBitsUsed;

#if 0
	// Can pass a null stream to indicate that the bits should be discarded
	if (stream == NULL) return;
#else
	// Change the conditional if the function might be passed a null stream
	assert(stream != NULL);
#endif

	// Should ONLY call this routine when 32 bits are to be written out
	assert(nBits == BITSTREAM_LONG_SIZE);

	// Move the current word into a int32_t buffer
	wBuffer = stream->wBuffer;

	// Number of bits remaining in the current word
	nBitsFree = stream->nBitsFree;

	// Should ONLY call this routine when wBuffer is either full or empty
	assert(nBitsFree == 0 || nBitsFree == BITSTREAM_LONG_SIZE);

	// If wBuffer is full, write out its content first
	if(nBitsFree == 0) {
		PutLong(stream, wBuffer);
		nBitsFree = BITSTREAM_LONG_SIZE;
	}

	// Force the write-out of the new value to the bitstream
	PutLong(stream, wBits);

	// Save the new current word and bit count in the stream
	stream->nBitsFree = nBitsFree;

	// Count the number of bits written to the bitstream
	//stream->cntBits += nBits;
}

// Pad the bitstream with zeros up to the next bitword (byte) boundary
void PadBits(BITSTREAM *stream)
{
#if _BITSTREAM_BUFFER_BYTE
	if (0 < stream->nBitsFree && stream->nBitsFree < BITSTREAM_WORD_SIZE)
		PutBits(stream, 0, stream->nBitsFree);
#else
	int nLastWordBits = (BITSTREAM_LONG_SIZE - stream->nBitsFree) % BITSTREAM_WORD_SIZE;
	assert(0 <= nLastWordBits && nLastWordBits < BITSTREAM_WORD_SIZE);
	if (nLastWordBits > 0)
		PutBits(stream, 0, (BITSTREAM_WORD_SIZE - nLastWordBits));
#endif

	// Check that the bitstream buffer contains an integral number of words
	assert((stream->nBitsFree % BITSTREAM_WORD_SIZE) == 0);
}

// Pad the bitstream with zeros up to the next bitword (doubleword) boundary
void PadBits32(BITSTREAM *stream)
{
#if _BITSTREAM_BUFFER_BYTE
	assert(0);
#else
	int nLastWordBits = (BITSTREAM_LONG_SIZE - stream->nBitsFree) % BITSTREAM_LONG_SIZE;
	assert(0 <= nLastWordBits && nLastWordBits < BITSTREAM_LONG_SIZE);
	if (nLastWordBits > 0)
		PutBits(stream, 0, (BITSTREAM_LONG_SIZE - nLastWordBits));
#endif

	// Check that the bitstream buffer contains an integral number of words
	assert((stream->nBitsFree % BITSTREAM_WORD_SIZE) == 0);
}

// Pad the bitstream to a tag boundary and flush the bit field buffer
void PadBitsTag(BITSTREAM *stream)
{
	PadBits32(stream);
	FlushStream(stream);
}

// Align the bitstream to the next word boundary
void AlignBits(BITSTREAM *stream)
{
#if _BITSTREAM_BUFFER_BYTE
	if (0 < stream->nBitsFree && stream->nBitsFree < BITSTREAM_WORD_SIZE) {
		int nBitsUsed = BITSTREAM_WORD_SIZE - stream->nBitsFree;
		SkipBits(stream, nBitsUsed);
	}
#else
	int nBitsUsed = (BITSTREAM_LONG_SIZE - stream->nBitsFree) % BITSTREAM_WORD_SIZE;
	assert(0 <= nBitsUsed && nBitsUsed < BITSTREAM_WORD_SIZE);
	if (nBitsUsed > 0)
		SkipBits(stream, nBitsUsed);
#endif

	// Check that the bitstream buffer contains an integral number of words
	assert((stream->nBitsFree % BITSTREAM_WORD_SIZE) == 0);
}

#if !_BITSTREAM_UNALIGNED

// Align the bitstream to the beginning of a tag value pair
void AlignBitsTag(BITSTREAM *stream)
{
	// Compute the number of words in the buffer
	int nBitsUsed = BITSTREAM_LONG_SIZE - stream->nBitsFree;
	int nWordsInBuffer = nBitsUsed / BITSTREAM_WORD_SIZE;

	// Compute the number of words read from the current int32_t word
	int nWordsInStream = (DWORD)stream->lpCurrentWord % BITSTREAM_WORDS_PER_LONG;

	// Get the current bitstream pointer
	uint8_t  *lpCurrentWord = stream->lpCurrentWord;
	int nWordsUsed = stream->nWordsUsed;

	assert(0 <= nWordsInBuffer && nWordsInBuffer <= BITSTREAM_WORDS_PER_LONG);
	assert(0 <= nWordsInBuffer && nWordsInStream <= BITSTREAM_WORDS_PER_LONG);

	// Can the buffer words be pushed back into the bitstream?
	if (nWordsInBuffer >= nWordsInStream)
	{
		// Back up to the beginning of the current int32_t word
		int nWordsBack = ((DWORD)lpCurrentWord & BITSTREAM_LONG_MASK);

		lpCurrentWord = (uint8_t  *)((DWORD)lpCurrentWord & ~BITSTREAM_LONG_MASK);
		nWordsUsed -= nWordsBack;

	}
	else	// Must skip ahead to the next int32_t word boundary
	{
		// Skip ahead to the next int32_t word boundary
		lpCurrentWord = (uint8_t  *)ALIGN(lpCurrentWord, sizeof(uint32_t ));
		nWordsUsed = ALIGN(nWordsUsed, BITSTREAM_WORDS_PER_LONG);
	}

	// Check that the bitstream is int32_t word aligned
	assert(((DWORD)lpCurrentWord & BITSTREAM_LONG_MASK) == 0);
	assert(((DWORD)nWordsUsed & BITSTREAM_LONG_MASK) == 0);

	// Update the bitstream pointer
	stream->lpCurrentWord = lpCurrentWord;
	stream->nWordsUsed = nWordsUsed;

	// Discard the contents of the bitstream buffer
	stream->wBuffer = 0;
	stream->nBitsFree = BITSTREAM_LONG_SIZE;
}

#else

// Align the bitstream to the beginning of a tag value pair
void AlignBitsTag(BITSTREAM *stream)
{
	// Compute the number of words in the buffer
	int nBitsUsed = BITSTREAM_LONG_SIZE - stream->nBitsFree;
	int nWordsInBuffer = nBitsUsed / BITSTREAM_WORD_SIZE;

	// Get the offset of the bitstream within the sample
	int offset = stream->alignment;

	// Compute the number of words read from the current int32_t word
	int nWordsInStream = ((uintptr_t)stream->lpCurrentWord - offset) % BITSTREAM_WORDS_PER_LONG;

	// Get the current bitstream pointer
	uint8_t  *lpCurrentWord = stream->lpCurrentWord;
	int nWordsUsed = stream->nWordsUsed;

	assert(0 <= nWordsInBuffer && nWordsInBuffer <= BITSTREAM_WORDS_PER_LONG);
	assert(0 <= nWordsInBuffer && nWordsInStream <= BITSTREAM_WORDS_PER_LONG);

	// Can the buffer words be pushed back into the bitstream?
	if (nWordsInBuffer >= nWordsInStream)
	{
		// Back up to the beginning of the current int32_t word
		int nWordsBack = (((uintptr_t)lpCurrentWord - offset) & BITSTREAM_LONG_MASK);

		// Subtract the offset from the pointer into the stream
		uintptr_t dwCurrentWord = (uintptr_t)lpCurrentWord - offset;

		lpCurrentWord = (uint8_t  *)((dwCurrentWord & ~BITSTREAM_LONG_MASK) + offset);
		nWordsUsed -= nWordsBack;
	}
	else	// Must skip ahead to the next int32_t word boundary
	{
		// Skip ahead to the next int32_t word boundary
		//if (((DWORD)lpCurrentWord % offset) != 0)
		{
			// Subtract the offset from the pointer into the stream
			uintptr_t dwCurrentWord = (uintptr_t)lpCurrentWord - offset;
			int nWordsSkipped;

			// Align the pointer to the current word to a four byte boundary
			lpCurrentWord = (uint8_t  *)ALIGN(dwCurrentWord, sizeof(uint32_t ));

			// Adjust the number of words in the stream
			nWordsSkipped = (int)((uintptr_t)lpCurrentWord - dwCurrentWord);
			assert(nWordsSkipped >= 0);
			nWordsUsed -= nWordsSkipped; //DAN20070817 -- switched += to -= to fix miss align source bits.

			// Add the offset onto the pointer into the stream
			lpCurrentWord += offset;
		}
	}

	// Check that the bitstream is long word aligned
	assert(((uintptr_t)lpCurrentWord & BITSTREAM_LONG_MASK) == (unsigned)offset);
	assert(((uintptr_t)nWordsUsed & BITSTREAM_LONG_MASK) == 0);

	// Update the bitstream pointer
	stream->lpCurrentWord = lpCurrentWord;
	stream->nWordsUsed = nWordsUsed;

	// Discard the contents of the bitstream buffer
	stream->wBuffer = 0;
	stream->nBitsFree = BITSTREAM_LONG_SIZE;
}

#endif

// Align the bitstream to the next int32_t word boundary
void AlignBitsLong(BITSTREAM *stream)
{
	int nBitsUsed = (BITSTREAM_LONG_SIZE - stream->nBitsFree);
	assert(0 <= nBitsUsed && nBitsUsed < BITSTREAM_LONG_SIZE);
	if (nBitsUsed > 0)
		SkipBits(stream, nBitsUsed);

	// Check that the bitstream buffer contains an integral number of longwords
	assert((stream->nBitsFree % BITSTREAM_LONG_SIZE) == 0);
}

// Check that the bitstream is aligned on a word boundary
bool IsAlignedBits(BITSTREAM *stream)
{
	return ((stream->nBitsFree % BITSTREAM_WORD_SIZE) == 0);
}

// Check that the bitstream is aligned to a tag word boundary
bool IsAlignedTag(BITSTREAM *stream)
{
	return ((stream->nBitsFree % BITSTREAM_TAG_SIZE) == 0);
}

// Set the current bitstream position to have the specified alignment
void SetBitstreamAlignment(BITSTREAM *stream, int alignment)
{
	// Get the current bitstream pointer
	uint8_t  *lpCurrentWord = stream->lpCurrentWord;
	int offset;

	// Check that the bitstream buffer is empty
	assert(stream->nBitsFree == BITSTREAM_BUFFER_SIZE);

	// Get the current offset into the bitstream
	offset = ((uintptr_t)lpCurrentWord & BITSTREAM_LONG_MASK);

	// Add in the alignment
	offset = ((offset - alignment) % sizeof(uint32_t ));
	assert(offset >= 0);

	// Save the bitstream alignment
	stream->alignment = offset;
}

// Look at words in the bitstream without changing the position within the stream
uint8_t  PeekWord(BITSTREAM *stream, int index)
{
	return stream->lpCurrentWord[index - 1];
}

// Look at the next longword in the bitstream without changing the position within the stream
uint32_t  PeekLong(BITSTREAM *stream)
{
	const int nWordsPerLong = sizeof(uint32_t )/sizeof(uint8_t );
	int nWordsUsed = stream->nWordsUsed - nWordsPerLong;
	uint32_t  *lpCurrentWord = (uint32_t  *)(stream->lpCurrentWord);
	uint32_t  longword = 0x0C0C0C0C;

	// This routine assumes that the buffer is empty
	assert(stream->nBitsFree == BITSTREAM_LONG_SIZE);

	// Is there a longword in the block?
	assert(nWordsUsed >= 0);
	if (nWordsUsed >= 0)
	{
		// Get the int32_t word from the bitstream block
		longword = *(lpCurrentWord++);

		// Byte swap the int32_t word into little endian order
		//longword = _bswap(longword);
		longword = SwapInt32BtoN(longword);
	}
	else {
		// Signal that a bitstream error has occurred
		stream->error = BITSTREAM_ERROR_UNDERFLOW;
	}

	return longword;
}

// Skip the next longword in the bitstream
void SkipLong(BITSTREAM *stream)
{
	uint32_t  *lpCurrentLong = (uint32_t  *)(stream->lpCurrentWord);
	stream->lpCurrentWord = (uint8_t  *)(lpCurrentLong + 1);
}

int BitstreamSize(BITSTREAM *stream)
{
	int nBytesInBuffer;

	// This subroutine should only be called when the bitstream is aligned on a byte boundary
	assert((stream->nBitsFree % BITSTREAM_WORD_SIZE) == 0);

	// Compute the number of bytes in the bitstream buffer
	nBytesInBuffer = (BITSTREAM_LONG_SIZE - stream->nBitsFree)/BITSTREAM_WORD_SIZE;

	// Return the total number of bytes encoded into the bitstream
	return (stream->nWordsUsed + nBytesInBuffer);
}

static const unsigned char _lmo[] =
{
	0,	1,	2,	2,	3,	3,	3,	3,
	4,	4,	4,	4,	4,	4,	4,	4,
	5,	5,	5,	5,	5,	5,	5,	5,
	5,	5,	5,	5,	5,	5,	5,	5,
	6,	6,	6,	6,	6,	6,	6,	6,
	6,	6,	6,	6,	6,	6,	6,	6,
	6,	6,	6,	6,	6,	6,	6,	6,
	6,	6,	6,	6,	6,	6,	6,	6,
	7,	7,	7,	7,	7,	7,	7,	7,
	7,	7,	7,	7,	7,	7,	7,	7,
	7,	7,	7,	7,	7,	7,	7,	7,
	7,	7,	7,	7,	7,	7,	7,	7,
	7,	7,	7,	7,	7,	7,	7,	7,
	7,	7,	7,	7,	7,	7,	7,	7,
	7,	7,	7,	7,	7,	7,	7,	7,
	7,	7,	7,	7,	7,	7,	7,	7,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8,
	8,	8,	8,	8,	8,	8,	8,	8
};

// Compute number of bits required to represent a positive number
int LeftMostOne(unsigned int word)
{
	int lmo = 0;

#if 0
	while (word > 0) {
		lmo++;
		word >>= 1;
	}
#else
	if (word > (1 << 15)) {
		if (word > (1 << 23)) {
			lmo = 24 + _lmo[word >> 24];
		}
		else {
			lmo = 16 + _lmo[word >> 16];
		}
	}
	else if (word > (1 << 7)) {
		lmo = 8 + _lmo[word >> 8];
	}
	else {
		lmo = _lmo[word];
	}
#endif

	return lmo;
}

#if 0
 #if _ALLOCATOR

BITSTREAM *CreateBitstream(ALLOCATOR *allocator, const char *filename, uint32_t access)
{
	BITSTREAM *stream = (BITSTREAM *)Alloc(allocator, sizeof(BITSTREAM));
	if (stream == NULL) return NULL;

	InitBitstream(stream);
	OpenBitstream(stream, filename, access);

	return stream;
}
 #else

BITSTREAM *CreateBitstream(const char *filename, int access)
{
	BITSTREAM *stream = (BITSTREAM *)MEMORY_ALLOC(sizeof(BITSTREAM));
	if (stream == NULL)
	{
		return NULL;
	}

	InitBitstream(stream);
	OpenBitstream(stream, filename, access);

	return stream;
}

 #endif
#endif

#if 0
void DeleteBitstream(BITSTREAM *stream)
{
	if (stream != NULL) {
		if (stream->file != BITSTREAM_FILE_INVALID)
			CloseBitstream(stream);


#if _ALLOCATOR
	Free(allocator, stream);
#else
	MEMORY_FREE(stream);
#endif
	}
}
#endif

#if 0
void OpenBitstream(BITSTREAM *stream, const char *filename, uint32_t access)
{
#if _POSIX

	assert(stream != NULL);
	if (stream != NULL)
	{
		// Initialize the bitstream
		InitBitstream(stream);

		// Open the file for binary stream
		switch (access)
		{
		case BITSTREAM_ACCESS_WRITE:
			stream->file = fopen(filename, "wb");
			break;

		case BITSTREAM_ACCESS_READ:
			stream->file = fopen(filename, "rb");
			break;

		default:
			assert(0);
			stream->error = BITSTREAM_ERROR_ACCESS;
			break;
		}

		// Need to remember how the stream is accessed
		stream->access = access;

		// Set the size of the buffer for reading and writing
		stream->dwBlockLength = BITSTREAM_BLOCK_LENGTH;
	}

#else

	assert(stream != NULL);
	if (stream != NULL)
	{
		// Initialize the bitstream
		InitBitstream(stream);

		// Open the file for binary stream
		switch (access)
		{
		case BITSTREAM_ACCESS_WRITE:
			stream->file = CreateFile(filename, GENERIC_WRITE, 0, NULL,
									  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			break;

		case BITSTREAM_ACCESS_READ:
			stream->file = CreateFile(filename, GENERIC_READ, 0, NULL,
									  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			break;

		default:
			stream->error = BITSTREAM_ERROR_ACCESS;
			assert(0);
			break;
		}

		// Need to remember how the stream is accessed
		stream->access = access;

		// Set the size of the buffer for reading and writing
		stream->dwBlockLength = BITSTREAM_BLOCK_LENGTH;
	}

#endif
}
#endif

#if 0
void CloseBitstream(BITSTREAM *stream)
{
#if _POSIX

	assert(stream != NULL);
	if (stream != NULL)
	{
		// Write any bits in the current word to the output buffer
		FlushStream(stream);

		if (stream->file != BITSTREAM_FILE_INVALID)
		{
			size_t byte_count = stream->nWordsUsed * sizeof(uint8_t );

			// Write the block to the output file
			fwrite(stream->block, byte_count, 1, stream->file);

			// Close the file
			fclose(stream->file);

			// Indicate that the file is closed
			stream->file = BITSTREAM_FILE_INVALID;
			stream->access = BITSTREAM_ACCESS_NONE;
			stream->dwBlockLength = 0;
		}
	}

#else

	assert(stream != NULL);
	if (stream != NULL)
	{
		// Write any bits in the current word to the output buffer
		FlushStream(stream);

		if (stream->file != BITSTREAM_FILE_INVALID)
		{
			DWORD nBytesToWrite = stream->nWordsUsed * sizeof(uint8_t );
			DWORD nBytesWritten;
#ifdef _WIN32
			// Write the block to the output file
			WriteFile(stream->file, stream->block, nBytesToWrite, &nBytesWritten, NULL);
			assert(nBytesToWrite == nBytesWritten);
#else
			// Write the block to the output file
			size_t write_count = fwrite(stream->block, nBytesToWrite, 1, stream->file);
			assert(write_count > 0);
#endif
			// Close the file
			CloseHandle(stream->file);

			// Indicate that the file is closed
			stream->file = BITSTREAM_FILE_INVALID;
			stream->access = BITSTREAM_ACCESS_NONE;
			stream->dwBlockLength = 0;
		}
	}

#endif
}
#endif

#if 0
#if _POSIX && 0
#warning Posix support not implemented
#else

void AttachBitstreamFile(BITSTREAM *stream, HANDLE file, uint32_t access)
{
	// Stream must exist and not have a file already attached
	assert(stream != NULL && stream->file == BITSTREAM_FILE_INVALID);
	if (stream != NULL && stream->file == BITSTREAM_FILE_INVALID) {
		stream->file = file;
		stream->access = access;
	}
}

#endif
#endif

#if 0
void DetachBitstreamFile(BITSTREAM *stream)
{
#if _POSIX && 0
#warning Posix support not implemented
#else

	// Stream must exist
	assert(stream != NULL);
	if (stream != NULL)
	{
		// Write any bits in the current word to the output buffer
		FlushStream(stream);

		if (stream->file != BITSTREAM_FILE_INVALID &&
			stream->access == BITSTREAM_ACCESS_WRITE)
		{
			DWORD nBytesToWrite = stream->nWordsUsed * sizeof(uint8_t );
			DWORD nBytesWritten;
#ifdef _WIN32
			// Write the block to the output file
			WriteFile(stream->file, stream->block, nBytesToWrite, &nBytesWritten, NULL);
			assert(nBytesToWrite == nBytesWritten);
#else
			// Write the block to the output file
			size_t write_count = fwrite(stream->block, nBytesToWrite, 1, stream->file);
			assert(write_count > 0);
#endif
			// Do not close the file

			// Indicate that the stream no longer has an attached file
			stream->file = BITSTREAM_FILE_INVALID;
			stream->access = BITSTREAM_ACCESS_NONE;
		}
	}
#endif
}
#endif

void SetBitstreamBuffer(BITSTREAM *stream, uint8_t  *buffer, size_t length, uint32_t access)
{
	stream->lpCurrentBuffer = buffer;
	stream->lpCurrentWord = buffer;
	stream->dwBlockLength = (int32_t)(length/sizeof(uint8_t));
	stream->access = access;
	stream->nWordsUsed = (access == BITSTREAM_ACCESS_READ) ? stream->dwBlockLength : 0;
	stream->nBitsFree = BITSTREAM_BUFFER_SIZE;
	stream->wBuffer = 0;
}

void ClearBitstream(BITSTREAM *stream)
{
	if (stream != NULL)
	{
		// Anything to do?
	}
}

size_t BitstreamByteCount(BITSTREAM *stream)
{
	size_t byte_count;

	if (stream->access == BITSTREAM_ACCESS_READ) {
		byte_count = (stream->dwBlockLength - stream->nWordsUsed) * sizeof(uint8_t );
	}
	else {
		byte_count = stream->nWordsUsed * sizeof(uint8_t );
	}

	return byte_count;
}

void CopyBitstream(BITSTREAM *source, BITSTREAM *target)
{
	uint8_t *source_buffer;
	uint8_t *target_buffer;
	uint32_t buffer_size;

	// Check that the bitstream buffers are empty
	assert(source->nBitsFree == BITSTREAM_BUFFER_SIZE);
	assert(target->nBitsFree == BITSTREAM_BUFFER_SIZE);

	// Get the memory pointers into the source and target bitstreams
	source_buffer = source->lpCurrentBuffer;
	target_buffer = target->lpCurrentWord;

	// Get the size of the memory block to copy
	buffer_size = source->nWordsUsed * sizeof(uint8_t);

	// Copy the source memory block to the target bitstream
	memcpy(target_buffer, source_buffer, buffer_size);

	// Advance the target memory pointer and memory size
	target->lpCurrentWord += buffer_size/sizeof(uint8_t);
	target->nWordsUsed += (int)(buffer_size/sizeof(uint8_t));

	// Check for bitstream block overflow
	assert(target->nWordsUsed <= target->dwBlockLength);
}

#if _AVIFILES

// Routines for associating the bitstream with an AVI file
void AttachBitstreamAVI(BITSTREAM *stream, PAVISTREAM pavi, DWORD access)
{
	// Check that the stream is not already attached to an AVI stream
	assert(stream->pavi == BITSTREAM_AVI_INVALID);

	// Check that the stream is not attached to a file
	assert(stream->file == BITSTREAM_FILE_INVALID);

	// Check that the requested access is appropriate for an AVI stream
	assert(access == BITSTREAM_AVI_READ || access == BITSTREAM_AVI_WRITE);

	stream->pavi = pavi;
	stream->access = access;

	// Start reading the first sample from the AVI stream
	stream->sample = 0;
}

void DetachBitstreamAVI(BITSTREAM *stream)
{
	// Check that no AVI access uses the same code as no file access
	assert(BITSTREAM_AVI_NONE == BITSTREAM_ACCESS_NONE);

	stream->pavi = NULL;
	stream->access = BITSTREAM_ACCESS_NONE;

	// Let the caller close the AVI stream
}

#endif


static const char *tag_string_table[] =
{
	"Unused",

	"Type of sample",				// Tags for encoding video samples
	"Sample index table",
	"Sample index entry",
	"Bitstream marker",				// Used for debugging

	"Major version number",			// Tags for encoding the video sequence header
	"Minor version number",
	"Revision number",
	"Edit number",
	"Video sequence flags",
	"Transform type",				// Tags for encoding a wavelet group
	"Length of group of frames",
	"Number of transform channels",
	"Number of transform wavelets",
	"Number of encoded subbands",
	"Number of spatial levels",
	"Type of first wavelet",
	"Number of bytes per channel",
	"Group trailer",
	"Frame type",					// Tags for encoding a wavelet frame
	"Frame width",
	"Frame height",
	"Pixel format",
	"Index of frame in group",
	"Frame trailer",
	"Lowpass subband number",		// Tags for encoding the lowpass wavelet bands
	"Number of wavelet levels",
	"Width of the lowpass band",
	"Height of the lowpass band",
	"Top margin",
	"Bottom margin",
	"Left margin",
	"Right margin",
	"Pixel offset",
	"Quantization",
	"Bits per pixel",
	"Lowpass trailer",
	"Type of wavelet",				// Tags for encoding the highpass wavelet bands
	"Wavelet index transform array",
	"Wavelet level",
	"Number of wavelet bands",
	"Width of each highpass band",
	"Height of each highpass band",
	"Lowpass border dimensions",
	"Highpass border dimensions",
	"Scale factor",
	"Divisor",
	"Highpass trailer",
	"Wavelet band number",			// Tags for encoding the highpass band
	"Band width",
	"Band height",
	"Subband number",
	"Encoding method",
	"Band quantization",
	"Band scale factor",
	"Band divisor",
	"Band trailer",
	"Zero values",					// Tags for encoding zerotrees
	"Zero trees",
	"Positive values",
	"Negative values",
	"Zero nodes",
	"Channel number",				// Tags for encoding the color channel header

	"Interlaced flags",				// Optional tags in the video group extension
	"Copy protection flags",
	"Picture aspect ratio x",
	"Picture aspect ratio y",

	"Sample end"					// Denotes end of sample (for debugging only)
};

// Dump bitstream tags and values
void DumpBitstreamTags(BITSTREAM *stream, int count, FILE *logfile)
{
	const int nWordsPerTag = sizeof(uint32_t)/sizeof(uint8_t );
	const int max_tag_count = count;
	int tag_count = stream->nWordsUsed / nWordsPerTag;
	//uint32_t *tag_ptr = (uint32_t*)stream->lpCurrentBuffer;

	// Check for an invalid tag string table
	assert(sizeof(tag_string_table)/sizeof(tag_string_table[0]) == CODEC_TAG_COUNT);

	if (tag_count > max_tag_count)
		tag_count = max_tag_count;

	for (; tag_count > 0; tag_count--)
	{
#if 1
		TAGVALUE segment = GetTagValueAny(stream);
		TAGWORD tag = segment.tuple.tag;
		TAGWORD value = segment.tuple.value;
		bool optional = false;

		// Is this an optional tag?
		if (tag < 0) {
			tag = NEG(tag);
			optional = true;
		}

		if (0 <= tag && tag < CODEC_TAG_COUNT)
		{
			char *type = (optional ? "opt" : "req");

			if (tag == CODEC_TAG_MARKER)
			{
				fprintf(logfile, "%s (%d): 0x%04X %s\n", tag_string_table[tag], tag, value, type);
			}
			else if (tag == CODEC_TAG_INDEX)
			{
				int i;

				fprintf(logfile, "%s (%d): %d %s\n", tag_string_table[tag], tag, value, type);

				// Print out the index entries
				for (i = 0; i < value; i++)
				{
					uint32_t entry = GetLong(stream);
					fprintf(logfile, "Index entry %d: %d (0x%X)\n", i, entry, entry);
				}
			}
			else
			{
				if (tag == CODEC_TAG_WAVELET_TYPE) fprintf(logfile, "\n");

				fprintf(logfile, "%s (%d): %d %s\n", tag_string_table[tag], tag, value, type);
			}
		}
		else
			fprintf(logfile, "Unknown tag: 0x%04X\n", tag);

		// Check for the end of valid tags in the sample
		if (tag == CODEC_TAG_SAMPLE_END) break;
#else
		DWORD segment;
		TAGWORD tag;
		TAGWORD value;

		// Get the doubleword segment (tag and value) from the bitstream
		segment = *(tag_ptr++);

		// Need to swap the bytes into little endian order
		//segment = _bswap(segment);
		segment = SwapInt32BtoN(segment);

		// Get the tag and value
		tag = segment >> 16;
		value = (short)segment;

		//fprintf(logfile, "[tag: %s (%d): value: %d]\n", tag_string_table[tag], tag, value);
		//fprintf(logfile, "[%s (%d): %d]\n", tag_string_table[tag], tag, value);
		fprintf(logfile, "%s (%d): %d\n", tag_string_table[tag], tag, value);
#endif
	}
}


//DAN20090805 Upgraded to not use addressing math for reliable 64-bit encoding support.
void SizeTagPush(BITSTREAM *stream, int tag)
{
	int i;
	if(stream->ChunkSizeOffset[0])
	{
		for(i=NESTING_LEVELS-1;i>0;i--)
		{
			stream->ChunkSizeOffset[i] = stream->ChunkSizeOffset[i-1];
		}
	}
	stream->ChunkSizeOffset[0] = stream->nWordsUsed;
	PutTagPair(stream, tag, 0);
}

void SizeTagPop(BITSTREAM *stream)
{
	if(stream->ChunkSizeOffset[0] && (int)stream->ChunkSizeOffset[0] < (int)stream->nWordsUsed)
	{
		unsigned char *base = stream->lpCurrentBuffer;
		short tag = (base[stream->ChunkSizeOffset[0]]<<8) | (base[stream->ChunkSizeOffset[0]+1]);
		int i,size = stream->nWordsUsed - stream->ChunkSizeOffset[0];
		if(size>=4)
		{
			size >>= 2;
			size -= 1; // resize the size of the tag|value
		}
		else
		{
			size = 0;
		}

		if(tag & 0x2000) // 24bit chunks
		{
			if(size > 0xffffff)
			{
/*				tag |= CODEC_TAG_HUGE_FLAG; // larger chunks


				// add NOPs here
//				...

				size >>= 6;
				tag |= (size >> 16);
				size &= 0xffff;*/
			}
			else
			{
				tag |= (size >> 16);
				size &= 0xffff;
			}
		}
		else // 16bit chunks
		{
			size &= 0xffff;
		}

		tag = OPTIONALTAG(tag);

		base[stream->ChunkSizeOffset[0]+0] = (tag >> 8) & 0xff;
		base[stream->ChunkSizeOffset[0]+1] = tag & 0xff;
		base[stream->ChunkSizeOffset[0]+2] = (size >> 8) & 0xff;
		base[stream->ChunkSizeOffset[0]+3] = size & 0xff;

		for(i=0;i<NESTING_LEVELS-1;i++)
		{
			stream->ChunkSizeOffset[i] = stream->ChunkSizeOffset[i+1];
		}
		stream->ChunkSizeOffset[NESTING_LEVELS-1] = 0;
	}
	else
	{
		stream->ChunkSizeOffset[0] = 0;
	}
}


#if TRACE_PUTBITS

static FILE *tracefile = NULL;

void OpenTraceFile()
{
	if (tracefile == NULL) {
		tracefile = fopen("putbits.log", "w");
	}
	assert(tracefile != NULL);
}

void CloseTraceFile()
{
	if (tracefile != NULL) {
		fclose(tracefile);
		tracefile = NULL;
	}
}

void TraceEncodeFrame(int frame_number, int gop_length, int width, int height)
{
	OpenTraceFile();
	if (tracefile != NULL) {
		fprintf(tracefile,
				"# Frame: %d, gop length: %d, width: %d, height: %d\n",
				frame_number, gop_length, width, height);
	}
}

void TraceEncodeChannel(int channel)
{
	OpenTraceFile();
	if (tracefile != NULL) {
		fprintf(tracefile, "# Channel: %d\n", channel);
	}
}

void TraceEncodeBand(int width, int height)
{
	OpenTraceFile();
	if (tracefile != NULL) {
		fprintf(tracefile, "# Band width: %d, height: %d\n", width, height);
	}
}

void TracePutBits(int bit_count)
{
	OpenTraceFile();
	if (tracefile != NULL) {
		fprintf(tracefile, "%d\n", bit_count);
	}
}

#endif
