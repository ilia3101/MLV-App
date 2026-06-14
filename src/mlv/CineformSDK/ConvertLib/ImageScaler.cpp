/*! @file ImageScaler.cpp

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

// Define an assert macro that can be controlled in this file
#ifndef ASSERT
#define ASSERT(x)	assert(x)
#endif

#include "ColorFlags.h"
#include "MemAlloc.h"
#include "ImageConverter.h"
#include "ImageScaler.h"
//#include "cpuid.h"

#define CONVERT_709_TO_601	1

#if _WIN32

#if !defined(_OPENMP)
// Turn off warnings about the Open MP pragmas
#pragma warning(disable: 161 4068)
#endif

//TODO: Eliminate warnings about loss of precision in double to float conversion
#pragma warning(disable: 4244 4305)

#endif


#define SYSLOG (0)


#if _WIN32

#include <stdlib.h>

// Use the byte swapping routines defined in the standard library
#if _DEBUG
#define SwapInt16(x) ((((x)&0xff00)>>8)|(((x)&0xff)<<8))
#define SwapInt32(x) ((((x)&0xff000000)>>24)|(((x)&0xff0000)>>8)|(((x)&0xff00)<<8)|(((x)&0xff)<<24))
#else
#define SwapInt16(x)	_byteswap_ushort(x)
#define SwapInt32(x)	_byteswap_ulong(x)
#endif

#elif __APPLE__

#include "CoreFoundation/CoreFoundation.h"

// Use the byte swapping routines from the Core Foundation framework
#define SwapInt16(x)	CFSwapInt16(x)
#define SwapInt32(x)	CFSwapInt32(x)

#else

#include <byteswap.h>

#define SwapInt16(x)	bswap_16(x)
#define SwapInt32(x)	bswap_32(x)

#endif


#ifdef _WIN32

#include "cpuid.h"

int GetProcessorCount()
{
	SYSTEM_INFO cSystem_info;
	GetSystemInfo(&cSystem_info);
	return cSystem_info.dwNumberOfProcessors;
}

#elif __APPLE__

#include <sys/types.h>
#include <sys/sysctl.h>

static int GetProcessorCount()
{
	int maxProcs = 0;
	size_t length = sizeof(maxProcs);
	sysctlbyname("hw.physicalcpu",&maxProcs, &length, NULL, 0);
	
	return maxProcs;
}

#else

#include <stdint.h>
#include <unistd.h>

int GetProcessorCount()
{
	return sysconf(_SC_NPROCESSORS_ONLN);
}

#endif


void CLanczosScaler::ComputeRowScaleFactors(short *scaleFactors, int inputWidth, int outputWidth, int lobes=3)
{
	lanczosmix lmX[1024];
	short *ptr = scaleFactors;

	for (int x = 0; x < outputWidth; x++)
	{
		int nsamples = LanczosCoeff(inputWidth, outputWidth, x, lmX, false, false, lobes);

		*ptr++ = x; // dst line number

		for(int i = 0; i < nsamples; i++)
		{
			*ptr++ = lmX[i].srcline; //src line
			*ptr++ = lmX[i].mixval;  //src mixval
		}

		*ptr++ = -1;	// Next scale factor
	}

	*ptr++ = -1;		//no more factors
}

int CLanczosScaler::ComputeColumnScaleFactors(int row,
											  int inputHeight,
											  int outputHeight,
											  int renderFieldType,
											  lanczosmix *lmY,
											  int lobes = 3)
{
	int samples = 0;

	if (inputHeight == outputHeight)
	{
		//samples = LanczosCoeff(decoder->frame.height, imageRec->dstHeight, row, lmY, false, false, lobes);
	}
	else
	{
		switch(renderFieldType)
		{
		case 0:
			samples = LanczosCoeff(inputHeight, outputHeight, row, lmY, false, false, lobes);
			break;

		case 1:
		case 2:
			samples = LanczosCoeff(inputHeight, outputHeight, row, lmY, false, true, lobes);

			for (int i=0; i<samples; i++)
			{
				lmY[i].srcline *= 2;
				lmY[i].srcline += (row & 1);
			}
			break;
#if 0
		case 2: // this would be required if premiere wasn't doing something weird.

			samples = LanczosCoeff(inputHeight, imageRec->dstHeight, row, lmY, true, true, lobes);
			for(i=0; i<samples; i++)
			{
				lmY[i].srcline *= 2;
				lmY[i].srcline += 1-(row & 1);

			}
			break;
#endif
		}
	}

	return samples;
}


//TODO: Move these packing and unpacking routines into a class as static methods

static void Unpack10( uint32_t packed, int *red, int *green, int *blue)
{
	const int shift = 6;
	
	const int red10 = 22;
	const int green10 = 12;
	const int blue10 = 2;
	
	const uint32_t mask10 = 0x3FF;
	int		swappedPacked;
	
	swappedPacked = SwapInt32(packed);
	
	*red = ((swappedPacked >> red10)&mask10) << shift;
	*green = ((swappedPacked >> green10)&mask10) << shift;
	*blue = ((swappedPacked >> blue10)&mask10) << shift;
}

static uint32_t Pack10( int red, int green, int blue)
{
	const int shift = 6;
	
	const int red10 = 22;
	const int green10 = 12;
	const int blue10 = 2;
	
	const uint32_t mask10 = 0x3FF;
	
	red >>= shift;
	green >>= shift;
	blue >>= shift;
	
	// Pack the color values into a 32 bit word
	uint32_t word = ((red & mask10) << red10) | ((green &mask10) << green10) | ((blue & mask10) << blue10);
	
	// Return the packed word after byte swapping (if necessary)
	return SwapInt32(word);
}	

int /*CLanczosScaler::*/_LanczosCoeff(int inputsize, int outputsize, int line,
								 lanczosmix *lm, bool changefielddominance, bool interlaced, int ilobes)
{
	double x,y,t;
	int j,pos=0;
	float sincxval[200];
	int samples = 0;
	int inputsizefield = inputsize;

	float lobes = (float)ilobes; // was 2.0

	if(outputsize >= inputsize)
	{
		float inv_step = (float)inputsize / (float)outputsize;
		int srcline;
		float dst_pos = (float)line;
		float src_1st;
		float src_1st_whole;
		float src_end;
		float src_end_next;
		float dst_offset;

		if(interlaced)
		{
			dst_pos /= 2.0;
			if(changefielddominance)
				dst_pos -= (1-(line & 1)) ? (inv_step * 0.5) : 0.0;
			else
				dst_pos -= (line & 1) ? (inv_step * 0.5) : 0.0;
		//	dst_pos -= (line & 1) ? 0.5 : 0;

			inputsizefield >>= 1;
		}


		src_1st = inv_step * (dst_pos - lobes);
		src_end = inv_step * (dst_pos + lobes);

		src_1st_whole = floor(src_1st);
		src_end_next = floor(src_end + 0.9999999);

		if(src_1st > 0)
			dst_offset = (src_1st - src_1st_whole);
		else
			dst_offset = fabs(src_1st_whole - src_1st);

//		if(changefielddominance)
//			dst_offset -= (0.5 - inv_step * 0.5);


		t = 0;
		pos = 0;
		for(x=dst_pos-lobes-dst_offset; x<dst_pos+lobes; x+=1.0)
		{
			float sincx = x - (dst_pos);

			if(sincx >= -lobes && sincx <= lobes)
			{
				if(sincx == 0.0)
					y = 1.0;
				else
					y = (sin(sincx*Pi)/(sincx*Pi)) * (sin(sincx*Pi/lobes)/(sincx*Pi/lobes));

				srcline = (int)floor(dst_pos*inv_step + sincx + 0.5);
				if(srcline >= 0 && srcline<inputsizefield)
				{
					t += y;
					sincxval[pos++] = y;
				}
			}
		}

		int tt = 0;
		pos = 0;
		for(x=dst_pos-lobes-dst_offset; x<dst_pos + lobes; x+=1.0)
		{
			float sincx = x - (dst_pos);
			int val;

			if(sincx >= -lobes && sincx <= lobes)
			{
				srcline = (int)floor(dst_pos*inv_step + sincx + 0.5);
				if(srcline >= 0 && srcline<inputsizefield)
				{
					y = sincxval[pos++];
					y = ((y*(float)256)/t);
					if(y > 0.5)
						y += 0.5;
					else
						y -= 0.5;

					assert(INT_MIN <= y && y <= INT_MAX);
					val = (int)y;

					if(val != 0)
					{
						lm[samples].srcline = srcline;
						lm[samples].mixval = val;
						samples++;
					}
					tt += val;
				}
			}
		}

		if(tt != 256)
		{
			int max = 0,maxpos=0;

			for(j=0; j<samples; j++)
			{
				if(lm[j].mixval > max)
				{
					max = lm[j].mixval;
					maxpos = j;
				}
			}

			lm[maxpos].mixval += 256-tt;
		}
	}
	else
	{
		//This helps reduce the number of sample take for extreme downsizing from 1920 to smaller than 480.
		int scaleinput = 1;
		while(inputsize / outputsize > 4)
		{
			scaleinput *= 2;
			inputsize /= 2;
			inputsizefield /= 2;
		};

		{
			float step = (float)outputsize / (float)inputsize;
			float inv_step = (float)inputsize / (float)outputsize;
			int srcline;
			float dst_pos = (float)line;
			float src_1st;
			float src_1st_whole;
			float src_end;
			float src_end_next;
			float dst_offset;

			if(interlaced)
			{
				dst_pos /= 2.0;
				if(changefielddominance)
					dst_pos -= (1-(line & 1)) ? (step * 0.5) : 0.0;
				else
					dst_pos -= (line & 1) ? (step * 0.5) : 0.0;
			//	dst_pos -= (line & 1) ? 0.5 : 0;

				inputsizefield >>= 1;
			}

			src_1st = inv_step * (dst_pos - lobes);
			src_end = inv_step * (dst_pos + lobes);

			src_1st_whole = floor(src_1st);
			src_end_next = floor(src_end + 0.9999999);

			if(src_1st > 0)
				dst_offset = (src_1st - src_1st_whole)*step;
			else
				dst_offset = fabs(src_1st_whole - src_1st)*step;

		//	if(changefielddominance)
		//		dst_offset += step*0.5;


			t = 0;
			pos = 0;
			for(x=dst_pos-lobes-dst_offset; x<dst_pos+lobes; x+=step)
			{
				float sincx = x - (dst_pos);

				if(sincx >= -lobes && sincx <= lobes)
				{
					if(sincx == 0.0)
						y = 1.0;
					else
						y = (sin(sincx*Pi)/(sincx*Pi)) * (sin(sincx*Pi/lobes)/(sincx*Pi/lobes));

					srcline = (int)floor(x*inv_step + 0.5);
					if(srcline >= 0 && srcline<inputsizefield)
					{
						t += y;
						sincxval[pos++] = y;
					}
				}
			}

			int tt = 0,val;
			pos = 0;
			for(x=dst_pos-lobes-dst_offset; x<dst_pos+lobes; x+=step)
			{
				float sincx = x - (dst_pos);

				if(sincx >= -lobes && sincx <= lobes)
				{
					srcline = (int)floor(x*inv_step + 0.5);
					if(srcline >= 0 && srcline<inputsizefield)
					{
						y = sincxval[pos++];

						y = ((y*(float)256)/t);
						if(y > 0.5)
							y += 0.5;
						else
							y -= 0.5;

						assert(INT_MIN <= y && y <= INT_MAX);
						val = (int)y;

						if(val != 0)
						{
							lm[samples].srcline = srcline;
							lm[samples].mixval = val;
							samples++;
						}
						tt += val;
					}
				}
			}

			if(tt != 256)
			{
				int max = 0,maxpos=0;

				for(j=0; j<samples; j++)
				{
					if(lm[j].mixval > max)
					{
						max = lm[j].mixval;
						maxpos = j;
					}
				}

				lm[maxpos].mixval += 256-tt;
			}
		}

		if(scaleinput > 1)
		{
			for(j=0; j<samples; j++)
			{
				lm[j].srcline *= scaleinput;
			}
		}
	}

	return samples;
}
int CLanczosScaler::LanczosCoeff(int inputsize, int outputsize, int line,
									 lanczosmix *lm, bool changefielddominance, bool interlaced, int lobes=3)
{
	return _LanczosCoeff(inputsize, outputsize, line,
						 lm, changefielddominance, interlaced, lobes);
}




THREAD_PROC(CImageScalerYU64::ScalerProc, lpParam)
{
	CImageScalerYU64 *myclass = (CImageScalerYU64 *)lpParam;
	MAILBOX *mailbox = (MAILBOX *)&myclass->mailbox;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&mailbox->pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < mailbox->pool.thread_count);

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&mailbox->pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			for (;;)
			{
				int work_index;

				// Wait for one row from each channel to process
				error = PoolThreadWaitForWork(&mailbox->pool, &work_index, thread_index);
				
				if (error == THREAD_ERROR_OKAY)
				{
					//thread_work->thread_func(work_index);
					switch(mailbox->jobtype)
					{
					case ScaleRowValuesThreadID:
						myclass->ScaleRowValuesThread(work_index);
						break;
					}
				}						
				else
				{
					// No more work to do
					break;
				}
			}

			// Signal that this thread is done
			PoolThreadSignalDone(&mailbox->pool, thread_index);
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}


void CImageScalerYU64::ScaleRowValuesThread(int index)
{
	unsigned short *input = (unsigned short *)mailbox.ptrs[0];
	unsigned short *output = (unsigned short *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	//int inputHeight = mailbox.vars[1];
	int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int yy = index;

	{
		uint16_t *YU64ptr1 = ((uint16_t *)input) + (inputPitch/2) * yy;
		uint16_t *outptr = ((uint16_t *)output) + (outputWidth * 3) * yy;

		// Scale the luma values in this row
		ScaleRowLuma(YU64ptr1, outptr, scalefactorsL);

		// Scale the chroma values in this row
		ScaleRowChroma(YU64ptr1, outptr, scalefactorsC);
	}
}


// Scale the rows of luma and chroma
void CImageScalerYU64::ScaleRowValues(unsigned short *input, int inputWidth, int inputHeight, int inputPitch,
									  unsigned short *output, int outputWidth)
{
	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)input;
	mailbox.ptrs[1] = (void *)output;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.jobtype = ScaleRowValuesThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, inputHeight);
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);
}

// Scale one row of luma values (skip the chroma values)
void CImageScalerYU64::ScaleRowLuma(unsigned short *inputRow,
									unsigned short *outputRow,
									short *scaleFactors)
{
	short *ptrL = scalefactorsL;
	int dstx;
	int srcx;
	int srcmix;
	int tmpY;

	while((dstx = *ptrL++) != -1)
	{
		tmpY = 0;
		while((srcx = *ptrL++) != -1)
		{
			srcmix = *ptrL++;
			tmpY += inputRow[srcx*2]*srcmix; //*2 YUYV
		}
		tmpY >>= 8;
		if(tmpY > 65535) tmpY = 65535;
		if(tmpY < 0) tmpY = 0;

		outputRow[dstx*3] = tmpY; ///now Y..Y..YUVYUV
	}
}

// Scale one row of chroma values (skip the luma values)
void CImageScalerYU64::ScaleRowChroma(unsigned short *inputRow,
									  unsigned short *outputRow,
									  short *scaleFactors)
{
	short *ptrC = scalefactorsC;
	int dstx;
	int srcx;
	int srcmix;
	int tmpU;
	int tmpV;

	while((dstx = *ptrC++) != -1)
	{
		tmpU = tmpV = 0;
		while((srcx = *ptrC++) != -1)
		{
			srcmix = *ptrC++;
			tmpU += inputRow[srcx*4+3]*srcmix; //*4 YUYV
			tmpV += inputRow[srcx*4+1]*srcmix; //*4 YUYV
		}
		tmpU >>= 8;
		if(tmpU > 65535) tmpU = 65535;
		if(tmpU < 0) tmpU = 0;

		tmpV >>= 8;
		if(tmpV > 65535) tmpV = 65535;
		if(tmpV < 0) tmpV = 0;

		outputRow[dstx*3+1] = tmpU;	//now.U..U.YUV
		outputRow[dstx*3+2] = tmpV;	//now..V..VYUV
	}
}

// Scale the luma and chroma values in the specified column
void CImageScalerYU64::ScaleColumnValues(uint16_t *input, int stride,
										 lanczosmix *lmY, int sampleCount,
										 int &Y, int &U, int &V)
{
	// The input stride is in units of luma and chroma values (not in units of bytes)
	uint16_t *YUVptr;

	Y = U = V = 0;

	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		YUVptr = input + stride * lmY[i].srcline;
		Y += *YUVptr++ * mix;
		U += *YUVptr++ * mix;
		V += *YUVptr++ * mix;
	}

	Y >>= 8;
	U >>= 8;
	V >>= 8;

	if (Y > 65535) Y = 65535;
	if (Y < 0) Y = 0;

	if (U > 65535) U = 65535;
	if (U < 0) U = 0;

	if (V > 65535) V = 65535;
	if (V < 0) V = 0;
}


THREAD_PROC(CImageScalerRGB32::ScalerProc, lpParam)
{
	CImageScalerRGB32 *myclass = (CImageScalerRGB32 *)lpParam;
	MAILBOX *mailbox = (MAILBOX *)&myclass->mailbox;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&mailbox->pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < mailbox->pool.thread_count);

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&mailbox->pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			for (;;)
			{
				int work_index;

				// Wait for one row from each channel to process
				error = PoolThreadWaitForWork(&mailbox->pool, &work_index, thread_index);
				
				if (error == THREAD_ERROR_OKAY)
				{
					//thread_work->thread_func(work_index);
					switch(mailbox->jobtype)
					{
					case ScaleRowValuesThreadID:
						myclass->ScaleRowValuesThread(work_index);
						break;
					}
				}						
				else
				{
					// No more work to do
					break;
				}
			}

			// Signal that this thread is done
			PoolThreadSignalDone(&mailbox->pool, thread_index);
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}



void CImageScalerRGB32::ScaleRowValuesThread(int index)
{
	unsigned char *input = (unsigned char *)mailbox.ptrs[0];
	unsigned short *output = (unsigned short *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	//int inputHeight = mailbox.vars[1];
	int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int yy = index;

	{
		int dstx, srcx, srcmix, tmpR, tmpG, tmpB;
		unsigned char *rgbptr;
		unsigned short *outptr;

		short *ptrL = scaleFactors;

		outptr = output + (outputWidth * 3) * yy;
		rgbptr = input + inputPitch * yy;

		while((dstx = *ptrL++) != -1)
		{
			tmpR = tmpG = tmpB = 0;
			while((srcx = *ptrL++) != -1)
			{
				srcmix = *ptrL++;
				tmpB += rgbptr[srcx*4]*srcmix;
				tmpG += rgbptr[srcx*4+1]*srcmix;
				tmpR += rgbptr[srcx*4+2]*srcmix;
			}
			//tmpR >>= 8;
			if(tmpR > 65535) tmpR = 65535;
			if(tmpR < 0) tmpR = 0;

			//tmpG >>= 8;
			if(tmpG > 65535) tmpG = 65535;
			if(tmpG < 0) tmpG = 0;

			//tmpB >>= 8;
			if(tmpB > 65535) tmpB = 65535;
			if(tmpB < 0) tmpB = 0;

			outptr[dstx*3] = tmpR; ///now R..R..RGBRGB
			outptr[dstx*3+1] = tmpG; ///now .G..G.RGBRGB
			outptr[dstx*3+2] = tmpB; ///now ..B..BRGBRGB
		}
	}
}


// Scale the rows of luma and chroma
void CImageScalerRGB32::ScaleRowValues(unsigned char *input, int inputWidth, int inputHeight, int inputPitch,
									  unsigned short *output, int outputWidth)
{
	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)input;
	mailbox.ptrs[1] = (void *)output;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.jobtype = ScaleRowValuesThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, inputHeight);
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);
}


// Scale the RGB values in the specified column
void CImageScalerRGB32::ScaleColumnValues(unsigned short *input, int stride,
										 lanczosmix *lmY, int sampleCount,
										 int &R, int &G, int &B)
{
	unsigned short *RGBptr;

	R = G = B = 0;

	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		RGBptr = input + stride * lmY[i].srcline;
		R += *RGBptr++ * mix;
		G += *RGBptr++ * mix;
		B += *RGBptr++ * mix;
	}

	R >>= 8;
	G >>= 8;
	B >>= 8;

#if 0
	if(R > 65535) R = 65535;
	if(R < 0) R = 0;

	if(G > 65535) G = 65535;
	if(G < 0) G = 0;

	if(B > 65535) B = 65535;
	if(B < 0) B = 0;
#endif
}






THREAD_PROC(CImageScalerConverterYU64ToYUV::ScalerProc, lpParam)
{
	CImageScalerConverterYU64ToYUV *myclass = (CImageScalerConverterYU64ToYUV *)lpParam;
	MAILBOX *mailbox = (MAILBOX *)&myclass->mailbox;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&mailbox->pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < mailbox->pool.thread_count);

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&mailbox->pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			for (;;)
			{
				int work_index;

				// Wait for one row from each channel to process
				error = PoolThreadWaitForWork(&mailbox->pool, &work_index, thread_index);
				
				if (error == THREAD_ERROR_OKAY)
				{
					//thread_work->thread_func(work_index);
					switch(mailbox->jobtype)
					{
					case ScaleToNV12ActiveThreadID:
						myclass->ScaleToNV12ActiveThread(work_index);
						break;
					case ScaleToYU64ThreadID:
						myclass->ScaleToYU64Thread(work_index);
						break;
					case ScaleToCbYCrY_10bit_2_8_ThreadID:
						myclass->ScaleToCbYCrY_10bit_2_8_Thread(work_index);
						break;
					}
				}						
				else
				{
					// No more work to do
					break;
				}
			}

			// Signal that this thread is done
			PoolThreadSignalDone(&mailbox->pool, thread_index);
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}


void CImageScalerConverterYU64ToYUV::ScaleToNV12ActiveThread(int index)
{
	//void *input_buffer = (void *)mailbox.ptrs[0];
	void *output_buffer= (void *)mailbox.ptrs[1];
	//int input_width = mailbox.vars[0];
	int input_height = mailbox.vars[1];
	//int input_pitch = mailbox.vars[2];
	int output_width = mailbox.vars[3];
	int output_height = mailbox.vars[4];
	int output_pitch = mailbox.vars[5];
	//int row_offset = mailbox.vars[6];
	//int column_offset = mailbox.vars[7];
	int first_row = mailbox.vars[8];
	int last_row = mailbox.vars[9];
	const int renderFieldType = 0;
	// Luma and chroma offsets for 709 to 601 color conversion
	const int input_luma_offset = (16 << 8);
	const int input_chroma_offset = (128 << 8);
	const int output_luma_offset = 16;
	const int output_chroma_offset = 128;
	const int scaled_stride = output_width * 3;
	int scaled_height = last_row - first_row + 1;
	int row = index*2+first_row;

	// Scale the input image to the active region in the output frame
	{
#if CONVERT_709_TO_601
		// Chroma values from the previous row (without the chroma offset)
		int16_t U_even[5200], V_even[5200];
#else
		// Chroma values from the previous row
		uint16_t U_even[5200], V_even[5200];
#endif
		uint8_t *luma_row_ptr = (uint8_t *)output_buffer;
		uint8_t *chroma_row_ptr = luma_row_ptr + (output_height * output_pitch);

		int samples = 0;
		lanczosmix lmY[200];

		// Advance to the next row in the luma plane
		luma_row_ptr += output_pitch * row;

		// Advance to the next row in the chroma plane
		chroma_row_ptr += output_pitch * (row/2);
	
		// Compute the coefficients for scaling each column at the current row position
		int line = row - first_row;
		samples = ComputeColumnScaleFactors(line, input_height, scaled_height, renderFieldType, lmY);

		// The horizontally scaled image is the input for this procesing stage
		uint16_t *scaled_column_ptr = (uint16_t *)horizontalscale;

		for (int column = 0; column < output_width; column += 2)
		{
			int Y1, U1, V1;
			int Y2, U2, V2;

			if (input_height == output_height)
			{
				uint16_t *YUV_scaled_ptr = scaled_column_ptr + (scaled_stride * row);
				Y1 = YUV_scaled_ptr[0];
				U1 = YUV_scaled_ptr[1];
				V1 = YUV_scaled_ptr[2];
			}
			else
			{
				ScaleColumnValues(scaled_column_ptr, scaled_stride, lmY, samples, Y1, U1, V1);
			}

			// Advance to the next column of horizontally scaled pixels
			scaled_column_ptr += 3;

			if (input_height == output_height)
			{
				uint16_t *YUV_scaled_ptr = scaled_column_ptr + (scaled_stride * row);
				Y2 = YUV_scaled_ptr[0];
				U2 = YUV_scaled_ptr[1];
				V2 = YUV_scaled_ptr[2];
			}
			else
			{
				ScaleColumnValues(scaled_column_ptr, scaled_stride, lmY, samples, Y2, U2, V2);
			}

			// Advance to the next column of horizontally scaled pixels
			scaled_column_ptr += 3;

#if CONVERT_709_TO_601
			// Convert the luma from 709 to 601 color space

			// Subtract the luma and chroma offsets
			Y1 -= input_luma_offset;
			Y2 -= input_luma_offset;

			U1 -= input_chroma_offset;
			U2 -= input_chroma_offset;
			V1 -= input_chroma_offset;
			V2 -= input_chroma_offset;

			// The coefficients are scaled by 8192
			Y1 = (Y1 << 13) + 815 * U1 + 1568 * V1;
			Y2 = (Y2 << 13) + 815 * U2 + 1568 * V2;

			// Remove the scale factor in the coefficients and reduce to 8 bits
			Y1 >>= (13 + 8);
			Y2 >>= (13 + 8);

			// Add the luma offset for the video safe range
			Y1 += output_luma_offset;
			Y2 += output_luma_offset;
  #if 1
			// Clamp the luma to the 8-bit video safe range
			if (Y1 < 16) Y1 = 16;
			else if (Y1 > 235) Y1 = 235;

			if (Y2 < 16) Y2 = 16;
			else if (Y2 > 235) Y2 = 235;
  #else
			Y1 = clamp_uint8(Y1);
			Y2 = clamp_uint8(Y2);
  #endif
#else
			// Scale the luma to 8 bits
			Y1 = clamp_uint8(Y1 >> 8);
			Y2 = clamp_uint8(Y2 >> 8);
#endif
			// Write the luma values to the luma plane
			luma_row_ptr[column + 0] = Y1;
			luma_row_ptr[column + 1] = Y2;

#if CONVERT_709_TO_601
			// Convert the chroma from 709 to 601 color space

			// The coefficients are scaled by 8192
			int U1_601 = 8110 * U1 - 895 * V1;
			int U2_601 = 8110 * U2 - 895 * V2;
			int V1_601 = 8056 * V1 - 590 * U1;
			int V2_601 = 8056 * V2 - 590 * U2;

			// Remove the scale factor in the coefficients
			U1 = U1_601 >> 13;
			U2 = U2_601 >> 13;
			V1 = V1_601 >> 13;
			V2 = V2_601 >> 13;
#endif
			// Save the chroma values from this row for the next row
			U_even[column + 0] = U1;
			U_even[column + 1] = U2;
			V_even[column + 0] = V1;
			V_even[column + 1] = V2;
		}

		//--------------------------------- 2nd row ---------------------------------------------------
		samples = 0;

		// Advance to the next row in the luma plane
		luma_row_ptr += output_pitch;

		//chroma_row_ptr += output_pitch;
		
		// Compute the coefficients for scaling each column at the current row position
		line = row - first_row + 1;
		samples = ComputeColumnScaleFactors(line, input_height, scaled_height, renderFieldType, lmY);

		// The horizontally scaled image is the input for this procesing stage
		scaled_column_ptr = (uint16_t *)horizontalscale;

		for (int column = 0; column < output_width; column += 2)
		{
			int Y1, U1, V1;
			int Y2, U2, V2;

			if (input_height == output_height)
			{
				uint16_t *YUV_scaled_ptr = scaled_column_ptr + (scaled_stride * (row+1));
				Y1 = YUV_scaled_ptr[0];
				U1 = YUV_scaled_ptr[1];
				V1 = YUV_scaled_ptr[2];
			}
			else
			{
				ScaleColumnValues(scaled_column_ptr, scaled_stride, lmY, samples, Y1, U1, V1);
			}

			// Advance to the next column of horizontally scaled pixels
			scaled_column_ptr += 3;

			if (input_height == output_height)
			{
				uint16_t *YUV_scaled_ptr = scaled_column_ptr + (scaled_stride * (row+1));
				Y2 = YUV_scaled_ptr[0];
				U2 = YUV_scaled_ptr[1];
				V2 = YUV_scaled_ptr[2];
			}
			else
			{
				ScaleColumnValues(scaled_column_ptr, scaled_stride, lmY, samples, Y2, U2, V2);
			}

			// Advance to the next column of horizontally scaled pixels
			scaled_column_ptr += 3;

#if CONVERT_709_TO_601
			// Convert the luma from 709 to 601 color space

			// Subtract the luma and chroma offsets
			Y1 -= input_luma_offset;
			Y2 -= input_luma_offset;

			U1 -= input_chroma_offset;
			U2 -= input_chroma_offset;
			V1 -= input_chroma_offset;
			V2 -= input_chroma_offset;

			// The coefficients are scaled by 8192
			Y1 = (Y1 << 13) + 815 * U1 + 1568 * V1;
			Y2 = (Y2 << 13) + 815 * U2 + 1568 * V2;

			// Remove the scale factor in the coefficients and reduce to 8 bits
			Y1 >>= (13 + 8);
			Y2 >>= (13 + 8);

			// Add the luma offset for the video safe range
			Y1 += output_luma_offset;
			Y2 += output_luma_offset;
  #if 1
			// Clamp the luma to the 8-bit video safe range
			if (Y1 < 16) Y1 = 16;
			else if (Y1 > 235) Y1 = 235;

			if (Y2 < 16) Y2 = 16;
			else if (Y2 > 235) Y2 = 235;
  #else
			Y1 = clamp_uint8(Y1);
			Y2 = clamp_uint8(Y2);
  #endif
#else
			// Scale the luma to 8 bits
			Y1 = clamp_uint8(Y1 >> 8);
			Y2 = clamp_uint8(Y2 >> 8);
#endif
			// Write the luma values to the luma plane
			luma_row_ptr[column + 0] = Y1;
			luma_row_ptr[column + 1] = Y2;

		
#if CONVERT_709_TO_601
			// Convert the chroma from 709 to 601 color spaces

			// The coefficients are scaled by 8192
			int U1_601 = 8110 * U1 - 895 * V1;
			int U2_601 = 8110 * U2 - 895 * V2;
			int V1_601 = 8056 * V1 - 590 * U1;
			int V2_601 = 8056 * V2 - 590 * U2;

			// Remove the scale factor in the coefficients
			U1 = U1_601 >> 13;
			U2 = U2_601 >> 13;
			V1 = V1_601 >> 13;
			V2 = V2_601 >> 13;

			// Get the converted chroma values from the previous row
			int U1_even = U_even[column + 0];
			int U2_even = U_even[column + 1];
			int V1_even = V_even[column + 0];
			int V2_even = V_even[column + 1];

			// Downsample the chroma to 4:2:0 by averaging the chroma values at the four nearest neighbors
			U1 = U1 + U2 + U1_even + U2_even;
			V1 = V1 + V2 + V1_even + V2_even;

			//TODO: Replace the 2x2 average with a 6 tap filter

			// Division by four to compute the average is folded into the scaling operation
			U1 = U1 >> 10;
			V1 = V1 >> 10;

			// Add the chroma offset
			U1 += output_chroma_offset;
			V1 += output_chroma_offset;
  #if 1
			// Clamp the chroma to the 8-bit video safe range for standard definition
			if (U1 < 16) U1 = 16;
			else if (U1 > 240) U1 = 240;

			if (V1 < 16) V1 = 16;
			else if (V1 > 240) V1 = 240;
  #else
			U1 = clamp_uint8(U1);
			V1 = clamp_uint8(V1);
  #endif
#else
			// Downsample the chroma to 4:2:0 by averaging the chroma values at the four nearest neighbors
			U1 = (uint32_t)U1 + (uint32_t)U2 + (uint32_t)U_even[column + 0] + (uint32_t)U_even[column + 1];
			V1 = (uint32_t)V1 + (uint32_t)V2 + (uint32_t)V_even[column + 0] + (uint32_t)V_even[column + 1];

			// Division by four to compute the average is folded into the scaling operation
			U1 = clamp_uint8(U1 >> 10);
			V1 = clamp_uint8(V1 >> 10);
#endif
			// Write the chroma values to the chroma plane
			chroma_row_ptr[column + 0] = U1;
			chroma_row_ptr[column + 1] = V1;
		}
	}
}




/*!
	@brief Scale an image in YU64 format to NV12

	Scale an image in YU64 to the NV12 pixel format used for
	MPEG-2 video encoding.  The row offset is used to define the
	letterbox region, the column offset has not been implemented.
*/
void
CImageScalerConverterYU64ToYUV::ScaleToNV12(void *input_buffer,
											int input_width,
											int input_height,
											int input_pitch,
											void *output_buffer,
											int output_width,
											int output_height,
											int output_pitch,
											int row_offset,
											int column_offset)
{
	//TODO: Need to choose a scheme for error codes

	//const int renderFieldType = 0;

	// Luma and chroma offsets for 709 to 601 color conversion
	//const int input_luma_offset = (16 << 8);
	//const int input_chroma_offset = (128 << 8);
	//const int output_luma_offset = 16;
	//const int output_chroma_offset = 128;

	// Compute the letterbox region
	int first_row = row_offset;
	int last_row = output_height - row_offset - 1;
	//int scaled_height = last_row - first_row + 1;

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(output_width, input_height)) {
		return;
	}

	// One chroma pair for every two luma values (4:2:2 sampling)
	const int luma_width = input_width;
	const int chroma_width = (input_width >> 1);

	ComputeRowScaleFactors(scalefactorsL, luma_width, output_width, 2);
	ComputeRowScaleFactors(scalefactorsC, chroma_width, output_width, 2);

	// The horizontally scaled luma and chroma have 4:4:4 sampling
	ScaleRowValues((uint16_t *)input_buffer, input_width, input_height, input_pitch, horizontalscale, output_width);

	//TODO: Preallocate a buffer large enough for the chroma values from the previous row
	assert(output_width <= 5200);

	// The plane of interleaved chroma follows the luma plane and has the same pitch
	// The chroma plane is half as high as the luma plane

	// The horizontally scaled YUV intermediate image uses 4:4:4 sampling
	//const int scaled_stride = output_width * 3;

	// Fill the upper letterbox region
	//#pragma omp parallel for
	for (int row = 0; row < first_row; row++)
	{
		uint8_t *luma_row_ptr = (uint8_t *)output_buffer;
		uint8_t *chroma_row_ptr = luma_row_ptr + (output_height * output_pitch);

		// Advance to the next row in the luma plane
		luma_row_ptr += output_pitch * row;

		// Advance to the next row in the chroma plane
		chroma_row_ptr += output_pitch * (row/2);

		uint8_t Y1 = 0;
		uint8_t Y2 = 0;
		uint8_t U1 = 128;
		uint8_t V1 = 128;

		for (int column = 0; column < output_width; column += 2)
		{
			// Write the luma values to the luma plane
			luma_row_ptr[column + 0] = Y1;
			luma_row_ptr[column + 1] = Y2;

			// Write the chroma values to the chroma plane
			chroma_row_ptr[column + 0] = U1;
			chroma_row_ptr[column + 1] = V1;
		}
	}


	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)input_buffer;
	mailbox.ptrs[1] = (void *)output_buffer;
	mailbox.vars[0] = input_width;
	mailbox.vars[1] = input_height;
	mailbox.vars[2] = input_pitch;
	mailbox.vars[3] = output_width;
	mailbox.vars[4] = output_height;
	mailbox.vars[5] = output_pitch;
	mailbox.vars[6] = row_offset;
	mailbox.vars[7] = column_offset;
	mailbox.vars[8] = first_row;
	mailbox.vars[9] = last_row;
	mailbox.jobtype = ScaleToNV12ActiveThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, (last_row-first_row)/2 );
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);



	// Fill the lower letterbox region
	//#pragma omp parallel for
	for (int row = last_row + 1; row < output_height; row++)
	{
		uint8_t *luma_row_ptr = (uint8_t *)output_buffer;
		uint8_t *chroma_row_ptr = luma_row_ptr + (output_height * output_pitch);

		// Advance to the next row in the luma plane
		luma_row_ptr += output_pitch * row;

		// Advance to the next row in the chroma plane
		chroma_row_ptr += output_pitch * (row/2);

		uint8_t Y1 = 0;
		uint8_t Y2 = 0;
		uint8_t U1 = 128;
		uint8_t V1 = 128;

		for (int column = 0; column < output_width; column += 2)
		{
			// Write the luma values to the luma plane
			luma_row_ptr[column + 0] = Y1;
			luma_row_ptr[column + 1] = Y2;

			// Write the chroma values to the chroma plane
			chroma_row_ptr[column + 0] = U1;
			chroma_row_ptr[column + 1] = V1;
		}
	}

	// Free the scratch buffers used for interpolation
	FreeScratchMemory();
}



void CImageScalerConverterYU64ToYUV::ScaleToYU64Thread(int index)
{
	//void *input_buffer = (void *)mailbox.ptrs[0];
	void *output_buffer= (void *)mailbox.ptrs[1];
	//int input_width = mailbox.vars[0];
	int input_height = mailbox.vars[1];
	//int input_pitch = mailbox.vars[2];
	int output_width = mailbox.vars[3];
	int output_height = mailbox.vars[4];
	int output_pitch = mailbox.vars[5];

	const int renderFieldType = 0;
	// One chroma pair for every two luma values (4:2:2 sampling)
	//const int luma_width = input_width;
	//const int chroma_width = (input_width >> 1);
	const int scaled_stride = output_width * 3;

	int row = index;

	{
		int samples = 0;
		lanczosmix lmY[200];
		
		uint16_t *YU64_row_ptr = (uint16_t *)output_buffer;
		YU64_row_ptr += row * output_pitch / 2;

		// Compute the coefficients for scaling each column at the current row position
		samples = ComputeColumnScaleFactors(row, input_height, output_height, renderFieldType, lmY);

		// The horizontally scaled image is the input for this procesing stage
		uint16_t *scaled_column_ptr = (uint16_t *)horizontalscale;

		for (int column = 0; column < output_width; column += 2)
		{
			int Y1, U1, V1;
			int Y2, U2, V2;

			if (input_height == output_height)
			{
				uint16_t *YUV_scaled_ptr = scaled_column_ptr + (scaled_stride * row);
				Y1 = YUV_scaled_ptr[0];
				U1 = YUV_scaled_ptr[1];
				V1 = YUV_scaled_ptr[2];
			}
			else
			{
				ScaleColumnValues(scaled_column_ptr, scaled_stride, lmY, samples, Y1, U1, V1);
			}

			// Advance to the next column of horizontally scaled pixels
			scaled_column_ptr += 3;

			if (input_height == output_height)
			{
				uint16_t *YUV_scaled_ptr = scaled_column_ptr + (scaled_stride * row);
				Y2 = YUV_scaled_ptr[0];
				U2 = YUV_scaled_ptr[1];
				V2 = YUV_scaled_ptr[2];
			}
			else
			{
				ScaleColumnValues(scaled_column_ptr, scaled_stride, lmY, samples, Y2, U2, V2);
			}

			// Advance to the next column of horizontally scaled pixels
			scaled_column_ptr += 3;

			YU64_row_ptr[column*2 + 0] = Y1;
			YU64_row_ptr[column*2 + 1] = V1;
			YU64_row_ptr[column*2 + 2] = Y2;
			YU64_row_ptr[column*2 + 3] = U1;
		}
	}
}

void
CImageScalerConverterYU64ToYUV::ScaleToYU64(void *input_buffer,
											int input_width,
											int input_height,
											int input_pitch,
											void *output_buffer,
											int output_width,
											int output_height,
											int output_pitch)
{
	//TODO: Need to choose a scheme for error codes

	//const int renderFieldType = 0;

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(output_width, input_height)) {
		return;
	}

	// One chroma pair for every two luma values (4:2:2 sampling)
	const int luma_width = input_width;
	const int chroma_width = (input_width >> 1);

	ComputeRowScaleFactors(scalefactorsL, luma_width, output_width);
	ComputeRowScaleFactors(scalefactorsC, chroma_width, output_width);

	// The horizontally scaled luma and chroma have 4:4:4 sampling
	ScaleRowValues((uint16_t *)input_buffer, input_width, input_height, input_pitch, horizontalscale, output_width);

	// The horizontally scaled YUV intermediate image uses 4:4:4 sampling
	//const int scaled_stride = output_width * 3;

	//TODO: Process two rows per iteration so that OpenMP can be enabled

	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)input_buffer;
	mailbox.ptrs[1] = (void *)output_buffer;
	mailbox.vars[0] = input_width;
	mailbox.vars[1] = input_height;
	mailbox.vars[2] = input_pitch;
	mailbox.vars[3] = output_width;
	mailbox.vars[4] = output_height;
	mailbox.vars[5] = output_pitch;
	mailbox.jobtype = ScaleToYU64ThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, output_height );
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);

	// Free the scratch buffers used for interpolation
	FreeScratchMemory();
}



void CImageScalerConverterYU64ToYUV::ScaleToCbYCrY_10bit_2_8_Thread(int index)
{
	//void *input_buffer = (void *)mailbox.ptrs[0];
	void *output_buffer= (void *)mailbox.ptrs[1];
	//int input_width = mailbox.vars[0];
	int input_height = mailbox.vars[1];
	//int input_pitch = mailbox.vars[2];
	int output_width = mailbox.vars[3];
	int output_height = mailbox.vars[4];
	//int output_pitch = mailbox.vars[5];
	//int row_offset = mailbox.vars[6];
	//int column_offset = mailbox.vars[7];
	int first_row = mailbox.vars[8];
	int last_row = mailbox.vars[9];
	// Luma and chroma offsets for 709 to 601 color conversion

	const int renderFieldType = 0;

	// Compute the size of the upper and lower output planes
	//size_t lower_size = 2 * (output_width * output_height);
	size_t upper_size = (output_width * output_height) / 2;
	//size_t total_size = upper_size + lower_size;

	// Compute the location of the upper and lower output planes
	uint8_t *upper_plane = reinterpret_cast<uint8_t *>(output_buffer);
	uint8_t *lower_plane = upper_plane + upper_size;

	// Compute the pitch of the upper and lower output planes
	int upper_row_pitch = output_width / 2;
	int lower_row_pitch = output_width * 2;

	// Compute the letterbox region
	int scaled_height = last_row - first_row + 1;

	// One chroma pair for every two luma values (4:2:2 sampling)
	//const int luma_width = input_width;
	//const int chroma_width = (input_width >> 1);

	// The horizontally scaled YUV intermediate image uses 4:4:4 sampling
	const int scaled_stride = output_width * 3;

	int row = index+first_row;

	// Scale the input image into the active area of the output frame
	{
		int samples = 0;
		lanczosmix lmY[200];
		
		//uint16_t *YU64_row_ptr = (uint16_t *)output_buffer;
		//YU64_row_ptr += row * output_pitch / 2;

		// Compute the coefficients for scaling each column at the current row position
		int line = row - first_row;
		samples = ComputeColumnScaleFactors(line, input_height, scaled_height, renderFieldType, lmY);

		// The horizontally scaled image is the input for this procesing stage
		uint16_t *scaled_column_ptr = (uint16_t *)horizontalscale;

		uint8_t *upper_row_ptr = upper_plane + row * upper_row_pitch;
		uint8_t *lower_row_ptr = lower_plane + row * lower_row_pitch;

		for (int column = 0; column < output_width; column += 2)
		{
			int Y1, U1, V1;
			int Y2, U2, V2;

			if (input_height == scaled_height)
			{
				uint16_t *YUV_scaled_ptr = scaled_column_ptr + (scaled_stride * row);
				Y1 = YUV_scaled_ptr[0];
				U1 = YUV_scaled_ptr[1];
				V1 = YUV_scaled_ptr[2];
			}
			else
			{
				ScaleColumnValues(scaled_column_ptr, scaled_stride, lmY, samples, Y1, U1, V1);
			}

			// Advance to the next column of horizontally scaled pixels
			scaled_column_ptr += 3;

			if (input_height == scaled_height)
			{
				uint16_t *YUV_scaled_ptr = scaled_column_ptr + (scaled_stride * row);
				Y2 = YUV_scaled_ptr[0];
				U2 = YUV_scaled_ptr[1];
				V2 = YUV_scaled_ptr[2];
			}
			else
			{
				ScaleColumnValues(scaled_column_ptr, scaled_stride, lmY, samples, Y2, U2, V2);
			}

			// Advance to the next column of horizontally scaled pixels
			scaled_column_ptr += 3;

			//YU64_row_ptr[2 * column + 0] = Y1;
			//YU64_row_ptr[2 * column + 1] = V1;
			//YU64_row_ptr[2 * column + 2] = Y2;
			//YU64_row_ptr[2 * column + 3] = U1;

			// Scale the pixels to 10 bits and split into the upper and lower parts
			uint16_t Y1_upper, Cr_upper, Y2_upper, Cb_upper;
			uint16_t Y1_lower, Cr_lower, Y2_lower, Cb_lower;
			uint16_t upper;

			// Average the chroma values
			int Cr = (V1 + V2) / 2;
			int Cb = (U1 + U2) / 2;

			// Process Y1
			Y1_upper = (Y1 >> 6) & 0x03;		// Least significant 2 bits
			Y1_lower = (Y1 >> 8) & 0xFF;		// Most significant 8 bits

			// Process Cr
			Cr_upper = (Cr >> 6) & 0x03;		// Least significant 2 bits
			Cr_lower = (Cr >> 8) & 0xFF;		// Most significant 8 bits

			// Process Y2
			Y2_upper = (Y2 >> 6) & 0x03;		// Least significant 2 bits
			Y2_lower = (Y2 >> 8) & 0xFF;		// Most significant 8 bits

			// Process Cb
			Cb_upper = (Cb >> 6) & 0x03;		// Least significant 2 bits
			Cb_lower = (Cb >> 8) & 0xFF;		// Most significant 8 bits

			// Pack the least significant bits into a byte
			upper = (Cb_upper << 6) | (Y1_upper << 4) | (Cr_upper << 2) | Y2_upper;

			// Write the byte to the upper plane in the output image
			upper_row_ptr[column/2] = upper;

			// Output the most significant bits of each component to the lower plane
			lower_row_ptr[2 * column + 0] = Cb_lower;
			lower_row_ptr[2 * column + 1] = Y1_lower;
			lower_row_ptr[2 * column + 2] = Cr_lower;
			lower_row_ptr[2 * column + 3] = Y2_lower;
		}
	}

}



/*!
	@Brief Scale 16-bit YU64 to the Avid 10-bit 2.8 format

	The Avid 10-bit 2.8 format splits the 10-bit pixels into two planes,
	with the most significant 8 bits in the lower plan and the least
	significant 2 bits in the upper plane.

	The row offset is used to specify letterboxing.

	The column offset has not been implemented.
*/
void
CImageScalerConverterYU64ToYUV::ScaleToCbYCrY_10bit_2_8(void *input_buffer,
														int input_width,
														int input_height,
														int input_pitch,
														void *output_buffer,
														int output_width,
														int output_height,
														int output_pitch,
														int row_offset,
														int column_offset)
{
	//TODO: Need to choose a scheme for error codes

	//const int renderFieldType = 0;

	// Compute the size of the upper and lower output planes
	//size_t lower_size = 2 * (output_width * output_height);
	size_t upper_size = (output_width * output_height) / 2;
	//size_t total_size = upper_size + lower_size;

	// Compute the location of the upper and lower output planes
	uint8_t *upper_plane = reinterpret_cast<uint8_t *>(output_buffer);
	uint8_t *lower_plane = upper_plane + upper_size;

	// Compute the pitch of the upper and lower output planes
	int upper_row_pitch = output_width / 2;
	int lower_row_pitch = output_width * 2;

	// Compute the letterbox region
	int first_row = row_offset;
	int last_row = output_height - row_offset - 1;
	//int scaled_height = last_row - first_row + 1;

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(output_width, input_height)) {
		return;
	}

	// One chroma pair for every two luma values (4:2:2 sampling)
	const int luma_width = input_width;
	const int chroma_width = (input_width >> 1);

	ComputeRowScaleFactors(scalefactorsL, luma_width, output_width);
	ComputeRowScaleFactors(scalefactorsC, chroma_width, output_width);

	// The horizontally scaled luma and chroma have 4:4:4 sampling
	ScaleRowValues((uint16_t *)input_buffer, input_width, input_height, input_pitch, horizontalscale, output_width);

	// The horizontally scaled YUV intermediate image uses 4:4:4 sampling
	//const int scaled_stride = output_width * 3;

	// Fill the upper letterbox region
	//#pragma omp parallel for
	for (int row = 0; row < first_row; row++)
	{
		uint8_t *upper_row_ptr = upper_plane + row * upper_row_pitch;
		uint8_t *lower_row_ptr = lower_plane + row * lower_row_pitch;

		uint16_t Cb_lower = 128;
		uint16_t Y1_lower = 0;
		uint16_t Cr_lower = 128;
		uint16_t Y2_lower = 0;
		uint16_t upper = 0;

		for (int column = 0; column < output_width; column += 2)
		{
			// Write the byte to the upper plane in the output image
			upper_row_ptr[column/2] = upper;

			// Output the most significant bits of each component to the lower plane
			lower_row_ptr[2 * column + 0] = Cb_lower;
			lower_row_ptr[2 * column + 1] = Y1_lower;
			lower_row_ptr[2 * column + 2] = Cr_lower;
			lower_row_ptr[2 * column + 3] = Y2_lower;
		}
	}

	
	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)input_buffer;
	mailbox.ptrs[1] = (void *)output_buffer;
	mailbox.vars[0] = input_width;
	mailbox.vars[1] = input_height;
	mailbox.vars[2] = input_pitch;
	mailbox.vars[3] = output_width;
	mailbox.vars[4] = output_height;
	mailbox.vars[5] = output_pitch;
	mailbox.vars[6] = row_offset;
	mailbox.vars[7] = column_offset;
	mailbox.vars[8] = first_row;
	mailbox.vars[9] = last_row;
	mailbox.jobtype = ScaleToCbYCrY_10bit_2_8_ThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, (last_row+1-first_row) );
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);


	
	// Fill the lower letterbox region
	//#pragma omp parallel for
	for (int row = last_row + 1; row < output_height; row++)
	{
		uint8_t *upper_row_ptr = upper_plane + row * upper_row_pitch;
		uint8_t *lower_row_ptr = lower_plane + row * lower_row_pitch;

		uint16_t Cb_lower = 128;
		uint16_t Y1_lower = 0;
		uint16_t Cr_lower = 128;
		uint16_t Y2_lower = 0;
		uint16_t upper = 0;

		for (int column = 0; column < output_width; column += 2)
		{
			// Write the byte to the upper plane in the output image
			upper_row_ptr[column/2] = upper;

			// Output the most significant bits of each component to the lower plane
			lower_row_ptr[2 * column + 0] = Cb_lower;
			lower_row_ptr[2 * column + 1] = Y1_lower;
			lower_row_ptr[2 * column + 2] = Cr_lower;
			lower_row_ptr[2 * column + 3] = Y2_lower;
		}
	}

	// Free the scratch buffers used for interpolation
	FreeScratchMemory();
}



THREAD_PROC(CImageScalerConverterYU64ToRGB::ScalerProc, lpParam)
{
	CImageScalerConverterYU64ToRGB *myclass = (CImageScalerConverterYU64ToRGB *)lpParam;
	MAILBOX *mailbox = (MAILBOX *)&myclass->mailbox;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&mailbox->pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < mailbox->pool.thread_count);

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&mailbox->pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			for (;;)
			{
				int work_index;

				// Wait for one row from each channel to process
				error = PoolThreadWaitForWork(&mailbox->pool, &work_index, thread_index);
				
				if (error == THREAD_ERROR_OKAY)
				{
					//thread_work->thread_func(work_index);
					switch(mailbox->jobtype)
					{
					case ScaleToVUYA_4444_32f_ThreadID:
						myclass->ScaleToVUYA_4444_32f_Thread(work_index);
						break;
					case ScaleToBGRA64ThreadID:
						myclass->ScaleToBGRA64Thread(work_index);
						break;
					}
				}						
				else
				{
					// No more work to do
					break;
				}
			}

			// Signal that this thread is done
			PoolThreadSignalDone(&mailbox->pool, thread_index);
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}


void CImageScalerConverterYU64ToRGB::ScaleToVUYA_4444_32f_Thread(int index)
{
	//unsigned char *inputBuffer = (unsigned char *)mailbox.ptrs[0];
	unsigned char *outputBuffer = (unsigned char *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	int inputHeight = mailbox.vars[1];
	//int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int outputHeight = mailbox.vars[4];
	int outputPitch = mailbox.vars[5];
	int yy = index;
	
	unsigned char *ptr = outputBuffer;
	// The image is flipped
	ptr += outputPitch * (outputHeight - 1);

	int renderFieldType = 0;

	{
		//int i;
		int x,samples = 0;
		lanczosmix lmY[200];
		unsigned char *localptr = ptr;
		float *outYUVA128;

		localptr -= (outputPitch * yy);
		outYUVA128 = (float *)localptr;

		samples = ComputeColumnScaleFactors(yy, inputHeight, outputHeight, renderFieldType, lmY);

		unsigned short *sourceYUV = (unsigned short *)horizontalscale;
		unsigned short *YUVptr;
		int scaledstride = outputWidth * 3;

		for(x=0;x<outputWidth; x++)
		{
			float y, u, v;
			//float u1,v1;
			int Y, U, V;
			//int Y2, U2, V2;

			if (inputHeight == outputHeight)
			{
				YUVptr = sourceYUV + scaledstride * yy;
				Y = *YUVptr++;
				U = *YUVptr++;
				V = *YUVptr++;
			}
			else
			{
				ScaleColumnValues(sourceYUV, scaledstride, lmY, samples, Y, U, V);
			}

			// Advance to the next column
			sourceYUV+=3;

			// Convert the YU64 pixel to Adobe Premiere floating-point format
			ConvertToVUYA_4444_32f(Y, U, V, y, u, v);

			*outYUVA128++ = v;
			*outYUVA128++ = u;
			*outYUVA128++ = y;
			*outYUVA128++ = 1.0;
		}
	}
}



//TODO: Need to test this routine
void CImageScalerConverterYU64ToRGB::ScaleToVUYA_4444_32f(unsigned char *inputBuffer,
														  int inputWidth,
														  int inputHeight,
														  int inputPitch,
														  unsigned char *outputBuffer,
														  int outputWidth,
														  int outputHeight,
														  int outputPitch)
{
	//unsigned short *YU64ptr1;

	//unsigned char *base = inputBuffer;
	//int renderFieldType = 0;
	unsigned char *ptr = outputBuffer;
	//unsigned char *fieldbase = base;

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(outputWidth, inputHeight)) {
		return;
	}
	//TODO: Need to choose a scheme for error codes

	//int Lsamples,Csamples;
	//int i,x;
	//int yy;

	ComputeRowScaleFactors(scalefactorsL, inputWidth, outputWidth);
	ComputeRowScaleFactors(scalefactorsC, (inputWidth>>1), outputWidth);

	ScaleRowValues((unsigned short *)inputBuffer, inputWidth, inputHeight, inputPitch, horizontalscale, outputWidth);

	// The image is flipped
	ptr += outputPitch * (outputHeight - 1);


	
	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)inputBuffer;
	mailbox.ptrs[1] = (void *)outputBuffer;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.vars[4] = outputHeight;
	mailbox.vars[5] = outputPitch;
	mailbox.jobtype = ScaleToVUYA_4444_32f_ThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, (outputHeight) );
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);



	// Free the scratch buffers used for interpolation
	FreeScratchMemory();

	//return imNoErr;
}



void CImageScalerConverterYU64ToRGB::ScaleToBGRA64Thread(int index)
{
	//unsigned char *inputBuffer = (unsigned char *)mailbox.ptrs[0];
	unsigned char *outputBuffer = (unsigned char *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	int inputHeight = mailbox.vars[1];
	//int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int outputHeight = mailbox.vars[4];
	int outputPitch = mailbox.vars[5];
	int swap_bytes_flag = mailbox.vars[6];
	int yy = index;
	
	int renderFieldType = 0;

	{
		//int i;
		int x,samples = 0;
		lanczosmix lmY[200];
		//unsigned char *localptr = ptr;

		unsigned short *outptr = (unsigned short *)(outputBuffer + (outputPitch * yy));

		samples = ComputeColumnScaleFactors(yy, inputHeight, outputHeight, renderFieldType, lmY);

		unsigned short *sourceYUV = (unsigned short *)horizontalscale;
		unsigned short *YUVptr;
		int scaledstride = outputWidth * 3;

		for(x = 0; x < outputWidth; x++)
		{
			int y, u, v;
			int r, g, b;

			if (inputHeight == outputHeight)
			{
				YUVptr = sourceYUV + scaledstride * yy;
				y = *YUVptr++;
				u = *YUVptr++;
				v = *YUVptr++;
			}
			else
			{
				ScaleColumnValues(sourceYUV, scaledstride, lmY, samples, y, u, v);
			}

			// Advance to the next column
			sourceYUV += 3;

			// Convert the YU64 pixel to QuickTime BGRA64
			ConvertToBGRA64(y, v, u, r, g, b);

			if (swap_bytes_flag)
			{
				// Swap the bytes in each component of the output pixel
				*(outptr++) = SwapInt16(alpha);
				*(outptr++) = SwapInt16(r);
				*(outptr++) = SwapInt16(g);
				*(outptr++) = SwapInt16(b);
			}
			else
			{
				// Output the BGRA64 value without swapping bytes
				*(outptr++) = alpha;
				*(outptr++) = r;
				*(outptr++) = g;
				*(outptr++) = b;
			}
		}
	}
}

// Scale the YU64 image and convert to 16-bit RGBA for After Effects on the Macintosh
void CImageScalerConverterYU64ToRGB::ScaleToBGRA64(unsigned char *inputBuffer,
												   int inputWidth,
												   int inputHeight,
												   int inputPitch,
												   unsigned char *outputBuffer,
												   int outputWidth,
												   int outputHeight,
												   int outputPitch,
												   int swap_bytes_flag)
{
	//unsigned short *YU64ptr1;

	//const int alpha = USHRT_MAX;

	unsigned char *base = inputBuffer;
	//int renderFieldType = 0;
	//unsigned char *ptr = outputBuffer;
	unsigned char *fieldbase = base;

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(outputWidth, inputHeight)) {
		return;
	}

	//TODO: Need to choose a scheme for error codes

	//int Lsamples,Csamples;
	//int i,x;
	//int yy;

	ComputeRowScaleFactors(scalefactorsL, inputWidth, outputWidth);
	ComputeRowScaleFactors(scalefactorsC, (inputWidth>>1), outputWidth);

	//unsigned short *outptr = (unsigned short *)horizontalscale;
	//YU64ptr1 = (unsigned short *)fieldbase;

	ScaleRowValues((unsigned short*)fieldbase, inputWidth, inputHeight, inputPitch, horizontalscale, outputWidth);

	
	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)inputBuffer;
	mailbox.ptrs[1] = (void *)outputBuffer;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.vars[4] = outputHeight;
	mailbox.vars[5] = outputPitch;
	mailbox.vars[6] = swap_bytes_flag;
	mailbox.jobtype = ScaleToBGRA64ThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, (outputHeight) );
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);



	// Free the scratch buffers used for interpolation
	FreeScratchMemory();

	//return imNoErr;
}


THREAD_PROC(CImageScalerConverterRGB32ToQuickTime::ScalerProc, lpParam)
{
	CImageScalerConverterRGB32ToQuickTime *myclass = (CImageScalerConverterRGB32ToQuickTime *)lpParam;
	MAILBOX *mailbox = (MAILBOX *)&myclass->mailbox;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&mailbox->pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < mailbox->pool.thread_count);

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&mailbox->pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			for (;;)
			{
				int work_index;

				// Wait for one row from each channel to process
				error = PoolThreadWaitForWork(&mailbox->pool, &work_index, thread_index);
				
				if (error == THREAD_ERROR_OKAY)
				{
					//thread_work->thread_func(work_index);
					switch(mailbox->jobtype)
					{
					case ScaleToQuickTimeBGRAThreadID:
						myclass->ScaleToQuickTimeBGRAThread(work_index);
						break;
					case ScaleToQuickTimeARGBThreadID:
						myclass->ScaleToQuickTimeARGBThread(work_index);
						break;
					}
				}						
				else
				{
					// No more work to do
					break;
				}
			}

			// Signal that this thread is done
			PoolThreadSignalDone(&mailbox->pool, thread_index);
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}


void CImageScalerConverterRGB32ToQuickTime::ScaleToQuickTimeBGRAThread(int index)
{
	//unsigned char *inputBuffer = (unsigned char *)mailbox.ptrs[0];
	unsigned char *outputBuffer = (unsigned char *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	int inputHeight = mailbox.vars[1];
	//int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int outputHeight = mailbox.vars[4];
	int outputPitch = mailbox.vars[5];
	int yy = index;
	
	unsigned char *ptr = outputBuffer;
	
	// if image is flipped
	if(flipDuringScale)
		ptr += outputPitch * (outputHeight - 1);

	int renderFieldType = 0;

	
	{
		int x;
		int samples = 0;
		lanczosmix lmY[200];
		unsigned char *localptr = ptr;
		unsigned char *BGRA;
		float *outYUVA128;

		if(flipDuringScale)
			localptr -= (outputPitch * yy);
		else 
			localptr += (outputPitch * yy);
		
		BGRA = (unsigned char *)localptr;
		outYUVA128 = (float *)localptr;

		samples = ComputeColumnScaleFactors(yy, inputHeight, outputHeight, renderFieldType, lmY);

		unsigned short *sourceRGB = (unsigned short *)horizontalscale;
		unsigned short *RGBptr;
		int scaledstride = outputWidth * 3;

		for(x=0;x<outputWidth; x++)
		{
			//float y,u,v,u1,v1;
			int R,G,B;

			if(inputHeight == outputHeight)
			{
				RGBptr = sourceRGB + scaledstride * yy;
				R = *RGBptr++;
				G = *RGBptr++;
				B = *RGBptr++;
			}
			else
			{
				ScaleColumnValues(sourceRGB, scaledstride, lmY, samples, R, G, B);
			}
			sourceRGB+=3;

			//if(imageRec->pixformat == PrPixelFormat_BGRA32)
			if (1)
			{
				bool usevideosystemsRGB = false;

				if(usevideosystemsRGB)
				{
					R *= 3518; //219/255
					R >>= 12;
					R += 16<<5;

					G *= 3518; //219/255
					G >>= 12;
					G += 16<<5;

					B *= 3518; //219/255
					B >>= 12;
					B += 16<<5;
				}

				R >>= 8;
				G >>= 8;
				B >>= 8;

				if(R < 0) R=0; else if(R>255) R=255;
				if(G < 0) G=0; else if(G>255) G=255;
				if(B < 0) B=0; else if(B>255) B=255;

				*BGRA++ = B;
				*BGRA++ = G;
				*BGRA++ = R;
				*BGRA++ = 255;
			}
		}
	}
}



// Scale and convert the input image to QuickTime BGRA with 8 bits per channel
void CImageScalerConverterRGB32ToQuickTime::ScaleToQuickTimeBGRA(unsigned char *inputBuffer,
																 int inputWidth,
																 int inputHeight,
																 int inputPitch,
																 unsigned char *outputBuffer,
																 int outputWidth,
																 int outputHeight,
																 int outputPitch)
{
	//const int alpha = UCHAR_MAX;

	//unsigned char *base = inputBuffer;
	//int renderFieldType = 0;
	//unsigned char *ptr = outputBuffer;
	//unsigned char *fieldbase = base;

#if (1 && SYSLOG)
	fprintf(stderr,
			"Normal scaling input width: %d, height: %d, output width: %d, height: %d\n",
			inputWidth, inputHeight, outputWidth, outputHeight);
#endif

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(outputWidth, inputHeight)) {
		return;
	}

	//TODO: Need to choose a scheme for error codes

	ComputeRowScaleFactors(scaleFactors, inputWidth, outputWidth);

    ScaleRowValues(inputBuffer, inputWidth, inputHeight, inputPitch, horizontalscale, outputWidth);


	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)inputBuffer;
	mailbox.ptrs[1] = (void *)outputBuffer;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.vars[4] = outputHeight;
	mailbox.vars[5] = outputPitch;
	mailbox.jobtype = ScaleToQuickTimeBGRAThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, (outputHeight) );
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);



	// Free the scratch buffers used for interpolation
	FreeScratchMemory();

	//return imNoErr;
}



void CImageScalerConverterRGB32ToQuickTime::ScaleToQuickTimeARGBThread(int index)
{
	//unsigned char *inputBuffer = (unsigned char *)mailbox.ptrs[0];
	unsigned char *outputBuffer = (unsigned char *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	int inputHeight = mailbox.vars[1];
	//int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int outputHeight = mailbox.vars[4];
	int outputPitch = mailbox.vars[5];
	int yy = index;
	
	unsigned char *ptr = outputBuffer;
	
	if(flipDuringScale)
		ptr += outputPitch * (outputHeight - 1);

	int renderFieldType = 0;


	{
		int x;
		int samples = 0;
		lanczosmix lmY[200];
		unsigned char *localptr = ptr;
		unsigned char *BGRA;
		float *outYUVA128;

		if(flipDuringScale)
			localptr -= (outputPitch * yy);
		else 
			localptr += (outputPitch * yy);
		
		BGRA = (unsigned char *)localptr;
		outYUVA128 = (float *)localptr;

		samples = ComputeColumnScaleFactors(yy, inputHeight, outputHeight, renderFieldType, lmY);

		unsigned short *sourceRGB = (unsigned short *)horizontalscale;
		unsigned short *RGBptr;
		int scaledstride = outputWidth * 3;

		for(x=0;x<outputWidth; x++)
		{
			//float y,u,v,u1,v1;
			int R,G,B;

			if(inputHeight == outputHeight)
			{
				RGBptr = sourceRGB + scaledstride * yy;
				R = *RGBptr++;
				G = *RGBptr++;
				B = *RGBptr++;
			}
			else
			{
				ScaleColumnValues(sourceRGB, scaledstride, lmY, samples, R, G, B);
			}
			sourceRGB+=3;

			//if(imageRec->pixformat == PrPixelFormat_BGRA32)
			if (1)
			{
				bool usevideosystemsRGB = false;

				if(usevideosystemsRGB)
				{
					R *= 3518; //219/255
					R >>= 12;
					R += 16<<5;

					G *= 3518; //219/255
					G >>= 12;
					G += 16<<5;

					B *= 3518; //219/255
					B >>= 12;
					B += 16<<5;
				}

				R >>= 8;
				G >>= 8;
				B >>= 8;

				if(R < 0) R=0; else if(R>255) R=255;
				if(G < 0) G=0; else if(G>255) G=255;
				if(B < 0) B=0; else if(B>255) B=255;
#if 0
				*BGRA++ = B;
				*BGRA++ = G;
				*BGRA++ = R;
				*BGRA++ = 255;
#else
				*BGRA++ = 255;
				*BGRA++ = R;
				*BGRA++ = G;
				*BGRA++ = B;
#endif
			}
		}
	}
}

// Scale and convert the input image to QuickTime ARGB with 8 bits per channel
void CImageScalerConverterRGB32ToQuickTime::ScaleToQuickTimeARGB(unsigned char *inputBuffer,
																 int inputWidth,
																 int inputHeight,
																 int inputPitch,
																 unsigned char *outputBuffer,
																 int outputWidth,
																 int outputHeight,
																 int outputPitch)
{
	//const int alpha = UCHAR_MAX;

	//unsigned char *base = inputBuffer;
	//int renderFieldType = 0;
	//unsigned char *ptr = outputBuffer;
	//unsigned char *fieldbase = base;

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(outputWidth, inputHeight)) {
		return;
	}

	//TODO: Need to choose a scheme for error codes

	ComputeRowScaleFactors(scaleFactors, inputWidth, outputWidth);

    ScaleRowValues(inputBuffer, inputWidth, inputHeight, inputPitch, horizontalscale, outputWidth);

	
	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)inputBuffer;
	mailbox.ptrs[1] = (void *)outputBuffer;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.vars[4] = outputHeight;
	mailbox.vars[5] = outputPitch;
	mailbox.jobtype = ScaleToQuickTimeARGBThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, (outputHeight) );
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);


	// Free the scratch buffers used for interpolation
	FreeScratchMemory();

	//return imNoErr;
}



THREAD_PROC(CImageScalerB64A::ScalerProc, lpParam)
{
	CImageScalerB64A *myclass = (CImageScalerB64A *)lpParam;
	MAILBOX *mailbox = (MAILBOX *)&myclass->mailbox;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&mailbox->pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < mailbox->pool.thread_count);

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&mailbox->pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			for (;;)
			{
				int work_index;

				// Wait for one row from each channel to process
				error = PoolThreadWaitForWork(&mailbox->pool, &work_index, thread_index);
				
				if (error == THREAD_ERROR_OKAY)
				{
					//thread_work->thread_func(work_index);
					switch(mailbox->jobtype)
					{
					case ScaleRowValuesThreadID:
						myclass->ScaleRowValuesThread(work_index);
						break;
					}
				}						
				else
				{
					// No more work to do
					break;
				}
			}

			// Signal that this thread is done
			PoolThreadSignalDone(&mailbox->pool, thread_index);
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}


void CImageScalerB64A::ScaleRowValuesThread(int index)
{
	unsigned short *input = (unsigned short *)mailbox.ptrs[0];
	unsigned short *output = (unsigned short *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	//int inputHeight = mailbox.vars[1];
	int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int yy = index;
	unsigned short *outptr = output;

	{
		int dstx, srcx, srcmix, tmpA, tmpR, tmpG, tmpB;
		unsigned short *rgbptr;

		short *ptrL = scaleFactors;

		outptr = output + (outputWidth * 4) * yy;
		rgbptr = (unsigned short *)(input + (inputPitch/2) * yy);

		while((dstx = *ptrL++) != -1)
		{
			tmpR = tmpG = tmpB = tmpA = 0;
			while((srcx = *ptrL++) != -1)
			{
				srcmix = *ptrL++;
				tmpA += rgbptr[srcx*4 + 0] * srcmix;
				tmpR += rgbptr[srcx*4 + 1] * srcmix;
				tmpG += rgbptr[srcx*4 + 2] * srcmix;
				tmpB += rgbptr[srcx*4 + 3] * srcmix;
			}
			tmpA >>= 8;
			if(tmpA > USHRT_MAX) tmpA = USHRT_MAX;
			if(tmpA < 0) tmpA = 0;

			tmpR >>= 8;
			if (tmpR > USHRT_MAX) tmpR = USHRT_MAX;
			if (tmpR < 0) tmpR = 0;

			tmpG >>= 8;
			if(tmpG > USHRT_MAX) tmpG = USHRT_MAX;
			if(tmpG < 0) tmpG = 0;

			tmpB >>= 8;
			if(tmpB > USHRT_MAX) tmpB = USHRT_MAX;
			if(tmpB < 0) tmpB = 0;

			outptr[dstx*4 + 0] = tmpA; ///now R..R..RGBRGB
			outptr[dstx*4 + 1] = tmpR; ///now R..R..RGBRGB
			outptr[dstx*4 + 2] = tmpG; ///now .G..G.RGBRGB
			outptr[dstx*4 + 3] = tmpB; ///now ..B..BRGBRGB
		}
	}
}


// Scale the rows of ARGB values
void CImageScalerB64A::ScaleRowValues(unsigned char *input, int inputWidth, int inputHeight, int inputPitch,
									  unsigned short *output, int outputWidth)
{
	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)input;
	mailbox.ptrs[1] = (void *)output;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.jobtype = ScaleRowValuesThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, inputHeight);
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);
}

// Scale the ARGB values in the specified column
void CImageScalerB64A::ScaleColumnValues(unsigned short *input, int stride,
										 lanczosmix *lmY, int sampleCount,
										 int &A, int &R, int &G, int &B)
{
	unsigned short *RGBptr;

	A = R = G = B = 0;

	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		RGBptr = input + stride * lmY[i].srcline;
		A += *RGBptr++ * mix;
		R += *RGBptr++ * mix;
		G += *RGBptr++ * mix;
		B += *RGBptr++ * mix;
	}

	A >>= 8;
	R >>= 8;
	G >>= 8;
	B >>= 8;

	if (A < 0) A = 0;
	else if (A > USHRT_MAX) A = USHRT_MAX;

	if (R < 0) R = 0;
	else if (R > USHRT_MAX) R = USHRT_MAX;

	if (G < 0) G = 0;
	else if (G > USHRT_MAX) G = USHRT_MAX;

	if (B < 0) B = 0;
	else if (B > USHRT_MAX) B = USHRT_MAX;
}





THREAD_PROC(CImageScalerConverterB64A::ScalerProc, lpParam)
{
	CImageScalerConverterB64A *myclass = (CImageScalerConverterB64A *)lpParam;
	MAILBOX *mailbox = (MAILBOX *)&myclass->mailbox;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&mailbox->pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < mailbox->pool.thread_count);

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&mailbox->pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			for (;;)
			{
				int work_index;

				// Wait for one row from each channel to process
				error = PoolThreadWaitForWork(&mailbox->pool, &work_index, thread_index);
				
				if (error == THREAD_ERROR_OKAY)
				{
					//thread_work->thread_func(work_index);
					switch(mailbox->jobtype)
					{
					case ScaleToB64AThreadID:
						myclass->ScaleToB64AThread(work_index);
						break;
					case ScaleToBGRAThreadID:
						myclass->ScaleToBGRAThread(work_index);
						break;
					}
				}						
				else
				{
					// No more work to do
					break;
				}
			}

			// Signal that this thread is done
			PoolThreadSignalDone(&mailbox->pool, thread_index);
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}


void CImageScalerConverterB64A::ScaleToB64AThread(int index)
{
	//unsigned char *inputBuffer = (unsigned char *)mailbox.ptrs[0];
	unsigned char *outputBuffer = (unsigned char *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	int inputHeight = mailbox.vars[1];
	//int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int outputHeight = mailbox.vars[4];
	int outputPitch = mailbox.vars[5];
#if _WIN32
	int byte_swap_flag = mailbox.vars[6];
#endif
	int yy = index;
	int renderFieldType = 0;
	unsigned char *ptr = outputBuffer;
	
	{
		int x;
		int samples = 0;
		lanczosmix lmY[200];
		unsigned char *localptr = ptr;
		unsigned short *BGRA;
		//float *outYUVA128;

		const int max_rgb = USHRT_MAX;
		//const int alpha = USHRT_MAX;

		//localptr -= (outputPitch * yy);
		localptr += (outputPitch * yy);
		BGRA = (unsigned short *)localptr;
		//outYUVA128 = (float *)localptr;

		samples = ComputeColumnScaleFactors(yy, inputHeight, outputHeight, renderFieldType, lmY);

		unsigned short *sourceRGB = horizontalscale;
		unsigned short *RGBptr;
		int scaledstride = outputWidth * 4;

		for(x=0;x<outputWidth; x++)
		{
			//float y,u,v,u1,v1;
			int A,R,G,B;

			if(inputHeight == outputHeight)
			{
				RGBptr = (unsigned short *)(sourceRGB + scaledstride * yy);
				A = *RGBptr++;
				R = *RGBptr++;
				G = *RGBptr++;
				B = *RGBptr++;
			}
			else
			{
				ScaleColumnValues(sourceRGB, scaledstride, lmY, samples, A, R, G, B);
			}
			sourceRGB+=4;

			//if(imageRec->pixformat == PrPixelFormat_BGRA32)
			if (1)
			{
				bool usevideosystemsRGB = false;

				if(usevideosystemsRGB)
				{
					R *= 3518; //219/255
					R >>= 12;
					R += 16<<5;

					G *= 3518; //219/255
					G >>= 12;
					G += 16<<5;

					B *= 3518; //219/255
					B >>= 12;
					B += 16<<5;
				}

				//R >>= 8;
				//G >>= 8;
				//B >>= 8;

				if (A < 0) A = 0; else if (A > max_rgb) A = max_rgb;
				if (R < 0) R = 0; else if (R > max_rgb) R = max_rgb;
				if (G < 0) G = 0; else if (G > max_rgb) G = max_rgb;
				if (B < 0) B = 0; else if (B > max_rgb) B = max_rgb;

#ifdef _WIN32
				if (!byte_swap_flag)
				{
					*(BGRA++) = A;
					*(BGRA++) = R;
					*(BGRA++) = G;
					*(BGRA++) = B;
				}
				else
				{
					*(BGRA++) = SwapInt16(A);
					*(BGRA++) = SwapInt16(R);
					*(BGRA++) = SwapInt16(G);
					*(BGRA++) = SwapInt16(B);
				}
#else
				*(BGRA++) = SwapInt16(A);
				*(BGRA++) = SwapInt16(R);
				*(BGRA++) = SwapInt16(G);
				*(BGRA++) = SwapInt16(B);
#endif
			}
		}
	}
}



// Scale and convert the input image to QuickTime b64a with 16 bits per channel
void CImageScalerConverterB64A::ScaleToB64A(unsigned char *inputBuffer,
											int inputWidth,
											int inputHeight,
											int inputPitch,
											unsigned char *outputBuffer,
											int outputWidth,
											int outputHeight,
											int outputPitch,
											int byte_swap_flag)
{
	//const int alpha = UCHAR_MAX;

	//unsigned char *base = inputBuffer
	//unsigned char *fieldbase = base;

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(outputWidth, inputHeight)) {
		return;
	}

	//TODO: Need to choose a scheme for error codes

	ComputeRowScaleFactors(scaleFactors, inputWidth, outputWidth);

    ScaleRowValues(inputBuffer, inputWidth, inputHeight, inputPitch, horizontalscale, outputWidth);



	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)inputBuffer;
	mailbox.ptrs[1] = (void *)outputBuffer;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.vars[4] = outputHeight;
	mailbox.vars[5] = outputPitch;
	mailbox.vars[6] = byte_swap_flag;
	mailbox.jobtype = ScaleToB64AThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, outputHeight);
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);



	// Free the scratch buffers used for interpolation
	FreeScratchMemory();

	//return imNoErr;
}




THREAD_PROC(CImageScalerRG48::ScalerProc, lpParam)
{
	CImageScalerRG48 *myclass = (CImageScalerRG48 *)lpParam;
	MAILBOX *mailbox = (MAILBOX *)&myclass->mailbox;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&mailbox->pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < mailbox->pool.thread_count);

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&mailbox->pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			for (;;)
			{
				int work_index;

				// Wait for one row from each channel to process
				error = PoolThreadWaitForWork(&mailbox->pool, &work_index, thread_index);
				
				if (error == THREAD_ERROR_OKAY)
				{
					//thread_work->thread_func(work_index);
					switch(mailbox->jobtype)
					{
					case ScaleRowValuesThreadID:
						myclass->ScaleRowValuesThread(work_index);
						break;
					}
				}						
				else
				{
					// No more work to do
					break;
				}
			}

			// Signal that this thread is done
			PoolThreadSignalDone(&mailbox->pool, thread_index);
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}


void CImageScalerRG48::ScaleRowValuesThread(int index)
{
	unsigned char *input = (unsigned char *)mailbox.ptrs[0];
	unsigned short *output = (unsigned short *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	//int inputHeight = mailbox.vars[1];
	int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int yy = index;
	unsigned short *outptr = output;

	{
		int dstx, srcx, srcmix, tmpR, tmpG, tmpB;
		unsigned short *rgbptr;

		short *ptrL = scaleFactors;

		outptr = output + (outputWidth * 3) * yy;
		rgbptr = (unsigned short *)(input + inputPitch * yy);

		while((dstx = *ptrL++) != -1)
		{
			tmpR = tmpG = tmpB = 0;
			while((srcx = *ptrL++) != -1)
			{
				srcmix = *ptrL++;
				tmpR += rgbptr[srcx*3 + 0] * srcmix;
				tmpG += rgbptr[srcx*3 + 1] * srcmix;
				tmpB += rgbptr[srcx*3 + 2] * srcmix;
			}

			tmpR >>= 8;
			if (tmpR > USHRT_MAX) tmpR = USHRT_MAX;
			if (tmpR < 0) tmpR = 0;

			tmpG >>= 8;
			if(tmpG > USHRT_MAX) tmpG = USHRT_MAX;
			if(tmpG < 0) tmpG = 0;

			tmpB >>= 8;
			if(tmpB > USHRT_MAX) tmpB = USHRT_MAX;
			if(tmpB < 0) tmpB = 0;

			outptr[dstx*3 + 0] = tmpR; ///now R..R..RGBRGB
			outptr[dstx*3 + 1] = tmpG; ///now .G..G.RGBRGB
			outptr[dstx*3 + 2] = tmpB; ///now ..B..BRGBRGB
		}
	}
}



// Scale the rows of ARGB values
void CImageScalerRG48::ScaleRowValues(unsigned char *input, int inputWidth, int inputHeight, int inputPitch,
									  unsigned short *output, int outputWidth)
{
	//unsigned short *outptr = output;
	//int yy;
	
	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)input;
	mailbox.ptrs[1] = (void *)output;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.jobtype = ScaleRowValuesThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, inputHeight);
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);



}

// Scale the ARGB values in the specified column
void CImageScalerRG48::ScaleColumnValues(unsigned short *input, int stride,
										 lanczosmix *lmY, int sampleCount,
										 int &R, int &G, int &B)
{
	unsigned short *RGBptr;

	R = G = B = 0;

	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		RGBptr = input + stride * lmY[i].srcline;
		R += *RGBptr++ * mix;
		G += *RGBptr++ * mix;
		B += *RGBptr++ * mix;
	}

	R >>= 8;
	G >>= 8;
	B >>= 8;

	if (R < 0) R = 0;
	else if (R > USHRT_MAX) R = USHRT_MAX;

	if (G < 0) G = 0;
	else if (G > USHRT_MAX) G = USHRT_MAX;

	if (B < 0) B = 0;
	else if (B > USHRT_MAX) B = USHRT_MAX;
}



THREAD_PROC(CImageScalerConverterRG48::ScalerProc, lpParam)
{
	CImageScalerConverterRG48 *myclass = (CImageScalerConverterRG48 *)lpParam;
	MAILBOX *mailbox = (MAILBOX *)&myclass->mailbox;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&mailbox->pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < mailbox->pool.thread_count);

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&mailbox->pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			for (;;)
			{
				int work_index;

				// Wait for one row from each channel to process
				error = PoolThreadWaitForWork(&mailbox->pool, &work_index, thread_index);
				
				if (error == THREAD_ERROR_OKAY)
				{
					//thread_work->thread_func(work_index);
					switch(mailbox->jobtype)
					{
					case ScaleToRG48ThreadID:
						myclass->ScaleToRG48Thread(work_index);
						break;
					}
				}						
				else
				{
					// No more work to do
					break;
				}
			}

			// Signal that this thread is done
			PoolThreadSignalDone(&mailbox->pool, thread_index);
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}


void CImageScalerConverterRG48::ScaleToRG48Thread(int index)
{
	//unsigned char *inputBuffer = (unsigned char *)mailbox.ptrs[0];
	unsigned char *outputBuffer = (unsigned char *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	int inputHeight = mailbox.vars[1];
	//int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int outputHeight = mailbox.vars[4];
	int outputPitch = mailbox.vars[5];
	int byte_swap_flag = mailbox.vars[6];
	int lobes = mailbox.vars[7];
	int yy = index;
	int renderFieldType = 0;
	unsigned char *ptr = outputBuffer;

	{
		int x;
		int samples = 0;
		lanczosmix lmY[200];
		unsigned char *localptr = ptr;
		unsigned short *BGRA;
		//float *outYUVA128;

		const int max_rgb = USHRT_MAX;
		//const int alpha = USHRT_MAX;

		//localptr -= (outputPitch * yy);
		localptr += (outputPitch * yy);
		BGRA = (unsigned short *)localptr;
		//outYUVA128 = (float *)localptr;

		samples = ComputeColumnScaleFactors(yy, inputHeight, outputHeight, renderFieldType, lmY, lobes);

		unsigned short *sourceRGB = horizontalscale;
		unsigned short *RGBptr;
		int scaledstride = outputWidth * 3;

		for(x=0;x<outputWidth; x++)
		{
			//float y,u,v,u1,v1;
			int R,G,B;

			if(inputHeight == outputHeight)
			{
				RGBptr = (unsigned short *)(sourceRGB + scaledstride * yy);
				R = *RGBptr++;
				G = *RGBptr++;
				B = *RGBptr++;
			}
			else
			{
				ScaleColumnValues(sourceRGB, scaledstride, lmY, samples, R, G, B);
			}
			sourceRGB+=3;

			//if(imageRec->pixformat == PrPixelFormat_BGRA32)
			if (1)
			{
				bool usevideosystemsRGB = false;

				if(usevideosystemsRGB)
				{
					R *= 3518; //219/255
					R >>= 12;
					R += 16<<5;

					G *= 3518; //219/255
					G >>= 12;
					G += 16<<5;

					B *= 3518; //219/255
					B >>= 12;
					B += 16<<5;
				}

				//R >>= 8;
				//G >>= 8;
				//B >>= 8;

				if (R < 0) R = 0; else if (R > max_rgb) R = max_rgb;
				if (G < 0) G = 0; else if (G > max_rgb) G = max_rgb;
				if (B < 0) B = 0; else if (B > max_rgb) B = max_rgb;

#ifdef _WIN32
				if (!byte_swap_flag)
				{
					*(BGRA++) = R;
					*(BGRA++) = G;
					*(BGRA++) = B;
				}
				else
				{
					*(BGRA++) = SwapInt16(R);
					*(BGRA++) = SwapInt16(G);
					*(BGRA++) = SwapInt16(B);
				}
#else
				if (!byte_swap_flag)
				{
					*(BGRA++) = R;
					*(BGRA++) = G;
					*(BGRA++) = B;
				}
				else
				{
					*(BGRA++) = SwapInt16(R);
					*(BGRA++) = SwapInt16(G);
					*(BGRA++) = SwapInt16(B);
				}
#endif
			}
		}
	}
}

	



// Scale and convert the input image to RG48 with 16 bits per channel
void CImageScalerConverterRG48::ScaleToRG48(unsigned char *inputBuffer,
											int inputWidth,
											int inputHeight,
											int inputPitch,
											unsigned char *outputBuffer,
											int outputWidth,
											int outputHeight,
											int outputPitch,
											int byte_swap_flag,
											int lobes)
{
	//const int alpha = UCHAR_MAX;

	//unsigned char *base = inputBuffer;
	//int renderFieldType = 0;
	//unsigned char *ptr = outputBuffer;
	//unsigned char *fieldbase = base;

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(outputWidth, inputHeight)) {
		return;
	}

	//TODO: Need to choose a scheme for error codes

	ComputeRowScaleFactors(scaleFactors, inputWidth, outputWidth, lobes);

    ScaleRowValues(inputBuffer, inputWidth, inputHeight, inputPitch, horizontalscale, outputWidth);


	
	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)inputBuffer;
	mailbox.ptrs[1] = (void *)outputBuffer;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.vars[4] = outputHeight;
	mailbox.vars[5] = outputPitch;
	mailbox.vars[6] = byte_swap_flag;
	mailbox.vars[7] = lobes;
	mailbox.jobtype = ScaleToRG48ThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, outputHeight);
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);



	// Free the scratch buffers used for interpolation
	FreeScratchMemory();

	//return imNoErr;
}



void CImageScalerConverterB64A::ScaleToBGRAThread(int index)
{
	//unsigned char *inputBuffer = (unsigned char *)mailbox.ptrs[0];
	unsigned char *outputBuffer = (unsigned char *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	int inputHeight = mailbox.vars[1];
	//int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int outputHeight = mailbox.vars[4];
	int outputPitch = mailbox.vars[5];
	int yy = index;
	int renderFieldType = 0;
	unsigned char *ptr = outputBuffer;

	{
		int x;
		int samples = 0;
		lanczosmix lmY[200];
		unsigned char *localptr = ptr;
		unsigned char *BGRA;
		//float *outYUVA128;

		const int max_rgb = UCHAR_MAX;
		//const int alpha = UCHAR_MAX;

		//localptr -= (outputPitch * yy);
		localptr += (outputPitch * yy);
		BGRA = (unsigned char *)localptr;
		//outYUVA128 = (float *)localptr;

		samples = ComputeColumnScaleFactors(yy, inputHeight, outputHeight, renderFieldType, lmY);

		unsigned short *sourceRGB = horizontalscale;
		unsigned short *RGBptr;
		int scaledstride = outputWidth * 3;

		for(x=0;x<outputWidth; x++)
		{
			//float y,u,v,u1,v1;
			int A,R,G,B;

			if(inputHeight == outputHeight)
			{
				RGBptr = (unsigned short *)(sourceRGB + scaledstride * yy);
				A = *RGBptr++;
				R = *RGBptr++;
				G = *RGBptr++;
				B = *RGBptr++;
			}
			else
			{
				ScaleColumnValues(sourceRGB, scaledstride, lmY, samples, A, R, G, B);
			}
			sourceRGB+=4;

			//if(imageRec->pixformat == PrPixelFormat_BGRA32)
			if (1)
			{
				bool usevideosystemsRGB = false;

				if(usevideosystemsRGB)
				{
					R *= 3518; //219/255
					R >>= 12;
					R += 16<<5;

					G *= 3518; //219/255
					G >>= 12;
					G += 16<<5;

					B *= 3518; //219/255
					B >>= 12;
					B += 16<<5;
				}

				A >>= 8;
				R >>= 8;
				G >>= 8;
				B >>= 8;

				if (A < 0) A = 0; else if (A > max_rgb) A = max_rgb;
				if (R < 0) R = 0; else if (R > max_rgb) R = max_rgb;
				if (G < 0) G = 0; else if (G > max_rgb) G = max_rgb;
				if (B < 0) B = 0; else if (B > max_rgb) B = max_rgb;

				*(BGRA++) = B;
				*(BGRA++) = G;
				*(BGRA++) = R;
				*(BGRA++) = A;
			}
		}
	}
}

	

	



// Scale and convert the input image to QuickTime BGRA with 8 bits per channel
void CImageScalerConverterB64A::ScaleToBGRA(unsigned char *inputBuffer,
											int inputWidth,
											int inputHeight,
											int inputPitch,
											unsigned char *outputBuffer,
											int outputWidth,
											int outputHeight,
											int outputPitch)
{
	//const int alpha = UCHAR_MAX;

	//unsigned char *base = inputBuffer;
	//int renderFieldType = 0;
	//unsigned char *ptr = outputBuffer;
	//unsigned char *fieldbase = base;

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(outputWidth, inputHeight)) {
		return;
	}

	//TODO: Need to choose a scheme for error codes

	ComputeRowScaleFactors(scaleFactors, inputWidth, outputWidth);

    ScaleRowValues(inputBuffer, inputWidth, inputHeight, inputPitch, horizontalscale, outputWidth);

	
	if(mailbox.pool.thread_count == 0)
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)inputBuffer;
	mailbox.ptrs[1] = (void *)outputBuffer;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.vars[4] = outputHeight;
	mailbox.vars[5] = outputPitch;
	mailbox.jobtype = ScaleToBGRAThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, outputHeight);
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);




	// Free the scratch buffers used for interpolation
	FreeScratchMemory();

	//return imNoErr;
}





THREAD_PROC(CImageScalerYUV::ScalerProc, lpParam)
{
	CImageScalerYUV *myclass = (CImageScalerYUV *)lpParam;
	MAILBOX *mailbox = (MAILBOX *)&myclass->mailbox;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&mailbox->pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < mailbox->pool.thread_count);

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&mailbox->pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			for (;;)
			{
				int work_index;

				// Wait for one row from each channel to process
				error = PoolThreadWaitForWork(&mailbox->pool, &work_index, thread_index);
				
				if (error == THREAD_ERROR_OKAY)
				{
					//thread_work->thread_func(work_index);
					switch(mailbox->jobtype)
					{
					case ScaleRowValuesThreadID:
						myclass->ScaleRowValuesThread(work_index);
						break;
					}
				}						
				else
				{
					// No more work to do
					break;
				}
			}

			// Signal that this thread is done
			PoolThreadSignalDone(&mailbox->pool, thread_index);
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}


void CImageScalerYUV::ScaleRowValuesThread(int index)
{
	unsigned char *inputBuffer = (unsigned char *)mailbox.ptrs[0];
	unsigned char *output_buffer = (unsigned char *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	//int inputHeight = mailbox.vars[1];
	int inputPitch = mailbox.vars[2];
	//int outputWidth = mailbox.vars[3];
	int output_pitch = mailbox.vars[4];
	int input_pitch = inputPitch;
	uint8_t *input_buffer = (uint8_t *)inputBuffer;
	int row = index;

	{
		//int dstx,srcx,srcmix,tmpY,tmpU,tmpV;

		uint8_t *input_row_ptr = (input_buffer + row * input_pitch);
		uint8_t *output_row_ptr = (output_buffer + row * output_pitch);

		// Scale the luma values in this row
		ScaleRowLuma(input_row_ptr, output_row_ptr, scalefactorsL);

		// Scale the chroma values in this row
		ScaleRowChroma(input_row_ptr, output_row_ptr, scalefactorsC);

		// Advance to the next input and output rows
		//inputRowPtr += inputPitch;
		//outputRowPtr += outputStride;
	}
}



	

// Scale the rows of luma and chroma
void CImageScalerYUV::ScaleRowValues(unsigned char *inputBuffer, int inputWidth, int inputHeight, int inputPitch, int outputWidth)
{
	//TODO: Allocate a smaller buffer for the 8-bit scaled values
	uint8_t *output_buffer = (uint8_t *)horizontalscale;
	int output_pitch = 2 * outputWidth;


	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)inputBuffer;
	mailbox.ptrs[1] = (void *)output_buffer;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.vars[4] = output_pitch;
	mailbox.jobtype = ScaleRowValuesThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, inputHeight);
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);
}

// Scale one row of luma values (skip the chroma values)
void CImageScalerYUV::ScaleRowLuma(unsigned char *inputRow,
								   unsigned char *outputRow,
								   short *scaleFactors)
{
	short *ptr = scaleFactors;
	int dstx;
	int srcx;
	int srcmix;
	int tmpY;

	while((dstx = *(ptr++)) != -1)
	{
		tmpY = 0;
		while((srcx = *(ptr++)) != -1)
		{
			srcmix = *(ptr++);
			tmpY += inputRow[srcx*2+1]*srcmix; //*2 YUYV
		}
		tmpY >>= 8;
		if (tmpY > 255) tmpY = 255;
		else if (tmpY < 0) tmpY = 0;

		outputRow[dstx*2] = tmpY;	///now Y.Y.YUYV
	}
}

// Scale one row of chroma values (skip the luma values)
void CImageScalerYUV::ScaleRowChroma(unsigned char *inputRow,
									 unsigned char *outputRow,
									 short *scaleFactors)
{
	short *ptr = scaleFactors;
	int dstx;
	int srcx;
	int srcmix;
	int tmpU;
	int tmpV;

	while((dstx = *(ptr++)) != -1)
	{
		tmpU = tmpV = 0;
		while((srcx = *(ptr++)) != -1)
		{
			srcmix = *(ptr++);
			tmpU += inputRow[srcx*4+0]*srcmix; //*4 YUYV
			tmpV += inputRow[srcx*4+2]*srcmix; //*4 YUYV
		}
		tmpU >>= 8;
		if (tmpU > 255) tmpU = 255;
		else if(tmpU < 0) tmpU = 0;

		tmpV >>= 8;
		if (tmpV > 255) tmpV = 255;
		else if (tmpV < 0) tmpV = 0;

		outputRow[dstx*4+1] = tmpU;		//now .U...U..YUYV
		outputRow[dstx*4+3] = tmpV;		//now ...V...VYUYV
	}
}

void CImageScalerYUV::ScaleColumnValues(unsigned char *input,
										int stride,
										lanczosmix *lmY,
										int sampleCount,
										int &y1,
										int &u1,
										int &y2,
										int &v1)
{
	const int yuv_max = 255;
	unsigned char *yuvptr;

	y1 = y2 = 0;
	u1 = v1 = 0;

	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		yuvptr = input + stride * lmY[i].srcline;
		u1 += *(yuvptr++) * mix;
		y1 += *(yuvptr++) * mix;
		v1 += *(yuvptr++) * mix;
		y2 += *(yuvptr++) * mix;
	}

	y1 >>= 8;
	u1 >>= 8;
	y2 >>= 8;
	v1 >>= 8;

	if (y1 > yuv_max) y1 = yuv_max;
	else if (y1 < 0) y1 = 0;

	if (u1 > yuv_max) u1 = yuv_max;
	else if (u1 < 0) u1 = 0;

	if (y2 > yuv_max) y2 = yuv_max;
	else if (y2 < 0) y2 = 0;

	if (v1 > yuv_max) v1 = yuv_max;
	if (v1 < 0) v1 = 0;
}




THREAD_PROC(CImageScalerConverterYUV::ScalerProc, lpParam)
{
	CImageScalerConverterYUV *myclass = (CImageScalerConverterYUV *)lpParam;
	MAILBOX *mailbox = (MAILBOX *)&myclass->mailbox;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&mailbox->pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < mailbox->pool.thread_count);

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&mailbox->pool, thread_index, &message);

		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
			for (;;)
			{
				int work_index;

				// Wait for one row from each channel to process
				error = PoolThreadWaitForWork(&mailbox->pool, &work_index, thread_index);
				
				if (error == THREAD_ERROR_OKAY)
				{
					//thread_work->thread_func(work_index);
					switch(mailbox->jobtype)
					{
					case ScaleToYUV_422_8u_ThreadID:
						myclass->ScaleToYUV_422_8u_Thread(work_index);
						break;
					case ScaleToCbYCrY_422_8u_ThreadID:
						myclass->ScaleToCbYCrY_422_8u_Thread(work_index);
						break;
					}
				}						
				else
				{
					// No more work to do
					break;
				}
			}

			// Signal that this thread is done
			PoolThreadSignalDone(&mailbox->pool, thread_index);
		}
		else if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_STOP)
		{
			// The worker thread has been told to terminate itself
			break;
		}
		else
		{
			// If the wait failed it probably means that the thread pool is shutting down
			break;
		}
	}

	return (THREAD_RETURN_TYPE)error;
}


void CImageScalerConverterYUV::ScaleToYUV_422_8u_Thread(int index)
{
	//unsigned char *inputBuffer = (unsigned char *)mailbox.ptrs[0];
	unsigned char *outputBuffer = (unsigned char *)mailbox.ptrs[1];
	//int inputWidth = mailbox.vars[0];
	int inputHeight = mailbox.vars[1];
	//int inputPitch = mailbox.vars[2];
	int outputWidth = mailbox.vars[3];
	int outputHeight = mailbox.vars[4];
	int outputPitch = mailbox.vars[5];
	int row = index;
	
	int renderFieldType = 0;
	//unsigned char *output_row_ptr = outputBuffer;
	//int inputChromaWidth = inputWidth / 2;
	//int outputChromaWidth = outputWidth / 2;
	int scaled_stride = outputWidth * 2;


	{
		int samples = 0;
		lanczosmix lmY[200];
		//unsigned char *localptr = ptr;
		unsigned char *outptr;
		//int x;

		outptr = outputBuffer + outputPitch * row;

		samples = ComputeColumnScaleFactors(row, inputHeight, outputHeight, renderFieldType, lmY);

		unsigned char *yuv_row_ptr = ((unsigned char *)horizontalscale) + row * scaled_stride;
		unsigned char *yuvptr = yuv_row_ptr;

		unsigned char *scaled_column_ptr = (unsigned char *)horizontalscale;

		// Process two luma and chroma pairs per iteration
		for (int column = 0; column < outputWidth; column += 2)
		{
			int y1, u1, y2, v1;

			if (inputHeight == outputHeight)
			{
				// Copy the horizontally scaled pixels to the output buffer
				u1 = *(yuvptr++);
				y1 = *(yuvptr++);
				v1 = *(yuvptr++);
				y2 = *(yuvptr++);
			}
			else
			{
				// Vertically scale the pixels that were horizontally scaled into the scratch buffer
				ScaleColumnValues(scaled_column_ptr, scaled_stride, lmY, samples, y1, u1, y2, v1);

				// Advance to the next set of four luma and chroma columns
				scaled_column_ptr += 4;
			}

			*(outptr++) = u1;
			*(outptr++) = y1;
			*(outptr++) = v1;
			*(outptr++) = y2;
		}
	}
}



	


void CImageScalerConverterYUV::ScaleToYUV_422_8u(unsigned char *inputBuffer,
												 int inputWidth,
												 int inputHeight,
												 int inputPitch,
												 unsigned char *outputBuffer,
												 int outputWidth,
												 int outputHeight,
												 int outputPitch)
{
	//int renderFieldType = 0;
	//unsigned char *output_row_ptr = outputBuffer;

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(outputWidth, inputHeight)) {
		return;
	}

	//TODO: Need to choose a scheme for error codes

	//int yy;

	int inputChromaWidth = inputWidth / 2;
	int outputChromaWidth = outputWidth / 2;

	ComputeRowScaleFactors(scalefactorsL, inputWidth, outputWidth);
	ComputeRowScaleFactors(scalefactorsC, inputChromaWidth, outputChromaWidth);

	ScaleRowValues(inputBuffer, inputWidth, inputHeight, inputPitch, outputWidth);

	// The image is flipped
	//output_row_ptr += outputPitch * (outputHeight - 1);

	//int scaled_stride = outputWidth * 2;


	
	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)inputBuffer;
	mailbox.ptrs[1] = (void *)outputBuffer;
	mailbox.vars[0] = inputWidth;
	mailbox.vars[1] = inputHeight;
	mailbox.vars[2] = inputPitch;
	mailbox.vars[3] = outputWidth;
	mailbox.vars[4] = outputHeight;
	mailbox.vars[5] = outputPitch;
	mailbox.jobtype = ScaleToYUV_422_8u_ThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, outputHeight);
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);


	// Free the scratch buffers used for interpolation
	FreeScratchMemory();

	//return imNoErr;
}




void CImageScalerConverterYUV::ScaleToCbYCrY_422_8u_Thread(int index)
{
	//unsigned char *inputBuffer = (unsigned char *)mailbox.ptrs[0];
	unsigned char *outputBuffer = (unsigned char *)mailbox.ptrs[1];
	//int input_width = mailbox.vars[0];
	int input_height = mailbox.vars[1];
	//int input_pitch = mailbox.vars[2];
	int output_width = mailbox.vars[3];
	int output_height = mailbox.vars[4];
	int output_pitch = mailbox.vars[5];
	int first_row = mailbox.vars[6]; 
	//int last_row = mailbox.vars[7];
	int scaled_height = mailbox.vars[8];
	int row = index;
	
	int renderFieldType = 0;
	//uint8_t *input_buffer = reinterpret_cast<uint8_t *>(inputBuffer);
	uint8_t *output_buffer = reinterpret_cast<uint8_t *>(outputBuffer);
	//int input_chroma_width = input_width / 2;
	//int output_chroma_width = output_width / 2;
	int scaled_stride = output_width * 2;

	{
		int samples = 0;
		lanczosmix lmY[200];
		uint8_t *outptr = output_buffer + row * output_pitch;

		// Compute the coefficients for scaling each column at the current row position
		int line = row - first_row;
		samples = ComputeColumnScaleFactors(line, input_height, scaled_height, renderFieldType, lmY);

		uint8_t *yuv_row_ptr = reinterpret_cast<uint8_t *>(horizontalscale) + row * scaled_stride;
		uint8_t *yuvptr = yuv_row_ptr;

		uint8_t *scaled_column_ptr = reinterpret_cast<uint8_t *>(horizontalscale);

		// Process two luma and chroma pairs per iteration
		for (int column = 0; column < output_width; column += 2)
		{
			int Y1, Cb, Y2, Cr;

			if (input_height == output_height)
			{
				// Copy the horizontally scaled pixels to the output buffer
				Cb = *(yuvptr++);
				Y1 = *(yuvptr++);
				Cr = *(yuvptr++);
				Y2 = *(yuvptr++);
			}
			else
			{
				// Vertically scale the pixels that were horizontally scaled into the scratch buffer
				ScaleColumnValues(scaled_column_ptr, scaled_stride, lmY, samples, Cb, Y1, Cr, Y2);

				// Advance to the next set of four luma and chroma columns
				scaled_column_ptr += 4;
			}

			*(outptr++) = Cb;
			*(outptr++) = Y1;
			*(outptr++) = Cr;
			*(outptr++) = Y2;
		}
	}
}



	




/*!
	@Brief Scale the Avid 8-bit CbYCrY 4:2:2 pixel format

	The row offset is used to specify letterboxing.

	The column offset has not been implemented.
*/
void CImageScalerConverterYUV::ScaleToCbYCrY_422_8u(void *inputBuffer,
													int input_width,
													int input_height,
													int input_pitch,
													void *outputBuffer,
													int output_width,
													int output_height,
													int output_pitch,
													int row_offset,
													int column_offset)
{
	//int renderFieldType = 0;

	uint8_t *input_buffer = reinterpret_cast<uint8_t *>(inputBuffer);
	uint8_t *output_buffer = reinterpret_cast<uint8_t *>(outputBuffer);

	//int output_pitch = outputPitch;

	// Compute the letterbox region
	int first_row = row_offset;
	int last_row = output_height - row_offset - 1;
	int scaled_height = last_row - first_row + 1;

	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(output_width, input_height)) {
		return;
	}

	int input_chroma_width = input_width / 2;
	int output_chroma_width = output_width / 2;

	ComputeRowScaleFactors(scalefactorsL, input_width, output_width);
	ComputeRowScaleFactors(scalefactorsC, input_chroma_width, output_chroma_width);

	ScaleRowValues(input_buffer, input_width, input_height, input_pitch, output_width);

	//int scaled_stride = output_width * 2;

	// Fill the upper letterbox region
	//#pragma omp parallel for
	for (int row = 0; row < first_row; row++)
	{
		uint8_t *outptr = output_buffer + row * output_pitch;

		uint8_t Cb = 128;
		uint8_t Y1 = 0;
		uint8_t Cr = 128;
		uint8_t Y2 = 0;

		for (int column = 0; column < output_width; column += 2)
		{
			*(outptr++) = Cb;
			*(outptr++) = Y1;
			*(outptr++) = Cr;
			*(outptr++) = Y2;
		}
	}


	{
		mailbox.cpus = GetProcessorCount();
		CreateLock(&mailbox.lock);
		ThreadPoolCreate(&mailbox.pool,
						mailbox.cpus,
						ScalerProc,
						this);
	}

	// Post a message to the mailbox
	mailbox.ptrs[0] = (void *)inputBuffer;
	mailbox.ptrs[1] = (void *)outputBuffer;
	mailbox.vars[0] = input_width;
	mailbox.vars[1] = input_height;
	mailbox.vars[2] = input_pitch;
	mailbox.vars[3] = output_width;
	mailbox.vars[4] = output_height;
	mailbox.vars[5] = output_pitch;	
	mailbox.vars[6] = first_row;
	mailbox.vars[7] = last_row;
	mailbox.vars[8] = scaled_height;
	mailbox.jobtype = ScaleToCbYCrY_422_8u_ThreadID;

	// Set the work count to the number of rows to process
	ThreadPoolSetWorkCount(&mailbox.pool, (last_row+1 - first_row));
	// Start the transform worker threads
	ThreadPoolSendMessage(&mailbox.pool, THREAD_MESSAGE_START);
	// Wait for all of the worker threads to finish
	ThreadPoolWaitAllDone(&mailbox.pool);



	// Fill the lower letterbox region
	//#pragma omp parallel for
	for (int row = last_row + 1; row < output_height; row++)
	{
		uint8_t *outptr = output_buffer + row * output_pitch;

		uint8_t Cb = 128;
		uint8_t Y1 = 0;
		uint8_t Cr = 128;
		uint8_t Y2 = 0;

		for (int column = 0; column < output_width; column += 2)
		{
			*(outptr++) = Cb;
			*(outptr++) = Y1;
			*(outptr++) = Cr;
			*(outptr++) = Y2;
		}
	}

	// Free the scratch buffers used for interpolation
	FreeScratchMemory();
}

//#pragma mark C routines
//
//
//	C interfaces
//
bool ComputeRowScaleFactors( short *rowScaleFactors,
							 int inputWidth,
							 int outputWidth,
							 int lobes,
							 int rowScaleMaxSamples)
{
	int totalSamples = 0;
	lanczosmix lmX[200];
	short *ptr = rowScaleFactors;
	if(outputWidth<0)
	{
		for (int x = 0; x < -outputWidth; x++)
		{
			int nsamples = _LanczosCoeff(inputWidth, -outputWidth, -outputWidth-x-1, lmX, false, false, lobes);

			totalSamples += (nsamples + 1);
			if (totalSamples>=rowScaleMaxSamples) {
				return false;
			}
			
			*ptr++ = x; // dst line number

			for(int i = 0; i < nsamples; i++)
			{
				*ptr++ = lmX[i].srcline; //src line
				*ptr++ = lmX[i].mixval;  //src mixval
			}

			*ptr++ = -1;	// Next scale factor
		}
	}
	else
	{
		for (int x = 0; x < outputWidth; x++)
		{
			int nsamples = _LanczosCoeff(inputWidth, outputWidth, x, lmX, false, false, lobes);

			totalSamples += (nsamples + 1);
			if (totalSamples>=rowScaleMaxSamples) {
				return false;
			}
			
			*ptr++ = x; // dst line number

			for(int i = 0; i < nsamples; i++)
			{
				*ptr++ = lmX[i].srcline; //src line
				*ptr++ = lmX[i].mixval;  //src mixval
			}

			*ptr++ = -1;	// Next scale factor
		}
	}
	*ptr++ = -1;		//no more factors
	return true;
}

int ComputeColumnScaleFactors( int row,
							   int inputHeight,
							   int outputHeight,
							   int renderFieldType,
							   lanczosmix **lmY,
							   int lobes)
{
	int samples = 0;
	lanczosmix		l[200];
	lanczosmix		*newMix;

	if (inputHeight == outputHeight)
	{
		//samples = LanczosCoeff(decoder->frame.height, imageRec->dstHeight, row, lmY, false, false);
	}
	else
	{
		switch(renderFieldType)
		{
			case 0:
				samples = _LanczosCoeff(inputHeight, outputHeight, row, l, false, false, lobes);
				break;

			case 1:
			case 2:
				samples = _LanczosCoeff(inputHeight, outputHeight, row, l, false, true, lobes);

				for (int i=0; i<samples; i++)
				{
					l[i].srcline *= 2;
					l[i].srcline += (row & 1);
				}
					break;
		}
	}
	newMix = NULL;
	if(samples)
	{
#ifdef __APPLE__
		newMix = (lanczosmix *)malloc(sizeof(lanczosmix)*samples);
#else
		newMix = (lanczosmix *)_mm_malloc( sizeof(lanczosmix)*samples, 16 );
#endif
		if(newMix)
		{
			memcpy( newMix, l, sizeof(lanczosmix)*samples );
		}
	}
	*lmY = newMix;
	return samples;
}
void ScaleYU64RowLuma( unsigned short *input_row_ptr,
					   unsigned short *output_row_ptr,
					   short *rowScaleFactors )
{
	short *ptrL = rowScaleFactors;
	int dstx;
	int srcx;
	int srcmix;
	int tmpY;

	while((dstx = *ptrL++) != -1)
	{
		tmpY = 0;
		while((srcx = *ptrL++) != -1)
		{
			srcmix = *ptrL++;
			tmpY += input_row_ptr[srcx*2]*srcmix; //*2 YUV
		}
		tmpY >>= 8;
		if(tmpY > 65535) tmpY = 65535;
		if(tmpY < 0) tmpY = 0;

		output_row_ptr[dstx*2] = tmpY; ///now Y..Y..YUVYUV
	}
}
void ScaleYU64RowChroma( unsigned short *input_row_ptr,
						 unsigned short *output_row_ptr,
						 short *rowScaleFactors )
{
	short *ptrC = rowScaleFactors;
	int dstx;
	int srcx;
	int srcmix;
	int tmpU;
	int tmpV;

	while((dstx = *ptrC++) != -1)
	{
		tmpU = tmpV = 0;
		while((srcx = *ptrC++) != -1)
		{
			srcmix = *ptrC++;
			tmpU += input_row_ptr[srcx*4+1]*srcmix; //*4 YUV
			tmpV += input_row_ptr[srcx*4+3]*srcmix; //*4 YUV
		}
		tmpU >>= 8;
		if(tmpU > 65535) tmpU = 65535;
		if(tmpU < 0) tmpU = 0;

		tmpV >>= 8;
		if(tmpV > 65535) tmpV = 65535;
		if(tmpV < 0) tmpV = 0;

		output_row_ptr[dstx*4+1] = tmpU;	//now.U..U.YUV
		output_row_ptr[dstx*4+3] = tmpV;	//now..V..VYUV
	}
}
void ScaleRGB32Row( unsigned char *input_row_ptr,
					unsigned short *output_row_ptr,
					short *rowScaleFactors )
{

	int dstx, srcx, srcmix, tmpR, tmpG, tmpB, tmpA;

	short *ptrL = rowScaleFactors;

	while((dstx = *ptrL++) != -1)
	{
		tmpR = tmpG = tmpB = tmpA =0;
		while((srcx = *ptrL++) != -1)
		{
			srcmix = *ptrL++;
			srcx <<= 2;
			tmpR += input_row_ptr[srcx]*srcmix;
			tmpG += input_row_ptr[srcx+1]*srcmix;
			tmpB += input_row_ptr[srcx+2]*srcmix;
			tmpA += input_row_ptr[srcx+3]*srcmix;
		}
		//tmpR >>= 8;
		if(tmpR > 65535) tmpR = 65535;
		if(tmpR < 0) tmpR = 0;

		//tmpG >>= 8;
		if(tmpG > 65535) tmpG = 65535;
		if(tmpG < 0) tmpG = 0;

		//tmpB >>= 8;
		if(tmpB > 65535) tmpB = 65535;
		if(tmpB < 0) tmpB = 0;
		
		//tmpA >>= A;
		if(tmpA > 65535) tmpA = 65535;
		if(tmpA < 0) tmpA = 0;
		
		output_row_ptr[dstx*4] = tmpR;   ///now R...R...RGBARGBA
		output_row_ptr[dstx*4+1] = tmpG; ///now .G...G..RGBARGBA
		output_row_ptr[dstx*4+2] = tmpB; ///now ..B...B.RGBARGBA
		output_row_ptr[dstx*4+3] = tmpA; ///now ...A...ARGBARGBA
	}
}

void ScaleRG48Row( unsigned short *input_row_ptr,
				  unsigned short *output_row_ptr,
				  short *rowScaleFactors )
{
	int dstx, srcx, srcmix, tmpR, tmpG, tmpB;
	
	short *ptrL = rowScaleFactors;
	
	while((dstx = *ptrL++) != -1)
	{
		tmpR = tmpG = tmpB = 0;
		while((srcx = *ptrL++) != -1)
		{
			srcmix = *ptrL++;
			tmpR += input_row_ptr[srcx*3 + 0] * srcmix;
			tmpG += input_row_ptr[srcx*3 + 1] * srcmix;
			tmpB += input_row_ptr[srcx*3 + 2] * srcmix;
		}
		
		tmpR >>= 8;
		if (tmpR > USHRT_MAX) tmpR = USHRT_MAX;
		if (tmpR < 0) tmpR = 0;
		
		tmpG >>= 8;
		if(tmpG > USHRT_MAX) tmpG = USHRT_MAX;
		if(tmpG < 0) tmpG = 0;
		
		tmpB >>= 8;
		if(tmpB > USHRT_MAX) tmpB = USHRT_MAX;
		if(tmpB < 0) tmpB = 0;
		
		output_row_ptr[dstx*3 + 0] = tmpR; ///now R..R..RGBRGB
		output_row_ptr[dstx*3 + 1] = tmpG; ///now .G..G.RGBRGB
		output_row_ptr[dstx*3 + 2] = tmpB; ///now ..B..BRGBRGB
	}
}

void ScaleDPX0Row( unsigned long *input_row_ptr,
				  unsigned long *output_row_ptr,
				  short *rowScaleFactors )
{
	int dstx, srcx, srcmix, tmpR, tmpG, tmpB;
	
	short *ptrL = rowScaleFactors;
	
	while((dstx = *ptrL++) != -1)
	{
		tmpR = tmpG = tmpB = 0;
		while((srcx = *ptrL++) != -1)
		{
			int r,g,b;
			srcmix = *ptrL++;
			Unpack10(input_row_ptr[srcx], &r, &g, &b);
			tmpR += r * srcmix;
			tmpG += g * srcmix;
			tmpB += b * srcmix;
		}
		
		tmpR >>= 8;
		if (tmpR > USHRT_MAX) tmpR = USHRT_MAX;
		if (tmpR < 0) tmpR = 0;
		
		tmpG >>= 8;
		if(tmpG > USHRT_MAX) tmpG = USHRT_MAX;
		if(tmpG < 0) tmpG = 0;
		
		tmpB >>= 8;
		if(tmpB > USHRT_MAX) tmpB = USHRT_MAX;
		if(tmpB < 0) tmpB = 0;
		
		output_row_ptr[dstx] = Pack10( tmpR, tmpG, tmpB );
	}
}

void ScaleR408Row( unsigned char *input_row_ptr,
					unsigned short *output_row_ptr,
					short *rowScaleFactors )
{

	int dstx, srcx, srcmix, tmpR, tmpG, tmpB, tmpA;

	short *ptrL = rowScaleFactors;

	while((dstx = *ptrL++) != -1)
	{
		tmpR = tmpG = tmpB = tmpA = 0;
		while((srcx = *ptrL++) != -1)
		{
			srcmix = *ptrL++;
			tmpA += input_row_ptr[srcx*4]*srcmix;
			tmpB += input_row_ptr[srcx*4+1]*srcmix;
			tmpG += input_row_ptr[srcx*4+2]*srcmix;
			tmpR += input_row_ptr[srcx*4+3]*srcmix;
		}
		//tmpR >>= 8;
		if(tmpR > 65535) tmpR = 65535;
		if(tmpR < 0) tmpR = 0;

		//tmpG >>= 8;
		if(tmpG > 65535) tmpG = 65535;
		if(tmpG < 0) tmpG = 0;

		//tmpB >>= 8;
		if(tmpB > 65535) tmpB = 65535;
		if(tmpB < 0) tmpB = 0;
		
		//tmpA >>= 8;
		if(tmpA > 65535) tmpA = 65535;
		if(tmpA < 0) tmpA = 0;
		
		output_row_ptr[dstx*4] = tmpR;   ///now R..R..RGBRGB
		output_row_ptr[dstx*4+1] = tmpG; ///now .G..G.RGBRGB
		output_row_ptr[dstx*4+2] = tmpB; ///now ..B..BRGBRGB
		output_row_ptr[dstx*4+3] = tmpA; ///now ..B..BRGBRGB
	}
}

void ScaleB64ARow( unsigned short *input_row_ptr,
				   unsigned short *output_row_ptr,
				   short *rowScaleFactors,
				   int byte_swap_flag )
{
	int dstx, srcx, srcmix, tmpR, tmpG, tmpB, tmpA;

	short *ptrL = rowScaleFactors;
	while((dstx = *ptrL++) != -1)
	{
        tmpR = tmpG = tmpB = tmpA = 0;
		while((srcx = *ptrL++) != -1)
		{
			srcmix = *ptrL++;
			if( byte_swap_flag )
			{
				tmpR += SwapInt16(input_row_ptr[srcx*4 + 1]) * srcmix;
				tmpG += SwapInt16(input_row_ptr[srcx*4 + 2]) * srcmix;
				tmpB += SwapInt16(input_row_ptr[srcx*4 + 3]) * srcmix;
				tmpA += SwapInt16(input_row_ptr[srcx*4 + 3]) * srcmix;
			}
			else
			{
				tmpR += input_row_ptr[srcx*4 + 1] * srcmix;
				tmpG += input_row_ptr[srcx*4 + 2] * srcmix;
				tmpB += input_row_ptr[srcx*4 + 3] * srcmix;
				tmpA += input_row_ptr[srcx*4 + 3] * srcmix;
			}
		}

		tmpR >>= 8;
		if (tmpR > USHRT_MAX) tmpR = USHRT_MAX;
		if (tmpR < 0) tmpR = 0;

		tmpG >>= 8;
		if(tmpG > USHRT_MAX) tmpG = USHRT_MAX;
		if(tmpG < 0) tmpG = 0;

		tmpB >>= 8;
		if(tmpB > USHRT_MAX) tmpB = USHRT_MAX;
		if(tmpB < 0) tmpB = 0;
		
		tmpA >>= 8;
		if(tmpA > USHRT_MAX) tmpA = USHRT_MAX;
		if(tmpA < 0) tmpA = 0;
		
		output_row_ptr[dstx*4 + 0] = tmpR; ///now R...R...RGBARGBA
		output_row_ptr[dstx*4 + 1] = tmpG; ///now .G...G..RGBARGBA
		output_row_ptr[dstx*4 + 2] = tmpB; ///now ..B...B.RGBARGBA
		output_row_ptr[dstx*4 + 3] = tmpA; ///now ...A...ARGBARGBA
	}
}

void ScaleYUVRowLuma( unsigned char *input_row_ptr,
					  unsigned char *output_row_ptr,
					  short *rowScaleFactors )
{
	short *ptr = rowScaleFactors;
	int dstx;
	int srcx;
	int srcmix;
	int tmpY;

	while((dstx = *(ptr++)) != -1)
	{
		tmpY = 0;
		while((srcx = *(ptr++)) != -1)
		{
			srcmix = *(ptr++);
			tmpY += input_row_ptr[srcx*2+1]*srcmix; //*2 UYVY
		}
		tmpY >>= 8;
		if (tmpY > 255) tmpY = 255;
		else if (tmpY < 0) tmpY = 0;

		output_row_ptr[dstx*2+1] = tmpY;	///now Y.Y.YUYV
	}
}

void ScaleYUVRowChroma( unsigned char *input_row_ptr,
						unsigned char *output_row_ptr,
						short *rowScaleFactors )
{
	short *ptr = rowScaleFactors;
	int dstx;
	int srcx;
	int srcmix;
	int tmpU;
	int tmpV;

	while((dstx = *(ptr++)) != -1)
	{
		tmpU = tmpV = 0;
		while((srcx = *(ptr++)) != -1)
		{
			srcmix = *(ptr++);
			tmpU += input_row_ptr[srcx*4]*srcmix; //*4 YUYV
			tmpV += input_row_ptr[srcx*4+2]*srcmix; //*4 YUYV
		}
		tmpU >>= 8;
		if (tmpU > 255) tmpU = 255;
		else if(tmpU < 0) tmpU = 0;

		tmpV >>= 8;
		if (tmpV > 255) tmpV = 255;
		else if (tmpV < 0) tmpV = 0;

		output_row_ptr[dstx*4] = tmpU;		//now .U...U..YUYV
		output_row_ptr[dstx*4+2] = tmpV;		//now ...V...VYUYV
	}
}

void ScaleYUV64ColumnValues(unsigned short *input, int stride,
					   lanczosmix *lmY, int sampleCount,
					   int &Y1, int &U, int &Y2, int &V)
{
	unsigned short *YUVptr;

	Y2 = Y1 = U = V = 0;


	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		YUVptr = input + stride * lmY[i].srcline;
		Y1 += *YUVptr++ * mix;
		U += *YUVptr++ * mix;
		Y2 += *YUVptr++ * mix;
		V += *YUVptr++ * mix;
	}

	Y1 >>= 8;
	U >>= 8;
	Y2 >>= 8;

	V >>= 8;

	if (Y1 > 65535) Y1 = 65535;
	if (Y1 < 0) Y1 = 0;

	if (Y2 > 65535) Y2 = 65535;
	if (Y2 < 0) Y2 = 0;

	if (U > 65535) U = 65535;
	if (U < 0) U = 0;

	if (V > 65535) V = 65535;
	if (V < 0) V = 0;
}

void ScaleYU64Column( int row,
					 int outputWidth,
					 unsigned char *input_row_ptr,
					 unsigned char *output_row_ptr,
					 COL_SCALE_FACTORS *colScaleFactors)
{
	int x,samples = 0;
	unsigned short *YUVptr = (unsigned short *)input_row_ptr;
	unsigned short *outptr = (unsigned short *)output_row_ptr;
	lanczosmix	*newMix;
	int			scaledStride = outputWidth*2;
	unsigned short *sourceYUV = YUVptr - row*scaledStride;

	samples = colScaleFactors[row].sampleCount;
	newMix = colScaleFactors[row].lmY;
	for (x=0; x<outputWidth/2; x++)
	{
		int	y1, y2, u, v;
		if(samples==0)
		{
			y1 = *YUVptr++;
			u = *YUVptr++;
			y2 = *YUVptr++;
			v = *YUVptr++;
		}
		else
		{
			// scale the column values
			ScaleYUV64ColumnValues(sourceYUV, scaledStride,
								   newMix, samples,
								   y1, u, y2, v);
			sourceYUV += 4;
		}
		*(outptr++) = y1;
		*(outptr++) = u;
		*(outptr++) = y2;
		*(outptr++) = v;
	}
}

void ScaleYU64ToBGRA64Column( int row,
							  int outputWidth,
							  unsigned char *input_row_ptr,
							  unsigned char *output_row_ptr,
							  COL_SCALE_FACTORS *colScaleFactors,
							  int byte_swap_flag,
							  int gamma,
							  void (*GammaFixRGBA)( unsigned char *outputBuffer, int width, int to2point2 ))
{
	const int alpha = USHRT_MAX;
	//int renderFieldType = 0;
	int x,samples = 0;
	unsigned short *YUVptr = (unsigned short *)input_row_ptr;
	unsigned short *outptr = (unsigned short *)output_row_ptr;
	lanczosmix	*newMix;
	int			scaledStride = outputWidth*3;
	unsigned short *sourceYUV = YUVptr - row*scaledStride;
	CImageConverterYU64ToRGB	converter(false,false);

	// See if we need to generate the scale factors

	// now process the width of the output row..
	samples = colScaleFactors[row].sampleCount;
	newMix = colScaleFactors[row].lmY;
	for (x=0; x<outputWidth; x++)
	{
		int	y1, y2, u, v;
		int	r, g, b;
		if(samples==0)
		{
			y1 = *YUVptr++;
			u = *YUVptr++;
			y2 = *YUVptr++;
			v = *YUVptr++;
		}
		else
		{
			// scale the column values
			ScaleYUV64ColumnValues(sourceYUV, scaledStride,
								   newMix, samples,
								   y1, u, y2, v);						// TODO: Need to check this YU64 is YUYV
			sourceYUV += 4;
		}
		// Convert to BGRA64
		converter.ConvertToBGRA64(y1, v, u, r, g, b);

		if (byte_swap_flag)
		{
			// Swap the bytes in each component of the output pixel
			*(outptr++) = SwapInt16(alpha);
			*(outptr++) = SwapInt16(r);
			*(outptr++) = SwapInt16(g);
			*(outptr++) = SwapInt16(b);
		}
		else
		{
			// Output the BGRA64 value without swapping bytes
			*(outptr++) = alpha;
			*(outptr++) = r;
			*(outptr++) = g;
			*(outptr++) = b;
		}
		// Convert to BGRA64
		converter.ConvertToBGRA64(y2, v, u, r, g, b);

		if (byte_swap_flag)
		{
			// Swap the bytes in each component of the output pixel
			*(outptr++) = SwapInt16(alpha);
			*(outptr++) = SwapInt16(r);
			*(outptr++) = SwapInt16(g);
			*(outptr++) = SwapInt16(b);
		}
		else
		{
			// Output the BGRA64 value without swapping bytes
			*(outptr++) = alpha;
			*(outptr++) = r;
			*(outptr++) = g;
			*(outptr++) = b;
		}
	}
}
void ScaleYU64ToR4FLColumn( int row,
							  int outputWidth,
							  unsigned char *input_row_ptr,
							  unsigned char *output_row_ptr,
							  COL_SCALE_FACTORS *colScaleFactors)
{
	const float normalize = 65280.0;
	//int renderFieldType = 0;
	int x,samples = 0;
	unsigned short *YUVptr = (unsigned short *)input_row_ptr;
	float *outptr = (float *)output_row_ptr;
	lanczosmix	*newMix;
	int			scaledStride = outputWidth*3;
	unsigned short *sourceYUV = YUVptr - row*scaledStride;
	CImageConverterYU64ToRGB	converter(false,false);

	// See if we need to generate the scale factors

	// now process the width of the output row..
	samples = colScaleFactors[row].sampleCount;
	newMix = colScaleFactors[row].lmY;
	for (x=0; x<outputWidth/2; x++)
	{
		int	y1, y2, u, v;
		float a, fY, fU, fV;
		if(samples==0)
		{
			y1 = *YUVptr++;
			u = *YUVptr++;
			y2 = *YUVptr++;
			v = *YUVptr++;
		}
		else
		{
			// scale the column values
			ScaleYUV64ColumnValues(sourceYUV, scaledStride,
								   newMix, samples,
								   y1, u, y2, v);
			sourceYUV += 4;
		}
		a = 1.0;
		fY = (float)y1/normalize;
		fU = (float)u/normalize;
		fV = (float)v/normalize;
		*(outptr++) =  a;
		*(outptr++) =  fY;
		*(outptr++) =  fU;	// Cb
		*(outptr++) =  fV;	// Cr
		fY = (float)y2/normalize;
		*(outptr++) =  a;
		*(outptr++) =  fY;
		*(outptr++) =  fU;	// Cb
		*(outptr++) =  fV;	// Cr
	}
}

void ScaleRGBColumnValues(unsigned short *input, int stride,
				  lanczosmix *lmY, int sampleCount,
				  int &R, int &G, int &B, int &A)
{
	unsigned short *RGBptr;

	R = G = B = A = 0;

	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		int sLine = lmY[i].srcline;
		RGBptr = input + stride * sLine;
		R += *RGBptr++ * mix;
		G += *RGBptr++ * mix;
		B += *RGBptr++ * mix;
		A += *RGBptr++ * mix;
	}

	R >>= 8;
	G >>= 8;
	B >>= 8;
	A >>= 8;
}

void ScaleRGB32Column( int row,
					   int outputWidth,
					   unsigned short *input_row_ptr,
					   unsigned char *output_row_ptr,
					   COL_SCALE_FACTORS *colScaleFactors,
					   int byte_swap_flag,
					   int gamma,
					   void (*GammaFixRGBA)( unsigned char *outputBuffer, int width, int to2point2 ))
//
//	byte_swap_flag == 0: produce ARGB 1: BGRA
{
	unsigned char *ptr = output_row_ptr;
	unsigned short *RGBptr = input_row_ptr;
	int	x;
	int scaledStride = outputWidth*4;
	unsigned short *sourceRGB = input_row_ptr - row*scaledStride;

	for(x=0;x<outputWidth;x++)
	{
		int	R,G,B,A;
		if( colScaleFactors[row].sampleCount==0 )
		{
			R = *RGBptr++;
			G = *RGBptr++;
			B = *RGBptr++;
			A = *RGBptr++;
		}
		else
		{
			// scale the values
			ScaleRGBColumnValues(sourceRGB, scaledStride,
								 colScaleFactors[row].lmY, colScaleFactors[row].sampleCount,
								 R, G, B, A);
			sourceRGB += 4;
		}

		// convert to 8 bit and clamp

		R >>= 8;
		G >>= 8;
		B >>= 8;
		A >>= 8;

		if(R < 0) R=0; else if(R>255) R=255;
		if(G < 0) G=0; else if(G>255) G=255;
		if(B < 0) B=0; else if(B>255) B=255;
		if(A < 0) A=0; else if(A>255) A=255;

		if(byte_swap_flag)
		{
			*ptr++ = R;	//B
			*ptr++ = G;
			*ptr++ = B; //R
			*ptr++ = A;
		}
		else
		{
			*ptr++ = 255;
			*ptr++ = B;	// R - all red
			*ptr++ = G; // G
			*ptr++ = R;	// B
			//fprintf(stderr,"%02x %02x %02x %02x\n",A,R,G,B);
		}
	}
#ifndef _WIN32
	// Do some gamma conversion if needed
	if(byte_swap_flag)
	{
		if( gamma==1 ) {
			GammaFixRGBA((unsigned char *)output_row_ptr, outputWidth, 1);
		} else if ((gamma==2) || (gamma==3)) {
			GammaFixRGBA((unsigned char *)output_row_ptr, outputWidth, 0);
		}
	}
	else
	{
		if( gamma==1 ) {
			GammaFixRGBA((unsigned char *)output_row_ptr, outputWidth, 1);
		} else if (gamma==2) {
			GammaFixRGBA((unsigned char *)output_row_ptr, outputWidth, 0);
		}
	}
#endif
}
void ScaleR408Column( int row,
					   int outputWidth,
					   unsigned short *input_row_ptr,
					   unsigned char *output_row_ptr,
					   COL_SCALE_FACTORS *colScaleFactors)
{
	unsigned char *ptr = output_row_ptr;
	unsigned short *R408ptr = input_row_ptr;
	int	x;
	int scaledStride = outputWidth*3;
	unsigned short *sourceR408 = input_row_ptr - row*scaledStride;

	for(x=0;x<outputWidth;x++)
	{
		int	Y,U,V, A;
		if( colScaleFactors[row].sampleCount==0 )
		{
			V = *R408ptr++;
			U = *R408ptr++;
			Y = *R408ptr++;
			A = *R408ptr++;
		}
		else
		{
			// scale the values
			ScaleRGBColumnValues(sourceR408, scaledStride,
								 colScaleFactors[row].lmY, colScaleFactors[row].sampleCount,
								 V, U, Y, A);
			sourceR408 += 4;
		}

		// convert to 8 bit and clamp

		Y >>= 8;
		U >>= 8;
		V >>= 8;
		A >>= 8;

		if(Y < 0) Y=0; else if(Y>219) Y=219;
		if(U < 0) U=0; else if(U>255) U=255;
		if(V < 0) V=0; else if(V>255) V=255;
		if(A < 0) A=0; else if(A>255) A=255;

		//*ptr++ = U;
		//*ptr++ = V;
		*ptr++ = A;		// Alpha
		*ptr++ = Y;			// Y
		*ptr++ = U;
		*ptr++ = V;
	}
}

void ScaleYUVColumnValues(unsigned char *input,
					   int stride,
					   lanczosmix *lmY,
					   int sampleCount,
					   int &y1,
					   int &u1,
					   int &y2,
					   int &v1)
{
	const int yuv_max = 255;
	unsigned char *yuvptr;

	y1 = y2 = 0;
	u1 = v1 = 0;

	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		yuvptr = input + stride * lmY[i].srcline;
		u1 += *(yuvptr++) * mix;
		y1 += *(yuvptr++) * mix;
		v1 += *(yuvptr++) * mix;
		y2 += *(yuvptr++) * mix;
	}

	y1 >>= 8;
	u1 >>= 8;
	y2 >>= 8;
	v1 >>= 8;

	if (y1 > yuv_max) y1 = yuv_max;
	else if (y1 < 0) y1 = 0;

	if (u1 > yuv_max) u1 = yuv_max;
	else if (u1 < 0) u1 = 0;

	if (y2 > yuv_max) y2 = yuv_max;
	else if (y2 < 0) y2 = 0;

	if (v1 > yuv_max) v1 = yuv_max;
	if (v1 < 0) v1 = 0;
}

void ScaleYUVColumn( int row,
					 int outputWidth,
					 unsigned char *input_row_ptr,
					 unsigned char *output_row_ptr,
					 COL_SCALE_FACTORS *colScaleFactors )
{
	int		scaledStride = outputWidth*2;
	unsigned char *yuvptr = input_row_ptr;
	unsigned char *outptr = output_row_ptr;
	unsigned char *scaled_column_ptr = input_row_ptr - row * scaledStride;

	for(int column=0; column<outputWidth; column += 2)
	{
		int y1, u1, y2, v1;
		if( colScaleFactors[row].sampleCount==0)
		{
			u1 = *(yuvptr++);
			y1 = *(yuvptr++);
			v1 = *(yuvptr++);
			y2 = *(yuvptr++);
		}
		else
		{
			ScaleYUVColumnValues(scaled_column_ptr, scaledStride, colScaleFactors[row].lmY,
								 colScaleFactors[row].sampleCount, y1, u1, y2, v1);
			scaled_column_ptr += 4;
		}
		*(outptr++) = u1;
		*(outptr++) = y1;
		*(outptr++) = v1;
		*(outptr++) = y2;

	}
}
void ScaleRG48ColumnValues(unsigned short *input, int stride,
						   lanczosmix *lmY, int sampleCount,
						   int &R, int &G, int &B)
{
	unsigned short *RGBptr;
	
	R = G = B = 0;
	
	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		RGBptr = input + stride * lmY[i].srcline;
		R += *RGBptr++ * mix;
		G += *RGBptr++ * mix;
		B += *RGBptr++ * mix;
	}
	
	R >>= 8;
	G >>= 8;
	B >>= 8;
	
	if (R < 0) R = 0;
	else if (R > USHRT_MAX) R = USHRT_MAX;
	
	if (G < 0) G = 0;
	else if (G > USHRT_MAX) G = USHRT_MAX;
	
	if (B < 0) B = 0;
	else if (B > USHRT_MAX) B = USHRT_MAX;
}

void ScaleRG48Column( int row,
					 int outputWidth,
					 unsigned short *input_row_ptr,
					 unsigned char *output_row_ptr,
					 COL_SCALE_FACTORS *colScaleFactors,
					 int byte_swap_flag )
{
	int scaledStride = outputWidth * 3;
	unsigned short *sourceRGB = input_row_ptr - row*scaledStride;
	unsigned short *RGBptr = input_row_ptr;
	unsigned short *BGRA = (unsigned short *)output_row_ptr;
	for(int column=0; column<outputWidth; column++)
	{
		int R,G,B;
		if(colScaleFactors[row].sampleCount==0)
		{
			R = *RGBptr++;
			G = *RGBptr++;
			B = *RGBptr++;
		}
		else
		{
			ScaleRG48ColumnValues(sourceRGB, scaledStride, colScaleFactors[row].lmY,
								  colScaleFactors[row].sampleCount, R, G, B);
			sourceRGB += 3;
		}
		/* done above
		 if (R < 0) R = 0; else if (R > USHRT_MAX) R = USHRT_MAX;
		 if (G < 0) G = 0; else if (G > USHRT_MAX) G = USHRT_MAX;
		 if (B < 0) B = 0; else if (B > USHRT_MAX) B = USHRT_MAX;
		 */
		
		if (!byte_swap_flag)
		{
			*(BGRA++) = R;
			*(BGRA++) = G;
			*(BGRA++) = B;
		}
		else
		{
			*(BGRA++) = SwapInt16(R);
			*(BGRA++) = SwapInt16(G);
			*(BGRA++) = SwapInt16(B);
		}
	}
}

void ScaleDPX0ColumnValues(unsigned long *input, int stride,
							lanczosmix *lmY, int sampleCount,
							int &R, int &G, int &B)
{
	unsigned long *RGBptr;
	
	R = G = B = 0;
	
	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		int r,g,b;
		RGBptr = input + stride * lmY[i].srcline;
		Unpack10(*RGBptr, &r, &g, &b);
		R += r * mix;
		G += g * mix;
		B += b * mix;
	}
	
	R >>= 8;
	G >>= 8;
	B >>= 8;
	
	if (R < 0) R = 0;
	else if (R > USHRT_MAX) R = USHRT_MAX;
	
	if (G < 0) G = 0;
	else if (G > USHRT_MAX) G = USHRT_MAX;
	
	if (B < 0) B = 0;
	else if (B > USHRT_MAX) B = USHRT_MAX;
}

void ScaleDPX0Column( int row,
					 int outputWidth,
					 unsigned long *input_row_ptr,
					 unsigned char *output_row_ptr,
					 COL_SCALE_FACTORS *colScaleFactors,
					 int byte_swap_flag )
{
	unsigned long *sourceRGB = input_row_ptr - row*outputWidth;
	unsigned long *RGBptr = input_row_ptr;
	unsigned long *BGRA = (unsigned long *)output_row_ptr;
	for(int column=0; column<outputWidth; column++)
	{
		int R,G,B;
		if(colScaleFactors[row].sampleCount==0)
		{
			Unpack10(*(RGBptr++), &R, &G, &B);
		}
		else
		{
			ScaleDPX0ColumnValues(sourceRGB, outputWidth, colScaleFactors[row].lmY,
								  colScaleFactors[row].sampleCount, R, G, B);
			sourceRGB += 1;
		}
		
		*(BGRA++) = Pack10(R, G, B);
	}
}
void ScaleB64AColumnValues(unsigned short *input, int stride,
						   lanczosmix *lmY, int sampleCount,
						   int &R, int &G, int &B, int &A)
{
	unsigned short *RGBptr;

	R = G = B = A = 0;

	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		RGBptr = input + stride * lmY[i].srcline;
		R +=  *RGBptr++  * mix;
		G +=  *RGBptr++  * mix;
		B +=  *RGBptr++  * mix;
		A +=  *RGBptr++  * mix;
	}

	R >>= 8;
	G >>= 8;
	B >>= 8;
	A >>= 8;

	if (R < 0) R = 0;
	else if (R > USHRT_MAX) R = USHRT_MAX;

	if (G < 0) G = 0;
	else if (G > USHRT_MAX) G = USHRT_MAX;

	if (B < 0) B = 0;
	else if (B > USHRT_MAX) B = USHRT_MAX;
	
	if (A < 0) A = 0;
	else if (A > USHRT_MAX) A = USHRT_MAX;
}

void ScaleB64AColumn( int row,
					  int outputWidth,
					  unsigned short *input_row_ptr,
					  unsigned char *output_row_ptr,
					  COL_SCALE_FACTORS *colScaleFactors,
					  int byte_swap_flag )
{
	int scaledStride = outputWidth * 4;
	unsigned short *sourceRGB = input_row_ptr - row*scaledStride;
	unsigned short *RGBptr = input_row_ptr;
	unsigned short *BGRA = (unsigned short *)output_row_ptr;

	for(int column=0; column<outputWidth; column++)
	{
		int R,G,B,A;
		if(colScaleFactors[row].sampleCount==0)
		{
			R = *RGBptr++;
			G = *RGBptr++;
			B = *RGBptr++;
			A = *RGBptr++;
		}
		else
		{
			ScaleB64AColumnValues(sourceRGB, scaledStride, colScaleFactors[row].lmY,
								  colScaleFactors[row].sampleCount, R, G, B, A);
			sourceRGB += 4;
		}
		if (R < 0) R = 0; else if (R > USHRT_MAX) R = USHRT_MAX;
		if (G < 0) G = 0; else if (G > USHRT_MAX) G = USHRT_MAX;
		if (B < 0) B = 0; else if (B > USHRT_MAX) B = USHRT_MAX;
		if (A < 0) A = 0; else if (A > USHRT_MAX) A = USHRT_MAX;
#ifdef _WIN32
		if (!byte_swap_flag)
		{
			*(BGRA++) = A;
			*(BGRA++) = R;
			*(BGRA++) = G;
			*(BGRA++) = B;
		}
		else
		{
			*(BGRA++) = SwapInt16(A);
			*(BGRA++) = SwapInt16(R);
			*(BGRA++) = SwapInt16(G);
			*(BGRA++) = SwapInt16(B);
		}
#elif __APPLE__
		*(BGRA++) = CFSwapInt16HostToBig(A);
		*(BGRA++) = CFSwapInt16HostToBig(R);
		*(BGRA++) = CFSwapInt16HostToBig(G);
		*(BGRA++) = CFSwapInt16HostToBig(B);
#else
		*(BGRA++) = SwapInt16(A);
		*(BGRA++) = SwapInt16(R);
		*(BGRA++) = SwapInt16(G);
		*(BGRA++) = SwapInt16(B);
#endif
	}
}

void ScaleB64AToBGRAColumn( int row,
							int outputWidth,
							unsigned short *input_row_ptr,
							unsigned char *output_row_ptr,
							COL_SCALE_FACTORS *colScaleFactors,
							int byte_swap_flag,
							int gamma,
							void (*GammaFixRGBA)( unsigned char *outputBuffer, int width, int to2point2 ))
{
	int scaledStride = outputWidth * 4;
	unsigned short *sourceRGB = input_row_ptr - row*scaledStride;
	unsigned short *RGBptr = input_row_ptr;
	unsigned char *BGRA = output_row_ptr;

	for(int column=0; column<outputWidth; column++)
	{
		int R,G,B,A;
		if(colScaleFactors[row].sampleCount==0)
		{
			R = *RGBptr++;
			G = *RGBptr++;
			B = *RGBptr++;
			A = *RGBptr++;
		}
		else
		{
			ScaleB64AColumnValues(sourceRGB, scaledStride, colScaleFactors[row].lmY,
								  colScaleFactors[row].sampleCount, R, G, B, A);
			sourceRGB += 4;
		}

		R >>= 8;
		G >>= 8;
		B >>= 8;
		A >>= 8;

		if (R < 0) R = 0; else if (R > 255) R = 255;
		if (G < 0) G = 0; else if (G > 255) G = 255;
		if (B < 0) B = 0; else if (B > 255) B = 255;
		if (A < 0) A = 0; else if (A > 255) A = 255;

		*(BGRA++) = B;
		*(BGRA++) = G;
		*(BGRA++) = R;
		*(BGRA++) = A;
	}
	// Have the row scaled, now do the gamma correction if needed
#ifndef _WIN32
	// Do some gamma conversion if needed
	if(byte_swap_flag)
	{
		if( gamma==1 ) {
			GammaFixRGBA((unsigned char *)output_row_ptr, outputWidth, 1);
		} else if ((gamma==2) || (gamma==3)) {
			GammaFixRGBA((unsigned char *)output_row_ptr, outputWidth, 0);
		}
	}
	else
	{
		if( gamma==1 ) {
			GammaFixRGBA((unsigned char *)output_row_ptr, outputWidth, 1);
		} else if (gamma==2) {
			GammaFixRGBA((unsigned char *)output_row_ptr, outputWidth, 0);
		}
	}
#endif
}
void ScaleB64AToR4FLColumn( int row,
							int outputWidth,
							unsigned short *input_row_ptr,
							unsigned char *output_row_ptr,
							COL_SCALE_FACTORS *colScaleFactors,
							int whitepoint)
{
	int scaledStride = outputWidth * 4;
	unsigned short *sourceRGB = input_row_ptr - row*scaledStride;
	unsigned short *RGBptr = input_row_ptr;
	float *output_ptr = (float *)output_row_ptr;
	float fwhitepoint = (float)whitepoint;


	for(int column=0; column<outputWidth; column++)
	{
		int R,G,B,A;
		if(colScaleFactors[row].sampleCount==0)
		{
			R = *RGBptr++;
			G = *RGBptr++;
			B = *RGBptr++;
			A = *RGBptr++;
		}
		else
		{
			ScaleB64AColumnValues(sourceRGB, scaledStride, colScaleFactors[row].lmY,
								  colScaleFactors[row].sampleCount, R, G, B, A);
			sourceRGB += 4;
		}

		float r = ((float)R)/fwhitepoint;
		float g = ((float)G)/fwhitepoint;
		float b = ((float)B)/fwhitepoint;
		float a = ((float)A)/fwhitepoint;

		float y =  0.183*r + 0.614*g + 0.062*b;
		float v = -0.101*r - 0.338*g + 0.439*b + 0.502;
		float u =  0.439*r - 0.399*g - 0.040*b + 0.502;

		// Store the yuva in the output row
		*(output_ptr++) =  a;
		*(output_ptr++) =  y;
		*(output_ptr++) =  v;	// Cb
		*(output_ptr++) =  u;	// Cr
	}
}


// Scale and convert the input image to the 10-bit RGB pixel format for DPX files
void CImageScalerConverterNV12ToRGB::ScaleToDPX0(void *input_buffer,
												 int input_width,
												 int input_height,
												 int input_pitch,
												 void *output_buffer,
												 int output_width,
												 int output_height,
												 int output_pitch,
												 int swap_bytes_flag)
{
	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(output_width, input_height)) {
		return;
	}

	int chroma_width = (input_width >> 1);
	ComputeRowScaleFactors(scalefactorsL, input_width, output_width);
	ComputeRowScaleFactors(scalefactorsC, chroma_width, output_width);

	ScaleRowValues(input_buffer, input_width, input_height, input_pitch, horizontalscale, output_width);
	
	int renderFieldType = 0;

	uint8_t *outbuf = reinterpret_cast<uint8_t *>(output_buffer);

	// Scale the luma and chroma offsets to 16-bit precision
	const int32_t luma_offset = (YUVToRGB<uint16_t>::luma_offset << 8);
	const int32_t chroma_offset = (YUVToRGB<uint16_t>::chroma_offset << 8);

	// Scale the converted RGB values to 16-bit precision
	const int shift = 13;

	for (int row = 0; row < output_height; row++)
	{
		int x;
		int samples = 0;
		lanczosmix lmY[200];

		uint32_t *outptr = reinterpret_cast<uint32_t *>(outbuf + (row * output_pitch));

		samples = ComputeColumnScaleFactors(row, input_height, output_height, renderFieldType, lmY);

		uint16_t *YUV_column_ptr = reinterpret_cast<uint16_t *>(horizontalscale);

		// Three 16-bit values for each column in the horizontally scaled results
		int scaled_stride = 3 * output_width;

		for (x = 0; x < output_width; x++)
		{
			int32_t Y, U, V;

			if (input_height == output_height)
			{
				uint16_t *YUV_ptr = YUV_column_ptr + row * scaled_stride;
				Y = *(YUV_ptr++);
				U = *(YUV_ptr++);
				V = *(YUV_ptr++);
			}
			else
			{
				ScaleColumnValues(YUV_column_ptr, scaled_stride, lmY, samples, Y, U, V);
			}

			// Advance to the next column in the horizontally scaled results
			YUV_column_ptr += 3;

			// Apply the luma and chroma offsets before conversion to RGB
			Y -= luma_offset;
			U -= chroma_offset;
			V -= chroma_offset;

			// Convert the scaled results to RGB
			int32_t R = C_y * Y + C_rv * V;
			int32_t G = C_y * Y - C_gv * V - C_gu * U;
			int32_t B = C_y * Y + C_bu * U;

			// Scale the output values to 16 bits
			R = Clamp16u(R >> shift);
			G = Clamp16u(G >> shift);
			B = Clamp16u(B >> shift);

			// Pack and output the pair of RGB tuples
			*(outptr++) = Pack10(R, G, B);
		}
	}
}

// Scale and convert the input image to the 8-bit BGRA pixel format for thumbnails
void CImageScalerConverterNV12ToRGB::ScaleToBGRA(void *input_buffer,
												 int input_width,
												 int input_height,
												 int input_pitch,
												 void *output_buffer,
												 int output_width,
												 int output_height,
												 int output_pitch,
												 int swap_bytes_flag)
{
	// Allocate scratch memory for use by the interpolator
	if (!AllocScratchMemory(output_width, input_height)) {
		return;
	}

	int chroma_width = (input_width >> 1);
	ComputeRowScaleFactors(scalefactorsL, input_width, output_width);
	ComputeRowScaleFactors(scalefactorsC, chroma_width, output_width);

	ScaleRowValues(input_buffer, input_width, input_height, input_pitch, horizontalscale, output_width);
	
	int renderFieldType = 0;

	uint8_t *outbuf = reinterpret_cast<uint8_t *>(output_buffer);

	// Use the default value for alpha
	const int A = 255;

	// Scale the luma and chroma offsets to 16-bit precision
	const int32_t luma_offset = (YUVToRGB<uint16_t>::luma_offset << 8);
	const int32_t chroma_offset = (YUVToRGB<uint16_t>::chroma_offset << 8);

	// Scale the converted RGB values to 8-bit precision
	const int shift = 21;

	for (int row = 0; row < output_height; row++)
	{
		int x;
		int samples = 0;
		lanczosmix lmY[200];

		uint8_t *outptr = outbuf + (row * output_pitch);

		samples = ComputeColumnScaleFactors(row, input_height, output_height, renderFieldType, lmY);

		uint16_t *YUV_column_ptr = reinterpret_cast<uint16_t *>(horizontalscale);

		// Three 16-bit values for each column in the horizontally scaled results
		int scaled_stride = 3 * output_width;

		for (x = 0; x < output_width; x++)
		{
			int32_t Y, U, V;

			if (input_height == output_height)
			{
				uint16_t *YUV_ptr = YUV_column_ptr + row * scaled_stride;
				Y = *(YUV_ptr++);
				U = *(YUV_ptr++);
				V = *(YUV_ptr++);
			}
			else
			{
				ScaleColumnValues(YUV_column_ptr, scaled_stride, lmY, samples, Y, U, V);
			}

			// Advance to the next column in the horizontally scaled results
			YUV_column_ptr += 3;

			// Apply the luma and chroma offsets before conversion to RGB
			Y -= luma_offset;
			U -= chroma_offset;
			V -= chroma_offset;

			// Convert the scaled results to RGB
			int32_t R = C_y * Y + C_rv * V;
			int32_t G = C_y * Y - C_gv * V - C_gu * U;
			int32_t B = C_y * Y + C_bu * U;

			// Scale the output values to 16 bits
			R = Clamp8u(R >> shift);
			G = Clamp8u(G >> shift);
			B = Clamp8u(B >> shift);

			// Output the BGRA tuple
			*(outptr++) = A;
			*(outptr++) = R;
			*(outptr++) = G;
			*(outptr++) = B;
		}
	}
}

// Scale the rows of luma and chroma
void
CImageScalerNV12::ScaleRowValues(void *input_buffer, int input_width, int input_height,
								 int input_pitch, uint16_t *output_buffer, int output_width)
{
	size_t luma_plane_size = (input_width * input_height);
	uint8_t *luma_input_buffer = reinterpret_cast<uint8_t *>(input_buffer);
	uint8_t *chroma_input_buffer = reinterpret_cast<uint8_t *>(input_buffer) + luma_plane_size;

	// The scaled intermediate results are stored as 16-bit YUV 4:4:4
	size_t output_pitch = (3 * sizeof(uint16_t) * output_width) / sizeof(uint16_t);

	//TODO: Check the shift amount
	//const int shift = 8;

	for (int row = 0; row < input_height; row++)
	{
		uint8_t *luma_row_ptr = luma_input_buffer + (row * input_pitch);
		uint8_t *chroma_row_ptr = chroma_input_buffer + (row/2 * input_pitch);

		uint16_t *output_row_ptr = output_buffer + (row * output_pitch);

		short *ptrL = scalefactorsL;
		short *ptrC = scalefactorsC;

		int dstx;

		// Scale the luma values
		while((dstx = *ptrL++) != -1)
		{
			assert(0 <= dstx && dstx < output_width);

			int tmpY = 0;
			int srcx;

			while((srcx = *ptrL++) != -1)
			{
				assert(0 <= srcx && srcx < input_width);

				int srcmix = *ptrL++;
				tmpY += luma_row_ptr[srcx] * srcmix;
			}

			//tmpY = Clamp16u(tmpY >> shift);
			tmpY = Clamp16u(tmpY);

			// The intermediate results have 16-bit precision
			output_row_ptr[3 * dstx + 0] = tmpY; ///now Y..Y..YUVYUV
		}

		// Scale the chroma values
		while((dstx = *ptrC++) != -1)
		{
			assert(0 <= dstx && dstx < output_width);

			int tmpU = 0;
			int tmpV = 0;
			int srcx;

			while((srcx = *ptrC++) != -1)
			{
				assert(0 <= srcx && srcx < input_width);

				int srcmix = *ptrC++;
				tmpU += chroma_row_ptr[2 * srcx + 0] * srcmix;
				tmpV += chroma_row_ptr[2 * srcx + 1] * srcmix;
			}

			//tmpU = Clamp16u(tmpU >> shift);
			//tmpV = Clamp16u(tmpV >> shift);
			tmpU = Clamp16u(tmpU);
			tmpV = Clamp16u(tmpV);

			// The intermediate results have 16-bit precision
			output_row_ptr[3 * dstx + 1] = tmpU;	//now.U..U.YUV
			output_row_ptr[3 * dstx + 2] = tmpV;	//now..V..VYUV
		}
	}
}

// Scale one row of luma values (skip the chroma values)
//void CImageScalerNV12::ScaleRowLuma(unsigned short *inputRow, unsigned short *outputRow, short *scaleFactors);

// Scale one row of chroma values (skip the luma values)
//void CImageScalerNV12::ScaleRowChroma(unsigned short *inputRow, unsigned short *outputRow, short *scaleFactors);

// Scale the luma and chroma values in the specified column
void CImageScalerNV12::ScaleColumnValues(unsigned short *input, int stride,
										 lanczosmix *lmY, int sampleCount,
										 int &Y, int &U, int &V)
{
	// The input stride is in units of luma and chroma values (not in units of bytes)
	uint16_t *YUVptr;

	Y = U = V = 0;

	for (int i = 0; i < sampleCount; i++)
	{
		int mix = lmY[i].mixval;
		YUVptr = input + stride * lmY[i].srcline;
		Y += *YUVptr++ * mix;
		U += *YUVptr++ * mix;
		V += *YUVptr++ * mix;
	}

	Y >>= 8;
	U >>= 8;
	V >>= 8;

	if (Y > 65535) Y = 65535;
	if (Y < 0) Y = 0;

	if (U > 65535) U = 65535;
	if (U < 0) U = 0;

	if (V > 65535) V = 65535;
	if (V < 0) V = 0;
}
