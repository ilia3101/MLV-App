/*! @file bitstream.h

*  @brief Variable length coding.
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

#ifndef _BITSTREAM_H
#define _BITSTREAM_H

#ifdef _POSIX
#undef _POSIX
#endif

#ifdef _WIN32
#include <windows.h>
#else
// Force the use of POSIX on Macintosh and Linux
#define _POSIX	1
#ifdef  __APPLE__
#include "../Common/macdefs.h"
#endif
#endif

#include <stdio.h>

#include "config.h"
#include "allocator.h"

// Omit support for AVI files by default
#ifndef _AVIFILES
#define _AVIFILES 0
#endif

#if _AVIFILES
#include <vfw.h>
#endif

// Control logfile output of bit packing operations
#define TRACE_PUTBITS	(0 && _DEBUG)

#ifndef _BITSTREAM_UNALIGNED
#define _BITSTREAM_UNALIGNED	1
#endif

#define BITSTREAM_BLOCK_LENGTH		(64 * 1024)		// Number of words in a block
#define BITSTREAM_WORD_SIZE			8				// Number of bits per word
#define BITSTREAM_LONG_SIZE			32				// Number of bits in a int32_t word

#define BITSTREAM_WORDS_PER_LONG	(BITSTREAM_LONG_SIZE/BITSTREAM_WORD_SIZE)
#define BITSTREAM_LONG_MASK			(sizeof(uint32_t ) - 1)

// Number of bits in a tag word or value inserted into the bitstream
#define BITSTREAM_TAG_SIZE  CODEC_TAG_SIZE

#define _BITSTREAM_BUFFER_BYTE 0			// Is the bitstream buffer a single byte?

#if _BITSTREAM_BUFFER_BYTE
#define BITSTREAM_BUFFER_T		uint8_t 
#define BITSTREAM_BUFFER_SIZE	BITSTREAM_WORD_SIZE
#else
#define BITSTREAM_BUFFER_T		uint32_t 
#define BITSTREAM_BUFFER_SIZE	BITSTREAM_LONG_SIZE
#endif

#define BITSTREAM_UNDEFINED_VALUE 0x0C0C0C0C

#define BITSTREAM_WORD_MASK		0xFFFF


// Use the same structure packing as the Intel C/C++ compiler
//#pragma pack(push)
//#pragma pack(16)
#pragma pack(push, 16)


/*
	The bitstream data structure supports the abstraction of an infinite
	stream of bits output from an encoder or input to a decoder.  For output,
	bits are shifted into a word until the word is full and inserted into
	the block of words.  For input, bits are shifted out of a word and a
	new word is read from the block when the current word is empty.
*/

typedef uint8_t BITWORD;	// Bits are output in multiples of one byte
typedef uint32_t BITLONG;	// Can handle up to one longword of bits
typedef int64_t BITCOUNT;		

typedef int16_t TAGWORD;		// Bitstream tag or value

typedef union tagvalue			// Bitstream tag and value pair
{
	struct {					// Fields in order for byte swapping
		TAGWORD value;
		TAGWORD tag;
	} tuple;

	uint32_t  longword;			// Tag value pair as a int32_t word

} TAGVALUE;


#if _POSIX

// POSIX definitions
#define BITSTREAM_FILE_INVALID		NULL

typedef enum
{
	BITSTREAM_ACCESS_NONE = 0,		// ""
	BITSTREAM_ACCESS_READ,			// "rb"
	BITSTREAM_ACCESS_WRITE			// "wb"

} BITSTREAM_ACCESS;

#else

// Windows definitions
#define BITSTREAM_FILE_INVALID		INVALID_HANDLE_VALUE
#define BITSTREAM_ACCESS_READ		GENERIC_READ
#define BITSTREAM_ACCESS_WRITE		GENERIC_WRITE
#define BITSTREAM_ACCESS_NONE		0

#endif

// Can associate a bitstream with a video stream in an AVI file
#if _AVIFILES
#include <vfw.h>
#define BITSTREAM_AVI_INVALID		0
#define BITSTREAM_AVI_NONE			0
#define BITSTREAM_AVI_READ			1
#define BITSTREAM_AVI_WRITE			2
#endif

// Bitstream error codes
enum {
	BITSTREAM_ERROR_OKAY = 0,
	BITSTREAM_ERROR_UNKNOWN,		// Unknown type of error
	BITSTREAM_ERROR_OVERFLOW,		// Attempt to write a word to a full buffer
	BITSTREAM_ERROR_UNDERFLOW,		// Attempt to read a word from an empty buffer
	BITSTREAM_ERROR_AVISAMPLE,		// Could not read sample from an AVI stream
	BITSTREAM_ERROR_READ,			// Could not read a block from a file
	BITSTREAM_ERROR_WRITE,			// Error writing a block to a file
	BITSTREAM_ERROR_BADTAG,			// Error reading a tag from the bitstream
	BITSTREAM_ERROR_ACCESS,			// Bad access mode

	// Add new error codes above this line

	BITSTREAM_ERROR_NUM_ERRORS		// Number of bitstream error codec (including okay)
};

#define NESTING_LEVELS	8

//TODO: Change the block length and number of words used to size_t

typedef struct bitstream
{
	/***** Must preserve the types and offsets of the following fields *****/

	int32_t error;				// Error parsing the bitstream
	int nBitsFree;				// Number of bits available in the current word
	uint8_t *lpCurrentWord;		// Pointer to next word in block
	int nWordsUsed;				// Number of words used in the block
	int32_t dwBlockLength;		// Number of entries in the block
	BITSTREAM_BUFFER_T wBuffer;	// Current word bit buffer

	/***** End of the fields with restrictions on the type and offset *****/


	//TODO: Is it still necessary to preserve the types and offsets of the fields?


	uint8_t  *lpCurrentBuffer;	// Pointer to the beginning of the buffer

	BITCOUNT cntBits;			// Number of bits written to the bitstream

	// Change this so more blocks are allocated on demand
#if 0
	uint8_t  block[BITSTREAM_BLOCK_LENGTH];		// was used form some very old debug code -- causes DECODER to grow hugely
#endif

	// File handle for writing the bitstream to an output file
#if _POSIX
	FILE *file;
	//char *access;
	BITSTREAM_ACCESS access;
#else
	HANDLE file;
	DWORD access;
#endif

#if _AVIFILES
	// Handle for associating a bitstream with an AVI file
	PAVISTREAM pavi;
	int32_t sample;
#endif

	// Alignment of the bitstream within the sample
	int alignment;

	unsigned int ChunkSizeOffset[NESTING_LEVELS];		//level pointers to the tuple to write the chunksize
														//-- used with SizeTagPush(output,TAG) & SizeTagPop(output)

} BITSTREAM;


// Forward reference
//typedef enum codec_tag CODEC_TAG;

// Macros for measuring the encoded size of the bitstream (in bits)
#if _DEBUG

#define START_BITCOUNT(s, c)	{ StartBitcount(s, &c); }
#define STOP_BITCOUNT(s, c)		{ StopBitcount(s, &c); }

#else

#define START_BITCOUNT(c)
#define STOP_BITCOUNT(c)

#endif


#ifdef __cplusplus
extern "C" {
#endif

// Mask for least significant n bits in a int32_t word
#define BITMASK(n)		_bitmask[n]
extern const uint32_t  _bitmask[];

// Initialize the bitstream
void InitBitstream(BITSTREAM *stream);

// Initialize the bitstream and associate it with a buffer
void InitBitstreamBuffer(BITSTREAM *stream, uint8_t  *buffer, size_t length, uint32_t access);

// Force pending bits in the bitstream buffer to be written to the block
void FlushBitstream(BITSTREAM *stream);
void FlushBitstreamAlign(BITSTREAM *stream, int align);

// Reset output bitstream for reading
void RewindBitstream(BITSTREAM *stream);

// Save the current position of the pointer into the block
uint8_t *GetBitstreamPosition(BITSTREAM *stream);

// Set the current position of the pointer into the block
void SetBitstreamPosition(BITSTREAM *stream, uint8_t  *position);

// Read specified number of bits from a bitstream
uint32_t GetBits(BITSTREAM *stream, int nBitCount);

// Read a double word from the bitstream
uint32_t GetLong(BITSTREAM *stream);

// Read a tag value pair from the bitstream
TAGVALUE GetTagValue(BITSTREAM *stream);

// Read an optional tag value pair from the bitstream
TAGVALUE GetTagOptional(BITSTREAM *stream);

// Return any segment with a required or optional tag
TAGVALUE GetSegment(BITSTREAM *stream);

// Read the specified tag from the bitstream and return the value
//TAGWORD GetValue(BITSTREAM *stream, enum codec_tag tag);
TAGWORD GetValue(BITSTREAM *stream, int tag);
//TAGWORD GetValue(BITSTREAM *stream, CODEC_TAG tag);

// Skip to the end of the encoded subband
void SkipSubband(BITSTREAM *stream);

// Read a 16-bit value from the bitstream
int GetWord16s(BITSTREAM *stream);

// Was a valid tag read from the bitstream?
bool IsValidSegment(BITSTREAM *stream, TAGVALUE segment, TAGWORD tag);

// Does the tag value pair have the specified tag code and value?
//BOOL IsTagValue(TAGVALUE segment, enum codec_tag tag, TAGWORD value);
bool IsTagValue(TAGVALUE segment, int tag, TAGWORD value);
//BOOL IsTagValue(TAGVALUE segment, CODEC_TAG tag, TAGWORD value);

// Insert the specified number of bits into a field
uint32_t  AddBits(BITSTREAM *stream, uint32_t  word, int nBitCount);

// Look ahead into the bitstream
uint32_t  PeekBits(BITSTREAM *stream, int nBitCount);

// Skip bits in the bitstream
void SkipBits(BITSTREAM *stream, int nBits);

// Look at the next longword in the bitstream without changing the position within the stream
uint32_t  PeekLong(BITSTREAM *stream);

// Skip the next longword in the bitstream
void SkipLong(BITSTREAM *stream);

// Read one byte from the bitstream
uint8_t GetByte(BITSTREAM *stream);

// Write bits to a bitstream
void PutBits(BITSTREAM *stream, uint32_t  bitlong, int length);

// Output a tagged value with double word alignment
void PutTagPair(BITSTREAM *stream, int tag, int value);

// Output an optional tagged value
void PutTagPairOptional(BITSTREAM *stream, int tag, int value);

// Output a tag that marks a place in the bitstream for debugging
void PutTagMarker(BITSTREAM *stream, uint32_t  marker, int size);

// Write a 16-bit value to the bitstream
void PutWord16s(BITSTREAM *stream, int value);

// Force the output of the current value into the bitstream
void WriteLong(BITSTREAM *stream, uint32_t  bitlong, int length);

// Write zeros to fill the current word in a bitstream
void PadBits(BITSTREAM *stream);

// Write zeros to fill the current wBuffer in a bitstream
void PadBits32(BITSTREAM *stream);

// Pad the bitstream to a tag boundary
void PadBitsTag(BITSTREAM *stream);

// Write a int32_t word into the bitstream
void PutLong(BITSTREAM *stream, uint32_t  word);

// Align the bitstream to the next word boundary
void AlignBits(BITSTREAM *stream);

// Align the bitstream to the beginning of a tag
void AlignBitsTag(BITSTREAM *stream);

// Align the bitstream to the next int32_t word boundary
void AlignBitsLong(BITSTREAM *stream);

// Check that the bitstream is aligned on a word boundary
bool IsAlignedBits(BITSTREAM *stream);

// Check that the bitstream is aligned on a word boundary
bool IsAlignedTag(BITSTREAM *stream);

// Set the current bitstream position to have the specified alignment
void SetBitstreamAlignment(BITSTREAM *stream, int alignment);

// Return the current size (in bytes) of the bitstream
int BitstreamSize(BITSTREAM *stream);

// Compute number of bits required to represent a positive number
int LeftMostOne(unsigned int number);

// Routines for associating the bitstream with files
#if 0
#if _ALLOCATOR
BITSTREAM *CreateBitstream(ALLOCATOR *allocator, const char *filename, DWORD access);
#else
BITSTREAM *CreateBitstream(const char *filename, DWORD access);
#endif
void DeleteBitstream(BITSTREAM *stream);
void OpenBitstream(BITSTREAM *stream, const char *filename, DWORD access);
void CloseBitstream(BITSTREAM *stream);
void AttachBitstreamFile(BITSTREAM *stream, HANDLE file, DWORD access);
void DetachBitstreamFile(BITSTREAM *stream);
#endif

// Routines for associating the bitstream with an external buffer
void SetBitstreamBuffer(BITSTREAM *stream, uint8_t *buffer, size_t length, uint32_t access);
void ClearBitstream(BITSTREAM *stream);
size_t BitstreamByteCount(BITSTREAM *stream);
void CopyBitstream(BITSTREAM *source, BITSTREAM *target);

#if _AVIFILES
// Routines for associating the bitstream with an AVI file
void AttachBitstreamAVI(BITSTREAM *stream, PAVISTREAM pavi, DWORD access);
void DetachBitstreamAVI(BITSTREAM *stream);
#endif

#if _DEBUG

// Routines for debugging bistreams
void DumpBitstream(BITSTREAM *stream, FILE *logfile);

// Dump the bitstream in the vicinity of the current location
void DumpBits(BITSTREAM *stream, FILE *file);

// Dump the next bytes in the bitstream
void DumpBytes(BITSTREAM *stream, int count, FILE *file);

// Dump bitstream tags and values
void DumpBitstreamTags(BITSTREAM *stream, int count, FILE *logfile);

// Set the logfile used for debugging the bitstream routines
void SetBitstreamLogfile(BITSTREAM *stream, FILE *logfile);

// Routines for measuring the size of encoded wavelet bands
void StartBitcount(BITSTREAM *stream, BITCOUNT *bitcount);
void StopBitcount(BITSTREAM *stream, BITCOUNT *bitcount);

void DebugOutputBitstreamPosition(BITSTREAM *stream);
void DebugOutputBitstreamBytes(BITSTREAM *stream, int count);

void PrintBitstreamPosition(BITSTREAM *stream, FILE *logfile);

#if TRACE_PUTBITS

void OpenTraceFile();
void CloseTraceFile();
void TraceEncodeFrame(int frame_number, int gop_length, int width, int height);
void TraceEncodeChannel(int channel);
void TraceEncodeBand(int width, int height);
void TracePutBits(int bit_count);

#endif

#endif

// New routines for nested tags
void SizeTagPush(BITSTREAM *stream, int tag);
void SizeTagPop(BITSTREAM *stream);

#ifdef __cplusplus
}
#endif

#pragma pack(pop)

#endif
