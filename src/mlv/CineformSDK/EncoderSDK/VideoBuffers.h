/*! @file VideoBuffers.h

*  @brief Definition of the classes for buffer abstractions
*  
*  This is a copy of the generic, sample, and frame buffers from
*  Libs/CommonLib/MediaBuffers.h without the audio buffer classes.
*  
*  This version of the buffer classes uses the same memory allocator
*  used by the encoder SDK.
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
#pragma once

// Forward reference
//class IAllocator;

#ifndef FOURCC
#define FOURCC(x)	(char)((x) & 0xFF), (char)(((x) >> 8) & 0xFF), (char)(((x) >> 16) & 0xFF), (char)(((x) >> 24) & 0xFF)
#endif

/*!
	@brief Base class for all buffer abstractions
*/
class CGenericBuffer
{
protected:

	CGenericBuffer(CFHD_ALLOCATOR *allocator = NULL,
				   size_t size = 0,
				   size_t alignment = 0) :
		 m_allocator(allocator),
		 m_dataBuffer(NULL),
		 m_bufferSize(0),
		 m_alignment(0)
	{
		Alloc(size, alignment);
	}

	~CGenericBuffer()
	{
		// Free the buffer if it was allocated
		Release();
	}

	bool SetAllocator(CFHD_ALLOCATOR *allocator)
	{
		// Cannot change the allocator
		if (m_allocator) return false;

		m_allocator = allocator;
		return true;
	}

	//! Return true if a buffer with the specified size was allocated
	bool Alloc(size_t size, size_t alignment = 0);

	bool Release();

	void *m_dataBuffer;				//!< Pointer to the data stored in the buffer
	size_t m_bufferSize;			//!< Allocated size of the data buffer
	size_t m_alignment;				//!< Alignment of the allocated buffer

private:

	CFHD_ALLOCATOR *m_allocator;	//!< Memory allocator for the data buffer
	
	void *UnalignedAlloc(size_t size)
	{
#if _ALLOCATOR
		// Use the allocator if it is available
		if (m_allocator) {
			return m_allocator->vtable->unaligned_malloc(m_allocator, size);
		}
#endif
		// Otherwise use the default memory allocator
		return malloc(size);
	}

	void UnalignedFree(void *block)
	{
#if _ALLOCATOR
		// Use the allocator if it is available
		if (m_allocator) {
			m_allocator->vtable->unaligned_free(m_allocator, block);
			return;
		}
#endif
		// Otherwise use the default memory allocator
		free(block);
	}

	void *AlignedAlloc(size_t size, size_t alignment)
	{
#if _ALLOCATOR
		// Use the aligned allocator if it is available
		if (m_allocator != NULL) {
			return m_allocator->vtable->aligned_malloc(m_allocator, size, alignment);
		}
#endif
		// Otherwise use the default aligned memory allocator
#if 1
  #ifdef __APPLE__
		return malloc(size);
  #else
		return _mm_malloc(size, alignment);
  #endif
#else
        int     memerror;
        void *  allocatedMemory;
        
        memerror = posix_memalign(&allocatedMemory, alignment, size);
        if (0==memerror) {
            return allocatedMemory;
        } else {
            return NULL;
        }
#endif
	}

	void AlignedFree(void *block)
	{
#if _ALLOCATOR
		// Use the aligned allocator if it is available
		if (m_allocator) {
			m_allocator->vtable->aligned_free(m_allocator, block);
			return;
		}
#endif
		// Otherwise use the default aligned memory allocator
#if __APPLE__
		free(block);
#else
		_mm_free(block);
#endif
	}

};

/*!
	@brief Buffer for encoded video samples
*/
class CSampleBuffer : public CGenericBuffer
{
private:

	enum
	{
		// The sample encoder requires buffers aligned to a sixteen byte boundary
		SAMPLE_BUFFER_ALIGNMENT = 16,
	};

public:

	CSampleBuffer(CFHD_ALLOCATOR *allocator = NULL) :
		CGenericBuffer(allocator),
		m_sampleSize(0)//,
		//m_sampleAlignment(SAMPLE_BUFFER_ALIGNMENT)
		//m_sampleOffset(0)
	{
	}

	CSampleBuffer(size_t sampleSize,
				  size_t sampleAlignment = SAMPLE_BUFFER_ALIGNMENT,
				  CFHD_ALLOCATOR *allocator = NULL) :
		CGenericBuffer(allocator),
		m_sampleSize(0)//,
		//m_sampleAlignment(sampleAlignment)
		//m_sampleOffset(0)
	{
		Alloc(sampleSize);
	}

	~CSampleBuffer()
	{
		m_sampleSize = 0;
		//m_sampleOffset = 0;
	}

	bool SetAllocator(CFHD_ALLOCATOR *allocator)
	{
		return CGenericBuffer::SetAllocator(allocator);
	}

	// Allocate a sample with the specified size and alignment
	bool Alloc(size_t sampleSize, size_t sampleAlignment = SAMPLE_BUFFER_ALIGNMENT)
	{
		// Allocate a buffer with the requested size and alignment
		if (!CGenericBuffer::Alloc(sampleSize, sampleAlignment)) {
			return false;
		}

		// Remember the actual size of the sample
		m_sampleSize = sampleSize;

		// Should have allocated a buffer large enough for an unbuffered read
		assert(m_dataBuffer && sampleSize <= m_bufferSize);
		if (! (m_dataBuffer && sampleSize <= m_bufferSize)) {
			return false;
		}

		// Record the offset to the start of the sample
		//m_sampleOffset = sampleOffset;

		// The sample buffer has been properly allocated
		return true;
	}

	bool Release()
	{
		// Free the buffer
		bool result = CGenericBuffer::Release();

		// Zero the actual size of the sample
		m_sampleSize = 0;

		// Should have released the sample buffer
		assert(m_dataBuffer == NULL && m_bufferSize == 0);

		return result;
	}

	//! Set the size of the sample to the actual encoded size
	void SetActualSize(size_t actualSize)
	{
		m_sampleSize = actualSize;
	}

	//! Return the address of the start of the sample
	void *Buffer()
	{
#if 0
		char *samplePtr = (char *)m_dataBuffer;
		samplePtr += m_sampleOffset;
		return samplePtr;
#else
		return m_dataBuffer;
#endif
	}

	//! Return the size of the sample (not the allocated buffer size)
	size_t Size()
	{
		// Return the size of the sample (in bytes)
		return m_sampleSize;
	}
		
	//! Return the size of the buffer (not the allocated sample size)
	size_t BufferSize()
	{
		// Return the size of the buffer (in bytes)
		return m_bufferSize;
	}

#if 0
	//! Return the address of the sample buffer
	void *BlockAddr()
	{
		return m_dataBuffer;
	}

	//! Return the size of the allocated sample buffer (in bytes)
	size_t BlockSize()
	{
		return m_bufferSize;
	}
#endif

	// Alternative method names and operators for accessing the sample data

#if 0
	void *Data()
	{
		char *samplePtr = (char *)m_dataBuffer;
		return (samplePtr + m_sampleOffset);
	}
#endif

	operator void * ()
	{
#if 0
		char *samplePtr = (char *)m_dataBuffer;
		return (samplePtr + m_sampleOffset);
#else
		return Buffer();
#endif
	}

	operator size_t ()
	{
		return m_sampleSize;
	}

	bool IsValid()
	{
		return (m_dataBuffer != NULL);
	}


private:

	size_t m_sampleSize;		//!< Actual size of the sample
	//size_t m_sampleOffset;	//!< Offset to the sample start (new version)
	//size_t m_sampleAlignment;		//!< Sample buffer alignment (in bytes)

};

/*!
	@brief Buffer for images
*/
class CFrameBuffer : public CGenericBuffer
{
private:

	enum
	{
		// Frame buffers must be aligned to a 512-byte boundary (for R3DSDK)
		FRAME_BUFFER_ALIGNMENT = 512,
	};

public:

	CFrameBuffer(CFHD_ALLOCATOR *allocator = NULL) :
		CGenericBuffer(allocator),
		m_offset(0),
		m_width(0),
		m_height(0),
		m_pitch(0),
		m_format(CFHD_PIXEL_FORMAT_UNKNOWN)
	{
	}

	CFrameBuffer(int width, int height, CFHD_PixelFormat format, size_t offset = 0) :
		CGenericBuffer(NULL),
		m_offset(0)
	{
		// Allocate the frame using the default allocator
		Alloc(width, height, format, offset);
	}

	void SetAllocator(CFHD_ALLOCATOR *allocator)
	{
		CGenericBuffer::SetAllocator(allocator);
	}

	void SetBufferFormat(size_t width, size_t height, size_t pitch, CFHD_PixelFormat format, size_t offset = 0)
	{
		m_width = width;
		m_height = height;
		m_pitch = pitch;
		m_format = format;
		m_offset = offset;
	}

	bool Alloc(size_t width, size_t height, CFHD_PixelFormat format, size_t offset = 0)
	{
		size_t pitch = 0;
		size_t size = 0;

		// Compute the pitch and size of the frame buffer (in bytes)
		if (format == CFHD_PIXEL_FORMAT_NV12)
		{
			assert((height % 2) == 0);
			size_t luma_height = height;
			size_t chroma_height = (height + 1) / 2;
			pitch = Align16(width);
			size = (luma_height + chroma_height) * pitch;
		}
		else if (format == CFHD_PIXEL_FORMAT_CT_10BIT_2_8)
		{
			size_t lower_size = 2 * (width * height);
			size_t upper_size = (width * height) / 2;
			pitch = width;
			size = upper_size + lower_size;
		}
		else
		{
			size_t pixelSize = PixelSize(format);
			pitch = Align16(width * pixelSize);
			size = height * pitch;
		}
		assert(pitch > 0 && size > 0);

		// Add the size of the frame header
		size += offset;

		// Use the allocator in the base class
		if (CGenericBuffer::Alloc(size, FRAME_BUFFER_ALIGNMENT))
		{
			m_width = width;
			m_height = height;
			m_pitch = pitch;
			m_format = format;
			m_offset = offset;

#if (0 && _WIN32 && !defined(_PRODUCTION))
			char message[256];
			sprintf_s(message,
				"CFrameBuffer::Alloc allocated buffer: 0x%08X, size: %d, width: %d, height: %d, format: %c%c%c%c\n",
				m_dataBuffer, m_bufferSize, m_width, m_height, FOURCC(m_format));
			OutputDebugString(message);
#endif
			// The frame buffer has been properly allocated
			return true;
		}
		else
		{
#if (0 && _WIN32 && !defined(_PRODUCTION))
			char message[256];
			sprintf_s(message, "CFrameBuffer::Alloc failed size: %d, width: %d, height: %d, format: %c%c%c%c\n",
				size, width, height, FOURCC(format));
			OutputDebugString(message);
#endif
		}
		return false;
	}

	bool Release()
	{
		return CGenericBuffer::Release();
	}

	//! Return the address of the image in the buffer
	void *Buffer()
	{
		//return m_dataBuffer;
		return (void *)((uintptr_t)m_dataBuffer + m_offset);
	}

	size_t Width()
	{
		return m_width;
	}

	size_t Height()
	{
		return m_height;
	}

	size_t Pitch()
	{
		return m_pitch;
	}

	CFHD_PixelFormat Format()
	{
		return m_format;
	}

	bool IsValid()
	{
		assert(m_width > 0 && m_height > 0 && m_pitch > 0);
		if (! (m_width > 0 && m_height > 0 && m_pitch > 0)) {
			return false;
		}

		assert(m_format != CFHD_PIXEL_FORMAT_UNKNOWN);
		if (! (m_format != CFHD_PIXEL_FORMAT_UNKNOWN)) {
			return false;
		}

		assert(m_dataBuffer != NULL && m_bufferSize > 0);
		if (! (m_dataBuffer != NULL && m_bufferSize > 0)) {
			return false;
		}

		assert(m_alignment >= FRAME_BUFFER_ALIGNMENT);
		if (! (m_alignment >= FRAME_BUFFER_ALIGNMENT)) {
			return false;
		}

		return true;
	}

	void GetFrameInfo(size_t *widthOut, size_t *heightOut, CFHD_PixelFormat *formatOut)
	{
		if (widthOut && heightOut && formatOut)
		{
			*widthOut = m_width;
			*heightOut = m_height;
			*formatOut = m_format;
		}
	}

	size_t Size()
	{
		return m_bufferSize;
	}

	//! Return the address of the buffer (same as the frame only if the offset is zero)
	void *BufferAddress()
	{
		return m_dataBuffer;
	}

	//! Return the size of the buffer including the header if the offset is not zero
	size_t BufferSize()
	{
		return m_bufferSize;
	}

	//! Return the address of the image in the buffer
	void *ImageBuffer()
	{
		return (void *)((uintptr_t)m_dataBuffer + m_offset);
	}

protected:

	// Return the pixel size of the specified format (in bytes)
	size_t PixelSize(CFHD_PixelFormat format);

	// Round up the size to a multiple of 16 bytes
	size_t Align16(size_t size)
	{
		const size_t mask = 0x0F;
		return ((size + mask) & ~mask);
	}

//private:
protected:

	size_t m_width;
	size_t m_height;
	size_t m_pitch;
	CFHD_PixelFormat m_format;

	size_t m_offset;
};

#if 0
/*!
	@brief Buffer for chunks of audio

	@todo Need to finish this class and use it in the file readers and importers
*/
class CAudioBuffer : public CGenericBuffer
{
public:

	CAudioBuffer(CFHD_ALLOCATOR *allocator = NULL) :
		CGenericBuffer(allocator),
		m_sampleCount(0),
		m_sampleSize(0),
		m_splitSampleSize(),
		m_channelBufferCount(0),
		m_channelSize(0),
		m_splitBuffer(NULL)

	{
	}
	//! This constructor is only used for testing
	//		sampleSize = bytes per sample (usually 2)
	//		sampleCount = channels*samplesPerBuffer (48000*channels) normally
	CAudioBuffer(size_t sampleSize, size_t sampleCount) :
		CGenericBuffer(NULL)
	{
		// Allocate the frame using the default allocator
		Alloc(sampleSize, sampleCount);
	}

	void SetAllocator(CFHD_ALLOCATOR *allocator)
	{
		CGenericBuffer::SetAllocator(allocator);
	}

	bool Alloc(size_t sampleSize, size_t sampleCount)
	{

		// Use the allocator in the base class
		if (CGenericBuffer::Alloc(sampleSize*sampleCount, 0))
		{
			m_sampleCount = sampleCount;
			m_splitSampleSize = m_sampleSize = sampleSize;
			m_channelSize = sampleSize * sampleCount;
			m_channelBufferCount = 0;
			m_splitBuffer = NULL;
			return true;
		}
		return false;
	}
	size_t Size()
	{
		return m_bufferSize;
	}
	size_t NumSamples()
	{
		return m_sampleCount;
	}
	void *Buffer()
	{
		return m_dataBuffer;
	}
	bool SplitChannels(int numChannels, int destByteSize=0)
	{
		// m_sampleCount must be multiple of numChannels
		// if needed allocate an array of pointers to array of samples
		//	each array is m_bufferSize/numChannels

		int srcSize, dstSize;

		if( (numChannels != 0) && (m_sampleSize % numChannels)==0)
		{
			srcSize = (int)(m_sampleSize/numChannels);
			if( destByteSize == 0)
			{
				dstSize = srcSize;
			}
			else
			{ 
				dstSize = destByteSize;
			}
			m_splitSampleSize = dstSize;
			if(m_channelBufferCount != numChannels)
			{
				if(m_channelBufferCount > 0)
				{
					for(int i = 0; i < m_channelBufferCount; i++)
					{
						free( m_splitBuffer[i] );
					}
					free( m_splitBuffer );
				}
				m_splitBuffer = (void **)calloc( numChannels, sizeof(void *));
				m_channelSize = m_sampleCount * dstSize;
				for(int i=0; i<numChannels; i++)
				{
					m_splitBuffer[i] = malloc( m_channelSize );
				}
				m_channelBufferCount = numChannels;
			}
			// Buffers allocated.  Copy source to dest.  Can be 2,3 or 4 bytes per sample.
			for( int chan=0; chan<numChannels; chan++)
			{
				unsigned char * src = (unsigned char *)m_dataBuffer;
				unsigned char * dst = (unsigned char *)m_splitBuffer[chan];
				src += (chan * srcSize);
				for(int i=0; i < (int)m_sampleCount; i++)
				{
					if(srcSize==dstSize)
					{
						memcpy( dst, src, srcSize);
						dst += dstSize;
						src += (m_sampleSize);
					}
					else
					{
						for(int s=0; s<dstSize;s++)
						{
							if(s<(dstSize-srcSize))
							{
								*dst++ = 0;
							}
							else
							{
								*dst++ = *src++;
							}
						}
						src += (m_sampleSize-srcSize);
					}
				}
			}
			return true;
		}
		return false;
	}
	bool MergeChannels()
	{
		if(m_channelBufferCount>1)
		{
			for( int chan=0; chan<m_channelBufferCount; chan++)
			{
				unsigned char * dst = (unsigned char *)m_dataBuffer;
				unsigned char * src = (unsigned char *)m_splitBuffer[chan];
				dst += (chan * (m_sampleSize/m_channelBufferCount));
				for(int i=0; i < (int)m_sampleCount; i++)
				{
					 memcpy( dst, src, m_sampleSize/m_channelBufferCount);
					 src += m_sampleSize/m_channelBufferCount;
					 dst += (m_sampleSize);
				}
			}
			return true;
		}
		return false;
	}
	size_t ChannelSize()
	{
		return m_channelSize;
	}
	size_t ChannelSize(size_t forNumSamples)
	{
		if(m_channelBufferCount)
		{
			return forNumSamples * m_splitSampleSize; // m_sampleSize / m_channelBufferCount;
		}
		else
		{
			return forNumSamples * m_sampleSize;
		}
	}
	void *ChannelBuffer(int channelNum)
	{
		if(channelNum < m_channelBufferCount)
		{
			return m_splitBuffer[channelNum];
		}
		else 
		{
			return NULL;
		}
	}
	
private:

	size_t m_sampleCount;
	size_t m_sampleSize;
	size_t m_splitSampleSize;
	int m_channelBufferCount;
	size_t m_channelSize;
	void **m_splitBuffer;

};
#endif
