/*! @file ImageScaler.h

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

#pragma once

#include "MemAlloc.h"
#include "thread.h"

typedef unsigned char UInt8;

#define Pi	3.1415926535

typedef struct {
	int srcline;
	int mixval;
} lanczosmix;

//
//
//	Regular C interface that can be used threaded follow the C++ routines
//
//
class CImageScaler
{
public:

	CImageScaler(IMemAlloc *pMemAlloc) :
		m_pMemAlloc(pMemAlloc)
	{
		ASSERT(m_pMemAlloc != NULL);
	}

	//~CImageScaler();

protected:

	void *Alloc(size_t size)
	{
		ASSERT(m_pMemAlloc != NULL);
		if (! (m_pMemAlloc != NULL)) return NULL;
		else return m_pMemAlloc->Alloc(size);
	}

	void Free(void *block)
	{
		ASSERT(m_pMemAlloc != NULL);
		if (m_pMemAlloc != NULL) {
			m_pMemAlloc->Free(block);
		}
	}

private:

	IMemAlloc *m_pMemAlloc;

};

// Base class for scalers that use the Lanczos algorithm
class CLanczosScaler : public CImageScaler
{
public:

	CLanczosScaler(IMemAlloc *pMemAlloc) :
		CImageScaler(pMemAlloc),
		horizontalscale(NULL)
	{
	}

	~CLanczosScaler()
	{
		// Free the scratch buffers used by the scaling routines
		FreeScratchMemory();
	}

protected:

	// Allocate scratch memory used by the scaling routines
	bool AllocScratchMemory(int outputWidth, int inputHeight, int pixelSize = 8)
	{
		size_t horizontal_scale_size = outputWidth * inputHeight * pixelSize;

		horizontalscale = (unsigned short *)Alloc(horizontal_scale_size);
		if (horizontalscale == NULL) return false;

		// Scratch memory has been successfully allocated
		return true;
	}

	// Free scratch memory used by the scaling routines
	void FreeScratchMemory()
	{
		if(horizontalscale) {
			Free((char *)horizontalscale);
			horizontalscale = NULL;
		}
	}

	// Compute the scale factors for interpolating along a row
	void ComputeRowScaleFactors( short *rowScaleFactors,
							 int inputWidth,
							 int outputWidth,
							 int lobes);

	// Compute the scale factors for interpolating down a column
	int ComputeColumnScaleFactors(int row, int inputWidth, int outputWidth,
								  int renderFieldType, lanczosmix *lmY, int lobes);

	// Compute the interpolation coefficients
	int LanczosCoeff(int inputsize, int outputsize, int line, lanczosmix *lm,
					 bool changefielddominance, bool interlaced, int lobes);

protected:

	// Scratch memory for use by the interpolator
	unsigned short *horizontalscale;

};


typedef struct
{
	THREAD_POOL pool;
	LOCK lock;
	int cpus;
	void *ptrs[10];
	int vars[10];
	int jobtype;
} MAILBOX;

// Scale YU64 input images to the output image dimensions
class CImageScalerYU64 : public CLanczosScaler
{
public:

	CImageScalerYU64(IMemAlloc *pMemAlloc) :
		CLanczosScaler(pMemAlloc),
		scalefactorsL(NULL),
		scalefactorsC(NULL)
	{
		memset(&mailbox, 0,sizeof(MAILBOX));
	}

	~CImageScalerYU64()
	{
		// Free the scratch buffers used by the scaling routines
		FreeScratchMemory();
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
	}

	// Allocate scratch memory used by the scaling routines
	bool AllocScratchMemory(int outputWidth, int inputHeight, int pixelSize = 6)
	{
		// Allocate scratch memory used by all Lanczos scalers
		if (!CLanczosScaler::AllocScratchMemory(outputWidth, inputHeight, pixelSize)) {
			return false;
		}

		//TODO: Need to compute the actual size that must be allocated
		scalefactorsL = (short *)Alloc(64000*2);
		if (scalefactorsL == NULL) return false;

		//TODO: Need to compute the actual size that must be allocated
		scalefactorsC = (short *)Alloc(64000*2);
		if (scalefactorsC == NULL) return false;

		// Scratch memory has been successfully allocated
		return true;
	}

	// Free scratch memory used by the scaling routines
	void FreeScratchMemory()
	{
		// Free the scratch memory used by all Lanczos scalers
		CLanczosScaler::FreeScratchMemory();

		if (scalefactorsL) {
			Free((char *)scalefactorsL);
			scalefactorsL = NULL;
		}

		if (scalefactorsC) {
			Free((char *)scalefactorsC);
			scalefactorsC = NULL;
		}
	}

	// Scale the rows of luma and chroma
	void ScaleRowValues(unsigned short *input, int inputWidth, int inputHeight, int inputPitch,
						unsigned short *output, int outputWidth);

	// Scale one row of luma values (skip the chroma values)
	void ScaleRowLuma(unsigned short *inputRow, unsigned short *outputRow, short *scaleFactors);

	// Scale one row of chroma values (skip the luma values)
	void ScaleRowChroma(unsigned short *inputRow, unsigned short *outputRow, short *scaleFactors);

	// Scale the luma and chroma values in the specified column
	void ScaleColumnValues(unsigned short *input, int stride,
						   lanczosmix *lmY, int sampleCount,
						   int &Y, int &U, int &V);

#define ScaleRowValuesThreadID	1
	void ScaleRowValuesThread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);

protected:

	// Scratch memory for use by the interpolator
	short *scalefactorsL;
	short *scalefactorsC;

};


// Scale NV12 input images to the output image dimensions
class CImageScalerNV12: public CLanczosScaler
{
public:

	CImageScalerNV12(IMemAlloc *pMemAlloc) :
		CLanczosScaler(pMemAlloc),
		scalefactorsL(NULL),
		scalefactorsC(NULL)
	{
#if 0
		memset(&mailbox, 0,sizeof(MAILBOX));
#endif
	}

	~CImageScalerNV12()
	{
		// Free the scratch buffers used by the scaling routines
		FreeScratchMemory();
#if 0
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
#endif
	}

	// Scale the NV12 input image to an NV12 output image
	void ScaleToNV12(void *input_buffer,
					 int input_width,
					 int input_height,
					 int input_pitch,
					 void *output_buffer,
					 int output_width,
					 int output_height,
					 int output_pitch);

	// Allocate scratch memory used by the scaling routines (16-bit YUV 4:4:4)
	bool AllocScratchMemory(int outputWidth, int inputHeight, int pixelSize = 6)
	{
		// Allocate scratch memory used by all Lanczos scalers
		if (!CLanczosScaler::AllocScratchMemory(outputWidth, inputHeight, pixelSize)) {
			return false;
		}

		//TODO: Need to compute the actual size that must be allocated
		scalefactorsL = (short *)Alloc(64000*2);
		if (scalefactorsL == NULL) return false;

		//TODO: Need to compute the actual size that must be allocated
		scalefactorsC = (short *)Alloc(64000*2);
		if (scalefactorsC == NULL) return false;

		// Scratch memory has been successfully allocated
		return true;
	}

	// Free scratch memory used by the scaling routines
	void FreeScratchMemory()
	{
		// Free the scratch memory used by all Lanczos scalers
		CLanczosScaler::FreeScratchMemory();

		if (scalefactorsL) {
			Free((char *)scalefactorsL);
			scalefactorsL = NULL;
		}

		if (scalefactorsC) {
			Free((char *)scalefactorsC);
			scalefactorsC = NULL;
		}
	}

	// Scale the rows of luma and chroma
	void ScaleRowValues(void *input_buffer, int input_width, int input_height,
						int input_pitch, uint16_t *output_buffer, int output_width);

	// Scale one row of luma values (skip the chroma values)
	//void ScaleRowLuma(unsigned short *inputRow, unsigned short *outputRow, short *scaleFactors);

	// Scale one row of chroma values (skip the luma values)
	//void ScaleRowChroma(unsigned short *inputRow, unsigned short *outputRow, short *scaleFactors);

	// Scale the luma and chroma values in the specified column
	void ScaleColumnValues(unsigned short *input, int stride,
						   lanczosmix *lmY, int sampleCount,
						   int &Y, int &U, int &V);

#if 0
#define ScaleRowValuesThreadID	1
	void ScaleRowValuesThread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);
#endif

protected:

	template <typename PixelType>
	PixelType Clamp16u(PixelType value)
	{
		return (value < 0) ? 0 : ((value > UINT16_MAX) ? UINT16_MAX : value);
	}

	template <typename PixelType>
	PixelType Clamp8u(PixelType value)
	{
		return (value < 0) ? 0 : ((value > UINT8_MAX) ? UINT8_MAX : value);
	}

	// Scratch memory for use by the interpolator
	short *scalefactorsL;
	short *scalefactorsC;

};

class CImageScalerRGB32 : public CLanczosScaler
{
public:

	CImageScalerRGB32(IMemAlloc *pMemAlloc) :
		CLanczosScaler(pMemAlloc),
		scaleFactors(NULL)
	{
		memset(&mailbox, 0,sizeof(MAILBOX));
	}

	~CImageScalerRGB32()
	{
		// Free the scratch buffers used by the scaling routines
		FreeScratchMemory();
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
	}

	// Scale the RGB32 input image to an RGB32 output image
	void ScaleToBGRA(unsigned char *inputBuffer,
					 int inputWidth,
					 int inputHeight,
					 int inputPitch,
					 unsigned char *outputBuffer,
					 int outputWidth,
					 int outputHeight,
					 int outputPitch);

	// Allocate scratch memory used by the scaling routines
	bool AllocScratchMemory(int outputWidth, int inputHeight)
	{
		// Allocate scratch memory used by all Lanczos scalers
		if (!CLanczosScaler::AllocScratchMemory(outputWidth, inputHeight)) {
			return false;
		}

		//TODO: Need to compute the actual size that must be allocated
		scaleFactors = (short *)Alloc(128000*2);
		if (scaleFactors == NULL) return false;

		// Scratch memory has been successfully allocated
		return true;
	}

	// Free scratch memory used by the scaling routines
	void FreeScratchMemory()
	{
		// Free the scratch memory used by all Lanczos scalers
		CLanczosScaler::FreeScratchMemory();

		if(scaleFactors) {
			Free((char *)scaleFactors);
			scaleFactors = NULL;
		}
	}

	// Scale the rows of RGB values
	void ScaleRowValues(unsigned char *input, int inputWidth, int inputHeight, int inputPitch,
						unsigned short *output, int outputWidth);

	void ScaleColumnValues(unsigned short *input, int stride,
						   lanczosmix *lmY, int sampleCount,
						   int &R, int &G, int &B);
	
#define ScaleRowValuesThreadID	1
	void ScaleRowValuesThread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);

protected:

	// Scratch memory for use by the interpolator
	short *scaleFactors;

};

// Scale and convert YU64 input images to various YUV output formats
class CImageScalerConverterYU64ToYUV : public CImageScalerYU64, public CImageConverterYU64ToYUV
{
public:

	CImageScalerConverterYU64ToYUV(IMemAlloc *pMemAlloc) :
		CImageScalerYU64(pMemAlloc)
	{
		memset(&mailbox, 0,sizeof(MAILBOX));
	}

	~CImageScalerConverterYU64ToYUV()
	{
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
	}

	// Scale and convert the input image to CbYCrY 4:2:0 for MPEG-2 encoding
	void ScaleToNV12(void *inputBuffer,
					 int inputWidth,
					 int inputHeight,
					 int inputPitch,
					 void *outputBuffer,
					 int outputWidth,
					 int outputHeight,
					 int outputPitch,
					 int row_offset = 0,
					 int column_offset = 0);

	void ScaleToYU64(void *inputBuffer,
					 int inputWidth,
					 int inputHeight,
					 int inputPitch,
					 void *outputBuffer,
					 int outputWidth,
					 int outputHeight,
					 int outputPitch);

	// Scale the input image to Avid 10-bit CbYCrY 4:2:2 (two planes of 8-bit and 2-bit pixels)
	void ScaleToCbYCrY_10bit_2_8(void *inputBuffer,
								 int inputWidth,
								 int inputHeight,
								 int inputPitch,
								 void *outputBuffer,
								 int outputWidth,
								 int outputHeight,
								 int outputPitch,
								 int row_offset = 0,
								 int column_offset = 0);

	
#define ScaleToNV12ActiveThreadID			1
#define ScaleToYU64ThreadID					2
#define ScaleToCbYCrY_10bit_2_8_ThreadID	3
	void ScaleToNV12ActiveThread(int index);
	void ScaleToYU64Thread(int index);
	void ScaleToCbYCrY_10bit_2_8_Thread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);

protected:
	
	uint8_t clamp_uint8(int x)
	{
		if (x < 0) x = 0;
		else if (x > 0xff/*UINT8_MAX*/) x = 0xff/*UINT8_MAX*/;
		return x;
	}
};

// Scale and convert YU64 input images to various RGB output formats
class CImageScalerConverterYU64ToRGB : public CImageScalerYU64, public CImageConverterYU64ToRGB
{
public:

	CImageScalerConverterYU64ToRGB(IMemAlloc *pMemAlloc,
								   bool sourceColorSpaceIs709 = false,
								   bool sourceImageInterleaved = false) :
		CImageScalerYU64(pMemAlloc),
		CImageConverterYU64ToRGB(sourceColorSpaceIs709,
								 sourceImageInterleaved)
	{
		memset(&mailbox, 0,sizeof(MAILBOX));
	}

	~CImageScalerConverterYU64ToRGB()
	{
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
	}

	// Scale and convert the input image to Adobe Premiere floating-point format
	void ScaleToVUYA_4444_32f(unsigned char *inputBuffer,
							  int inputWidth,
							  int inputHeight,
							  int inputPitch,
							  unsigned char *outputBuffer,
							  int outputWidth,
							  int outputHeight,
							  int outputPitch);

	// Scale and convert the input image to QuickTime with 16 bits per channel
	void ScaleToBGRA64(unsigned char *inputBuffer,
					   int inputWidth,
					   int inputHeight,
					   int inputPitch,
					   unsigned char *outputBuffer,
					   int outputWidth,
					   int outputHeight,
					   int outputPitch,
					   int swap_bytes_flag);

	
#define ScaleToVUYA_4444_32f_ThreadID			1
#define ScaleToBGRA64ThreadID					2
	void ScaleToVUYA_4444_32f_Thread(int index);
	void ScaleToBGRA64Thread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);

};

// Scale and convert NV12 input images to various RGB output formats
class CImageScalerConverterNV12ToRGB : public CImageScalerNV12, public YUVToRGB<uint16_t>
{
public:

	CImageScalerConverterNV12ToRGB(IMemAlloc *pMemAlloc,
								   ColorFlags color_flags = COLOR_FLAGS_VS_709) :
		CImageScalerNV12(pMemAlloc),
		YUVToRGB<uint16_t>(color_flags)
	{
#if 0
		memset(&mailbox, 0,sizeof(MAILBOX));
#endif
	}

	~CImageScalerConverterNV12ToRGB()
	{
#if 0
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
#endif
	}

	// Scale and convert the input image to the 10-bit RGB pixel format for DPX files
	void ScaleToDPX0(void *input_buffer, int input_width, int input_height, int input_pitch,
					 void *output_buffer, int output_width, int output_height, int output_pitch,
					 int swap_bytes_flag);


	// Scale and convert the input image to the 8-bit BGRA pixel format for thumbnails
	void ScaleToBGRA(void *input_buffer, int input_width, int input_height, int input_pitch,
					 void *output_buffer, int output_width, int output_height, int output_pitch,
					 int swap_bytes_flag);

#if 0	
#define ScaleToVUYA_4444_32f_ThreadID			1
#define ScaleToBGRA64ThreadID					2
	void ScaleToVUYA_4444_32f_Thread(int index);
	void ScaleToBGRA64Thread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);
#endif

};

// Scale and convert RGBA input images to various output formats
class CImageScalerConverterRGB32ToQuickTime : public CImageScalerRGB32, public CImageConverterRGB32ToQuickTime
{
public:
	CImageScalerConverterRGB32ToQuickTime(IMemAlloc *pMemAlloc,
										  bool sourceColorSpaceIs709 = false,
										  bool sourceImageInterleaved = false,
										  bool sourceImageIsYInverted = true) :
		CImageScalerRGB32(pMemAlloc),
		CImageConverterRGB32ToQuickTime(sourceColorSpaceIs709,
										sourceImageInterleaved)
	{
		memset(&mailbox, 0,sizeof(MAILBOX));
		flipDuringScale = sourceImageIsYInverted;
	}

	~CImageScalerConverterRGB32ToQuickTime()
	{
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
	}

	// Scale and convert the input image to QuickTime BGRA with 8 bits per channel
	void ScaleToQuickTimeBGRA(unsigned char *inputBuffer,
							  int inputWidth,
							  int inputHeight,
							  int inputPitch,
							  unsigned char *outputBuffer,
							  int outputWidth,
							  int outputHeight,
							  int outputPitch);

	// Scale and convert the input image to QuickTime ARGB with 8 bits per channel
	void ScaleToQuickTimeARGB(unsigned char *inputBuffer,
							  int inputWidth,
							  int inputHeight,
							  int inputPitch,
							  unsigned char *outputBuffer,
							  int outputWidth,
							  int outputHeight,
							  int outputPitch);
	
#define ScaleToQuickTimeBGRAThreadID			1
#define ScaleToQuickTimeARGBThreadID			2
	void ScaleToQuickTimeBGRAThread(int index);
	void ScaleToQuickTimeARGBThread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);
	
	bool flipDuringScale;
	
};

class CImageScalerB64A : public CLanczosScaler
{
public:
	CImageScalerB64A(IMemAlloc *pMemAlloc) :
		CLanczosScaler(pMemAlloc),
		scaleFactors(NULL)
	{
		memset(&mailbox, 0,sizeof(MAILBOX));
	}

	~CImageScalerB64A()
	{
		// Free the scratch buffers used by the scaling routines
		FreeScratchMemory();
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
	}

	// Allocate scratch memory used by the scaling routines
	bool AllocScratchMemory(int outputWidth, int inputHeight)
	{
		// Allocate scratch memory used by all Lanczos scalers
		if (!CLanczosScaler::AllocScratchMemory(outputWidth, inputHeight)) {
			return false;
		}

		//TODO: Need to compute the actual size that must be allocated
		scaleFactors = (short *)Alloc(64000*2);
		if (scaleFactors == NULL) return false;

		// Scratch memory has been successfully allocated
		return true;
	}

	// Free scratch memory used by the scaling routines
	void FreeScratchMemory()
	{
		// Free the scratch memory used by all Lanczos scalers
		CLanczosScaler::FreeScratchMemory();

		if(scaleFactors) {
			Free((char *)scaleFactors);
			scaleFactors = NULL;
		}
	}

	// Scale the rows of ARGB values
	void ScaleRowValues(unsigned char *input, int inputWidth, int inputHeight, int inputPitch,
						unsigned short *output, int outputWidth);

	// Scale the columns of ARGB values
	void ScaleColumnValues(unsigned short *input, int stride,
						   lanczosmix *lmY, int sampleCount,
						   int &A, int &R, int &G, int &B);

#define ScaleRowValuesThreadID		1
#define ScaleToB64AThreadID			2
	void ScaleRowValuesThread(int index);
	void ScaleToB64AThread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);

protected:

	// Scratch memory for use by the interpolator
	short *scaleFactors;

};

// Scale and convert b64a input images to various output formats
class CImageScalerConverterB64A : public CImageScalerB64A, public CImageConverterB64A
{
public:
	CImageScalerConverterB64A(IMemAlloc *pMemAlloc,
							  bool sourceColorSpaceIs709 = false,
							  bool sourceImageInterleaved = false) :
		CImageScalerB64A(pMemAlloc),
		CImageConverterB64A(sourceColorSpaceIs709,
							sourceImageInterleaved)
	{
		memset(&mailbox, 0,sizeof(MAILBOX));
	}

	~CImageScalerConverterB64A()
	{
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
	}
	// Scale and convert the input image to QuickTime with 16 bits per channel
	void ScaleToB64A(unsigned char *inputBuffer,
					 int inputWidth,
					 int inputHeight,
					 int inputPitch,
					 unsigned char *outputBuffer,
					 int outputWidth,
					 int outputHeight,
					 int outputPitch,
					 int byte_swap_flag);

	// Scale and convert the input image to QuickTime with 8 bits per channel
	void ScaleToBGRA(unsigned char *inputBuffer,
					 int inputWidth,
					 int inputHeight,
					 int inputPitch,
					 unsigned char *outputBuffer,
					 int outputWidth,
					 int outputHeight,
					 int outputPitch);

#define ScaleToBGRAThreadID			1
#define ScaleToB64AThreadID			2
	void ScaleToB64AThread(int index);
	void ScaleToBGRAThread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);

};



class CImageScalerRG48 : public CLanczosScaler
{
public:
	CImageScalerRG48(IMemAlloc *pMemAlloc) :
		CLanczosScaler(pMemAlloc),
		scaleFactors(NULL)
	{
		memset(&mailbox, 0,sizeof(MAILBOX));
	}

	~CImageScalerRG48()
	{
		// Free the scratch buffers used by the scaling routines
		FreeScratchMemory();
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
	}

	// Allocate scratch memory used by the scaling routines
	bool AllocScratchMemory(int outputWidth, int inputHeight)
	{
		// Allocate scratch memory used by all Lanczos scalers
		if (!CLanczosScaler::AllocScratchMemory(outputWidth, inputHeight)) {
			return false;
		}

		//TODO: Need to compute the actual size that must be allocated
		scaleFactors = (short *)Alloc(64000*2);
		if (scaleFactors == NULL) return false;

		// Scratch memory has been successfully allocated
		return true;
	}

	// Free scratch memory used by the scaling routines
	void FreeScratchMemory()
	{
		// Free the scratch memory used by all Lanczos scalers
		CLanczosScaler::FreeScratchMemory();

		if(scaleFactors) {
			Free((char *)scaleFactors);
			scaleFactors = NULL;
		}
	}

	// Scale the rows of ARGB values
	void ScaleRowValues(unsigned char *input, int inputWidth, int inputHeight, int inputPitch,
						unsigned short *output, int outputWidth);

	// Scale the columns of ARGB values
	void ScaleColumnValues(unsigned short *input, int stride,
						   lanczosmix *lmY, int sampleCount,
						   int &R, int &G, int &B);

#define ScaleRowValuesThreadID	1
	void ScaleRowValuesThread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);
protected:

	// Scratch memory for use by the interpolator
	short *scaleFactors;

};

// Scale and convert b64a input images to various output formats
class CImageScalerConverterRG48 : public CImageScalerRG48, public CImageConverterRG48
{
public:
	CImageScalerConverterRG48(IMemAlloc *pMemAlloc,
							  bool sourceColorSpaceIs709 = false,
							  bool sourceImageInterleaved = false) :
		CImageScalerRG48(pMemAlloc),
		CImageConverterRG48(sourceColorSpaceIs709,
							sourceImageInterleaved)
	{
		memset(&mailbox, 0,sizeof(MAILBOX));
	}

	~CImageScalerConverterRG48()
	{
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
	}

	// Scale and convert the input image to QuickTime with 16 bits per channel
	void ScaleToRG48(unsigned char *inputBuffer,
					 int inputWidth,
					 int inputHeight,
					 int inputPitch,
					 unsigned char *outputBuffer,
					 int outputWidth,
					 int outputHeight,
					 int outputPitch,
					 int byte_swap_flag,
					 int lobes = 3);

	
#define ScaleToRG48ThreadID	1
	void ScaleToRG48Thread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);
protected:
};


// Scale 8-bit YUV 4:2:2 images to the output image dimensions
class CImageScalerYUV : public CLanczosScaler
{
public:
	
	CImageScalerYUV(IMemAlloc *pMemAlloc) :
		CLanczosScaler(pMemAlloc),
		scalefactorsL(NULL),
		scalefactorsC(NULL)
	{
		memset(&mailbox, 0,sizeof(MAILBOX));
	}
	
	~CImageScalerYUV()
	{
		// Free the scratch buffers used by the scaling routines
		FreeScratchMemory();
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
	}
	
	// Allocate scratch memory used by the scaling routines
	bool AllocScratchMemory(int outputWidth, int inputHeight)
	{
		// Allocate scratch memory used by all Lanczos scalers
		if (!CLanczosScaler::AllocScratchMemory(outputWidth, inputHeight)) {
			return false;
		}
		
		//TODO: Need to compute the actual size that must be allocated
		scalefactorsL = (short *)Alloc(64000*2);
		if (scalefactorsL == NULL) return false;
		
		//TODO: Need to compute the actual size that must be allocated
		scalefactorsC = (short *)Alloc(64000*2);
		if (scalefactorsC == NULL) return false;
		
		// Scratch memory has been successfully allocated
		return true;
	}
	
	// Free scratch memory used by the scaling routines
	void FreeScratchMemory()
	{
		// Free the scratch memory used by all Lanczos scalers
		CLanczosScaler::FreeScratchMemory();
		
		if(scalefactorsL) {
			Free((char *)scalefactorsL);
			scalefactorsL = NULL;
		}
		
		if(scalefactorsC) {
			Free((char *)scalefactorsC);
			scalefactorsC = NULL;
		}
	}
	
	// Scale the rows of luma and chroma
	void ScaleRowValues(unsigned char *input, int inputWidth, int inputHeight, int inputPitch, int outputWidth);
	
	// Scale one row of luma values (skip the chroma values)
	void ScaleRowLuma(unsigned char *inputRow, unsigned char *outputRow, short *scaleFactors);
	
	// Scale one row of chroma values (skip the luma values)
	void ScaleRowChroma(unsigned char *inputRow, unsigned char *outputRow, short *scaleFactors);
	
	// Scale the luma and chroma values in the specified column
	void ScaleColumnValues(unsigned char *input,
						   int stride,
						   lanczosmix *lmY,
						   int sampleCount,
						   int &y1,
						   int &u1,
						   int &y2,
						   int &v1);
	
#define ScaleRowValuesThreadID	1
	void ScaleRowValuesThread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);
protected:
		
	// Scratch memory for use by the interpolator
	short *scalefactorsL;
	short *scalefactorsC;
	
};

// Scale and convert 8-bit YUV 4:2:2 images to various output formats
class CImageScalerConverterYUV : public CImageScalerYUV
{
public:
	
	CImageScalerConverterYUV(IMemAlloc *pMemAlloc,
							 bool sourceColorSpaceIs709 = false,
							 bool sourceImageInterleaved = false) :
		CImageScalerYUV(pMemAlloc)
	{
		memset(&mailbox, 0,sizeof(MAILBOX));
	}
	
	~CImageScalerConverterYUV()
	{
		if(mailbox.pool.thread_count > 0)
		{
			ThreadPoolDelete(&mailbox.pool);
			DeleteLock(&mailbox.lock);
		}
	}
	
	//TODO: Need to choose a scheme for error codes

	// Scale and convert the input image to Adobe Premiere floating-point format
	void ScaleToYUV_422_8u(unsigned char *inputBuffer,
						   int inputWidth,
						   int inputHeight,
						   int inputPitch,
						   unsigned char *outputBuffer,
						   int outputWidth,
						   int outputHeight,
						   int outputPitch);

	// Scale the input image to Avid 8-bit CbYCrY 4:2:2 (CT_UCHAR)
	void ScaleToCbYCrY_422_8u(void *inputBuffer,
							  int inputWidth,
							  int inputHeight,
							  int inputPitch,
							  void *outputBuffer,
							  int outputWidth,
							  int outputHeight,
							  int outputPitch,
							  int row_offset = 0,
							  int column_offset = 0);

	
#define ScaleToYUV_422_8u_ThreadID			1
#define ScaleToCbYCrY_422_8u_ThreadID		2
	void ScaleToYUV_422_8u_Thread(int index);
	void ScaleToCbYCrY_422_8u_Thread(int index);

	MAILBOX mailbox;
	static THREAD_PROC(ScalerProc, lpParam);
};

//
//	C interface to the same functions.
//
typedef struct
{
	int					sampleCount;
	lanczosmix			*lmY;			// array of mix coefficients
} COL_SCALE_FACTORS;

bool ComputeRowScaleFactors( short *rowScaleFactors ,
							int inputWidth, 
							int outputWidth,
							int lobes=3,
							int rowScaleMaxSamples=32000 );
int ComputeColumnScaleFactors( int row, 
							  int inputHeight, 
							  int outputHeight, 
							  int renderFieldType, 
							  lanczosmix **lmY, 
							  int lobes);

// Row scaling - run once per input row
void ScaleRG48Row( unsigned short *input_row_ptr, 
				  unsigned short *output_row_ptr, 
				  short *rowScaleFactors );
void ScaleDPX0Row( unsigned long *input_row_ptr,
				  unsigned long *output_row_ptr,
				  short *rowScaleFactors );
void ScaleYU64RowLuma( unsigned short *input_row_ptr, 
					   unsigned short *output_row_ptr, 
					   short *rowScaleFactors );
void ScaleYU64RowChroma( unsigned short *input_row_ptr, 
						 unsigned short *output_row_ptr, 
						 short *rowScaleFactors );
void ScaleRGB32Row( unsigned char *input_row_ptr, 
					unsigned short *output_row_ptr, 
					short *rowScaleFactors );
void ScaleR408Row( unsigned char *input_row_ptr, 
					unsigned short *output_row_ptr, 
					short *rowScaleFactors );
void ScaleB64ARow( unsigned short *input_row_ptr, 
				   unsigned short *output_row_ptr, 
				   short *rowScaleFactors,
				   int byte_swap_flag);
void ScaleYUVRowLuma( unsigned char *input_row_ptr, 
					  unsigned char *output_row_ptr, 
					  short *rowScaleFactors );
void ScaleYUVRowChroma( unsigned char *input_row_ptr, 
						unsigned char *output_row_ptr, 
						short *rowScaleFactors );

// Column scaling - run once per output row

void ScaleRG48Column( int row, 
					 int outputWidth,
					 unsigned short *input_row_ptr, 
					 unsigned char *output_row_ptr, 
					 COL_SCALE_FACTORS *colScaleFactors, 
					 int byte_swap_flag );
void ScaleDPX0Column( int row,
					 int outputWidth,
					 unsigned long *input_row_ptr,
					 unsigned char *output_row_ptr,
					 COL_SCALE_FACTORS *colScaleFactors,
					 int byte_swap_flag );
void ScaleYU64ToBGRA64Column( int row, 
							  int outputWidth,
							  unsigned char *input_row_ptr, 
							  unsigned char *output_row_ptr, 
							  COL_SCALE_FACTORS *colScaleFactors, 
							  int byte_swap_flag, 
							  int gamma, 
							  void (*GammaFixRGBA)( unsigned char *outputBuffer, int width, int to2point2 ));
void ScaleYU64ToR4FLColumn( int row, 
					  int outputWidth,
					  unsigned char *input_row_ptr, 
					  unsigned char *output_row_ptr, 
					  COL_SCALE_FACTORS *colScaleFactors);

void ScaleRGB32Column( int row, 
					   int outputWidth,
					   unsigned short *input_row_ptr, 
					   unsigned char *output_row_ptr, 
					   COL_SCALE_FACTORS *colScaleFactors, 
					   int byte_swap_flag, 
					   int gamma,
					   void (*GammaFixRGBA)( unsigned char *outputBuffer, int width, int to2point2 ));
void ScaleR408Column( int row, 
					  int outputWidth,
					  unsigned short *input_row_ptr, 
					  unsigned char *output_row_ptr, 
					  COL_SCALE_FACTORS *colScaleFactors);
void ScaleYUVColumn( int row, 
					 int outputWidth,
					 unsigned char *input_row_ptr, 
					 unsigned char *output_row_ptr, 
					 COL_SCALE_FACTORS *colScaleFactors );
void ScaleYU64Column( int row, 
							 int outputWidth,
							 unsigned char *input_row_ptr, 
							 unsigned char *output_row_ptr, 
							 COL_SCALE_FACTORS *colScaleFactors);
void ScaleB64AColumn( int row, 
					  int outputWidth,
					  unsigned short *input_row_ptr, 
					  unsigned char *output_row_ptr, 
					  COL_SCALE_FACTORS *colScaleFactors, 
					  int byte_swap_flag );
void ScaleB64AToBGRAColumn( int row, 
							int outputWidth,
							unsigned short *input_row_ptr, 
							unsigned char *output_row_ptr, 
							COL_SCALE_FACTORS *colScaleFactors, 
							int byte_swap_flag,
							int gamma,
							void (*GammaFixRGBA)( unsigned char *outputBuffer, int width, int to2point2 ));
void ScaleB64AToR4FLColumn( int row, 
							int outputWidth,
							unsigned short *input_row_ptr, 
							unsigned char *output_row_ptr, 
							COL_SCALE_FACTORS *colScaleFactors, 
							int whitepoint);


#if _USE_SIMPLE_NAMES

// Add support for simpler class names
namespace Scaler
{
	typedef CImageScalerConverterNV12ToRGB NV12ToRGB;
	typedef CImageScalerNV12 NV12ToNV12;
	typedef CMemAlloc MemoryAllocator;
}

#endif
