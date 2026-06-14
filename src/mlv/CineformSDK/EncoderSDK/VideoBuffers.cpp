/*! @file VideoBuffers.cpp

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
#include "StdAfx.h"

#undef	DEBUG
#define	DEBUG (1 && _DEBUG)

#ifndef ASSERT
#define ASSERT(x) assert(x)
#endif

#if _ALLOCATOR
#include "IAllocator.h"
#endif
#include "CFHDTypes.h"
#include "VideoBuffers.h"

//! Return true if a buffer with the specified size was allocated
bool CGenericBuffer::Alloc(size_t size,
						   size_t alignment)
{
	if (size == 0)
	{
		assert(m_dataBuffer == NULL && m_bufferSize == 0);
		return true;
	}

	if (m_dataBuffer)
	{
		if (m_bufferSize < size || m_alignment < alignment)
		{
			// Free the buffer so that a larger buffer can be allocated
			Release();
		}
		else
		{
			// Use the existing buffer
			return true;
		}
	}

	if (alignment > 0)
	{
		// Use the allocator to obtain an aligned memory block
		m_dataBuffer = AlignedAlloc(size, alignment);
	}
	else
	{
		// Use the allocator to obtain an unaligned memory block
		m_dataBuffer = UnalignedAlloc(size);
	}

	assert(m_dataBuffer);
	if (m_dataBuffer)
	{
		m_bufferSize = size;
		m_alignment = alignment;
		return true;
	}
	else
	{
		m_bufferSize = 0;
		m_alignment = 0;
		return false;
	}
}

bool CGenericBuffer::Release()
{
	if (m_dataBuffer)
	{
		if (m_alignment > 0)
		{
			// Use the allocator to free an aligned memory block
			AlignedFree(m_dataBuffer);
		}
		else
		{
			// Use the allocator to free an unaligned memory block
			UnalignedFree(m_dataBuffer);
		}

		m_dataBuffer = NULL;
		m_bufferSize = 0;
		m_alignment = 0;
	}

	return true;
}

/*!
	Return the pixel size of the specified format (in bytes)

	The code for computing the pixel size was copied from ISampleDecoder.h
*/
size_t CFrameBuffer::PixelSize(CFHD_PixelFormat format)
{
	size_t pixelSize = 0;

	// Compute the pixel size
	switch (format)
	{
	case CFHD_PIXEL_FORMAT_2VUY:
	case CFHD_PIXEL_FORMAT_YUYV:
	case CFHD_PIXEL_FORMAT_YUY2:
		pixelSize = 2;
		break;

	case CFHD_PIXEL_FORMAT_BGRA:
		pixelSize = 4;
		break;

	case CFHD_PIXEL_FORMAT_RG24:
		pixelSize = 3;
		break;

	case CFHD_PIXEL_FORMAT_B64A:
	case CFHD_PIXEL_FORMAT_W13A:
		pixelSize = 8;
		break;

	case CFHD_PIXEL_FORMAT_YU64:
		pixelSize = 4;
		break;

	case CFHD_PIXEL_FORMAT_RG48:
	case CFHD_PIXEL_FORMAT_WP13:
		pixelSize = 6;
		break;

	case CFHD_PIXEL_FORMAT_DPX0:
		pixelSize = 4;
		break;

	case CFHD_PIXEL_FORMAT_BYR4:
		pixelSize = 2;
		break;

	// Avid pixel formats
	case CFHD_PIXEL_FORMAT_CT_UCHAR:							// Avid 8-bit CbYCrY 4:2:2 (no alpha)
		pixelSize = 2;
		break;
	case CFHD_PIXEL_FORMAT_CT_10BIT_2_8:						// Two planes of 8-bit and 2-bit pixels
		break;
	case CFHD_PIXEL_FORMAT_CT_SHORT_2_14:						// Avid fixed point 2.14 pixel format
		pixelSize = 4;
		break;
	case CFHD_PIXEL_FORMAT_CT_USHORT_10_6:						// Avid fixed point 10.6 pixel format
		pixelSize = 4;
		break;
	case CFHD_PIXEL_FORMAT_CT_SHORT:							// Avid 16-bit signed pixels
		pixelSize = 4;
		break;

	//TODO: Add more pixel formats defined in CFHDDecoder.h

	case CFHD_PIXEL_FORMAT_R210:
	case CFHD_PIXEL_FORMAT_RG30:	
	case CFHD_PIXEL_FORMAT_AR10:	
	case CFHD_PIXEL_FORMAT_AB10:	
		pixelSize = 4;
		break;
			
	case CFHD_PIXEL_FORMAT_RG64:	
		pixelSize = 8;
		break;

	case CFHD_PIXEL_FORMAT_BYR2:
	case CFHD_PIXEL_FORMAT_V210:	


	default:
		assert(0);
		//throw CFHD_ERROR_BADFORMAT;
		break;
	}

	// Must return a valid pixel size
	ASSERT(pixelSize > 0);
	return pixelSize;
}
