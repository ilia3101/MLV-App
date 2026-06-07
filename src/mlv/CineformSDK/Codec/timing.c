/*! @file timing.c

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

#include "config.h"
#include "timing.h"

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif

#if _TIMING

#if defined(_WIN32) && DEBUG
#include <tchar.h>				// For printing debug string in the console window
#endif

#include <stdio.h>
#include <assert.h>


// Define the timers here and declare references to them in the modules that use
// the timers rather than declaring the timers in a header file that would cause
// massive recompilation if the header file is changed when timers are added.

TIMER tk_compress = TIMER_INITIALIZER;		// Total compression time
TIMER tk_decompress = TIMER_INITIALIZER;	// Total decompression time
TIMER tk_spatial = TIMER_INITIALIZER;		// Spatial wavelet transform
TIMER tk_inverse = TIMER_INITIALIZER;		// Inverse wavelet transform
TIMER tk_temporal = TIMER_INITIALIZER;		// Temporal wavelet transform
TIMER tk_horizontal = TIMER_INITIALIZER;	// Horizontal wavelet transform
TIMER tk_vertical = TIMER_INITIALIZER;		// Vertical wavelet transform
TIMER tk_frame = TIMER_INITIALIZER;			// Frame (temporal and horizontal)
TIMER tk_encoding = TIMER_INITIALIZER;		// Bitstream encoding
TIMER tk_decoding = TIMER_INITIALIZER;		// Bitstream decoding
TIMER tk_quant = TIMER_INITIALIZER;			// Quantization
TIMER tk_dequant = TIMER_INITIALIZER;		// Inverse quantization
TIMER tk_convert = TIMER_INITIALIZER;		// Image conversion
TIMER tk_fastruns = TIMER_INITIALIZER;		// Decoding using fast runs
TIMER tk_putbits = TIMER_INITIALIZER;		// Inserting bits into the bitstream
//TIMER tk_lowpass = TIMER_INITIALIZER;		// Encoding the lowpass band
TIMER tk_finish = TIMER_INITIALIZER;		// Time to finish the group transform
TIMER tk_progressive = TIMER_INITIALIZER;	// Spatial transform (progressive encoding)

TIMER tk_spatial1 = TIMER_INITIALIZER;		// Spatial transform (first frame)
TIMER tk_spatial2 = TIMER_INITIALIZER;		// Spatial transform (second frame)

TIMER tk_recursive = TIMER_INITIALIZER;		// Recursive wavelet transform

COUNTER decode_lookup_count = COUNTER_INITIALIZER;		// Decodes using fast lookup
COUNTER decode_search_count = COUNTER_INITIALIZER;		// Decodes using search
COUNTER decode_byte_count = COUNTER_INITIALIZER;		// Number of bytes decoded
COUNTER sample_byte_count = COUNTER_INITIALIZER;		// Number of sample bytes
COUNTER alloc_group_count = COUNTER_INITIALIZER;		// Group allocations
COUNTER alloc_transform_count = COUNTER_INITIALIZER;	// Transform allocations
COUNTER alloc_wavelet_count = COUNTER_INITIALIZER;		// Wavelet allocations
COUNTER alloc_frame_count = COUNTER_INITIALIZER;		// Number of frames allocated
COUNTER alloc_buffer_count = COUNTER_INITIALIZER;		// Number of buffers allocated
COUNTER spatial_transform_count = COUNTER_INITIALIZER;	// Spatial transforms outside of decoding
COUNTER temporal_transform_count = COUNTER_INITIALIZER;	// Temporal transforms outside of decoding
COUNTER spatial_decoding_count = COUNTER_INITIALIZER;	// Spatial transforms during decoding
COUNTER temporal_decoding_count = COUNTER_INITIALIZER;	// Temporal transforms during decoding
COUNTER progressive_encode_count = COUNTER_INITIALIZER;	// Encoded progressive frames
COUNTER progressive_decode_count = COUNTER_INITIALIZER;	// Decoded progressive frames
COUNTER putvlcbyte_count = COUNTER_INITIALIZER;			// Number of calls to PutVlcByte()
COUNTER putzerorun_count = COUNTER_INITIALIZER;			// Number of calls to PutZeroRun()

#ifdef _WIN32
// Clock frequency of the high-resolution timer for performance measurements.
// Zero means that the timers have not been initialized or are not available.
__int64 frequency = 0;
#else
// Timebase for the absolute timer on the Macintosh
struct mach_timebase_info timebase = {0, 0};
#endif


// Counters that are defined elsewhere


BOOL InitTiming(void)
{
#ifdef _WIN32
	// Get the clock frequency
	if (!QueryPerformanceFrequency((LARGE_INTEGER *)&frequency))
	{
		assert(0);
		return FALSE;
	}
#else
	// Get the timebase of the absolute timer
	mach_timebase_info(&timebase);
#endif

	// Initialize the timers
	tk_compress = 0;
	tk_decompress = 0;
	tk_spatial = 0;
	tk_inverse = 0;
	tk_temporal = 0;
	tk_horizontal = 0;
	tk_vertical = 0;
	tk_frame = 0;
	tk_encoding = 0;
	tk_decoding = 0;
	tk_quant = 0;
	tk_dequant = 0;
	tk_convert = 0;
	tk_fastruns = 0;
	tk_putbits = 0;
	//tk_lowpass = 0;
	tk_finish = 0;
	tk_progressive = 0;

	tk_spatial1 = 0;
	tk_spatial2 = 0;

	// Zero most of the counters
	decode_byte_count = 0;
	sample_byte_count = 0;
	spatial_transform_count = 0;
	temporal_transform_count = 0;
	spatial_decoding_count = 0;
	temporal_decoding_count = 0;
	progressive_encode_count = 0;
	progressive_decode_count = 0;
	putvlcbyte_count = 0;
	putzerorun_count = 0;

	return TRUE;
}

void StartTimer(TIMER *timer)
{
#ifdef _WIN32
	LARGE_INTEGER current_time;
	QueryPerformanceCounter(&current_time);
	*timer -= current_time.QuadPart;
#else
	uint64_t current_time = mach_absolute_time();
	*timer -= current_time;
#endif
}

void StopTimer(TIMER *timer)
{
#ifdef _WIN32
	LARGE_INTEGER current_time;
	QueryPerformanceCounter(&current_time);
	*timer += current_time.QuadPart;
#else
	uint64_t current_time = mach_absolute_time();
	*timer += current_time;
#endif
}

#ifndef _WIN32

// Convert the timer value to units of seconds
float AbsoluteTimeInSeconds(TIMER timer)
{
	//float seconds = timer / AbsoluteTimerFrequency();
	float seconds = ((float)timer * (float)timebase.numer) / (1.0e9 * (float)timebase.denom);
	return seconds;
}

// Compute the resolution of the absolute timer (in nanoseconds)
float AbsoluteTimerResolution()
{
	float resolution = (float)timebase.numer / (float)timebase.denom;
	return resolution;
}

#endif

#define GMEM_FLAGS (GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE)

#ifdef _WIN32
void PrintStatistics(FILE *logfile, int frame_count, HWND hwnd, char *results)
#else
void PrintStatistics(FILE *logfile, int frame_count, void *unused, char *results)
#endif
{
	TIMER tk_itemized;
	TIMER tk_uncounted;
	TIMER tk_total;
	float total_fps;
	float decode_search_ratio;

	// Adjust the encoding timer to subtract time counted by other timers
	//tk_encoding -= tk_putbits;
	//tk_encoding -= tk_lowpass;

	// Compute the total time for compression and decompression
	tk_total = tk_compress + tk_decompress;

#if _THREADED_ENCODER
	// Compute the total amount of time that was itemized
	//tk_progressive += tk_spatial1 + tk_spatial2;
	//tk_spatial = 0;
	//tk_spatial1 = 0;
	//tk_spatial2 = 0;
	//tk_temporal = 0;
	//tk_horizontal = 0;
	//tk_vertical = 0;
	//tk_quant = 0;
	tk_itemized = tk_frame + tk_progressive
				//+ tk_spatial + tk_temporal
				+ tk_finish + tk_quant + tk_encoding + tk_convert;
#else
	// Compute the total amount of time that was itemized
	tk_itemized = tk_spatial + tk_temporal + tk_horizontal + tk_vertical + tk_frame
				+ tk_inverse + tk_progressive + tk_recursive
				+ tk_quant + tk_dequant + tk_encoding + tk_decoding + tk_convert;
				//+ tk_lowpass + tk_putbits;
#endif

	// The uncounted time is the total less the itemized time
	tk_uncounted = tk_total - tk_itemized;

	// Compute the number of frames processed per second
	total_fps = (float)frame_count / SEC(tk_total);

	// Compute ratio of secondary searches to codebook lookups
	if (decode_lookup_count > 0)
		decode_search_ratio = (float)decode_search_count / (decode_lookup_count + decode_search_count);

#ifdef _WIN32
	// Can the results be copied to the clipboard?
	if (hwnd != NULL)
	{
		// Create text string for pasting timings into Excel
		char buffer[1024];
		char *bufptr = buffer;
		HANDLE hGlobalMemory;
		void *pGlobalMemory;
		int length;

		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Spatial\t%.6f\n", MS(tk_spatial) + MS(tk_spatial2));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Temporal\t%.6f\n", MS(tk_temporal));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Horizontal\t%.6f\n", MS(tk_horizontal));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Vertical\t%.6f\n", MS(tk_vertical));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Frame\t%.6f\n", MS(tk_frame));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Inverse\t%.6f\n", MS(tk_inverse));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Quant\t%.6f\n", MS(tk_quant));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Dequant\t%.6f\n", MS(tk_dequant));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Encoding\t%.6f\n", MS(tk_encoding));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Decoding\t%.6f\n", MS(tk_decoding));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Conversion\t%.6f\n", MS(tk_convert));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Uncounted\t%.6f\n", MS(tk_uncounted));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Total\t%.6f\n", MS(tk_total));
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "\n");
		bufptr += sprintf_s(bufptr, sizeof(bufptr), "Frames\t%d\n", frame_count);

		// Place the timing information on the clipboard for pasting into Excel
		length = (int)strlen(buffer);
		hGlobalMemory = GlobalAlloc(GMEM_FLAGS, length + 1);
		pGlobalMemory = GlobalLock(hGlobalMemory);
		strncpy_s(pGlobalMemory, length, buffer, length);
		GlobalUnlock(hGlobalMemory);

		if (OpenClipboard(hwnd)) {
			EmptyClipboard();
			SetClipboardData(CF_TEXT, hGlobalMemory);
			CloseClipboard();
		}
	}
#endif

	if (results != NULL)
	{
		// Write the results to a file that can be imported into Excel
		//FILE *csvfile = fopen(results, "w");
		FILE *csvfile;
		int err = 0;

#ifdef _WIN32
		err = fopen_s(&csvfile, results, "w");
#else
		csvfile = fopen(results, "w");
#endif

		if (err == 0 && csvfile != NULL)
		{
			fprintf(csvfile, "Spatial, %.6f\n", MS(tk_spatial) + MS(tk_spatial2));
			fprintf(csvfile, "Temporal, %.6f\n", MS(tk_temporal));
			fprintf(csvfile, "Horizontal, %.6f\n", MS(tk_horizontal));
			fprintf(csvfile, "Vertical, %.6f\n", MS(tk_vertical));
			fprintf(csvfile, "Frame, %.6f\n", MS(tk_frame));
			fprintf(csvfile, "Inverse, %.6f\n", MS(tk_inverse));
			fprintf(csvfile, "Quant, %.6f\n", MS(tk_quant));
			fprintf(csvfile, "Dequant, %.6f\n", MS(tk_dequant));
			fprintf(csvfile, "Encoding, %.6f\n", MS(tk_encoding));
			fprintf(csvfile, "Decoding, %.6f\n", MS(tk_decoding));
			fprintf(csvfile, "Conversion, %.6f\n", MS(tk_convert));
			fprintf(csvfile, "Fastruns, %.6f\n", MS(tk_fastruns));
			//fprintf(csvfile, "Lowpass, %.6f\n", MS(tk_lowpass));
			fprintf(csvfile, "Progressive, %.6f\n", MS(tk_progressive));
			fprintf(csvfile, "Finish, %.6f\n", MS(tk_finish));
			fprintf(csvfile, "Uncounted, %.6f\n", MS(tk_uncounted));
			fprintf(csvfile, "Total, %.6f\n", MS(tk_total));
			fprintf(csvfile, "\n");
			fprintf(csvfile, "Frames, %d\n", frame_count);
			fprintf(csvfile, "Decoded bytes, %d\n", decode_byte_count);
			fprintf(csvfile, "Sample bytes, %d\n", sample_byte_count);
			fclose(csvfile);
		}
	}

	// Output statistics to the log file if it has been opened
	if (logfile != NULL)
	{
		// Compute the number of decoded bytes per millisecond
		float bytes_per_ms = ((tk_decoding > 0) ? ((float)decode_byte_count/MS(tk_decoding)) : 0);

		// Print the timer values to the log file
		fprintf(logfile, "\nPerformance Timers\n\n");
		fprintf(logfile, "First:      %12.3f ms\n", MS(tk_progressive));
		fprintf(logfile, "Spatial:    %12.3f ms\n", MS(tk_spatial));
		fprintf(logfile, "Spatial1:   %12.3f ms\n", MS(tk_spatial1));
		fprintf(logfile, "Spatial2:   %12.3f ms\n", MS(tk_spatial2));
		fprintf(logfile, "Temporal:   %12.3f ms\n", MS(tk_temporal));
		fprintf(logfile, "Horizontal: %12.3f ms\n", MS(tk_horizontal));
		fprintf(logfile, "Vertical:   %12.3f ms\n", MS(tk_vertical));
		fprintf(logfile, "Frame:      %12.3f ms\n", MS(tk_frame));
		fprintf(logfile, "Recursive:  %12.3f ms\n", MS(tk_recursive));
		fprintf(logfile, "Inverse:    %12.3f ms\n", MS(tk_inverse));
		fprintf(logfile, "Quant:      %12.3f ms\n", MS(tk_quant));
		fprintf(logfile, "Dequant:    %12.3f ms\n", MS(tk_dequant));
		fprintf(logfile, "Encoding:   %12.3f ms\n", MS(tk_encoding));
		fprintf(logfile, "Decoding:   %12.3f ms (%.0f bytes/ms)\n", MS(tk_decoding), bytes_per_ms);
		fprintf(logfile, "Conversion: %12.3f ms\n", MS(tk_convert));
		//fprintf(logfile, "Putbits:    %12.3f ms\n", MS(tk_putbits));
		//fprintf(logfile, "Lowpass:    %12.3f ms\n", MS(tk_lowpass));
		fprintf(logfile, "Finish:     %12.3f ms\n", MS(tk_finish));
		fprintf(logfile, "Uncounted:  %12.3f ms\n", MS(tk_uncounted));
		fprintf(logfile, "Total:      %12.3f ms (%.2f fps)\n", MS(tk_total), total_fps);
		fprintf(logfile, "\n");
		fprintf(logfile, "Frame count: %7d\n", frame_count);
		fprintf(logfile, "\n");
		fprintf(logfile, "Decode bytes: %8d\n", decode_byte_count);
		fprintf(logfile, "\n");
		fprintf(logfile, "Sample bytes: %8d\n", sample_byte_count);
		fprintf(logfile, "\n");

#ifdef _WIN32
		// Print the timer resolution
		fprintf(logfile, "Resolution: %12.3f microseconds\n", 1.0e6 / (float)frequency);
#else
		// Print the resolution of the absolute timer
		fprintf(logfile, "Resolution: %12.3f microseconds\n", AbsoluteTimerResolution() / 1000.0);
#endif

		// Print the counters that measure decoding performance
		if (decode_lookup_count > 0) {
			fprintf(logfile, "\n");
			fprintf(logfile, "Lookup count: %d, search count: %d (%.2f percent)\n",
					decode_lookup_count, decode_search_count, (100.0 * decode_search_ratio));
		}
		fprintf(logfile, "\n");

		// Print the counters used to track memory allocations
		fprintf(logfile, "Group allocations:     %d\n", alloc_group_count);
		fprintf(logfile, "Transform allocations: %d\n", alloc_transform_count);
		fprintf(logfile, "Wavelet allocations:   %d\n", alloc_wavelet_count);
		fprintf(logfile, "Frame allocations:     %d\n", alloc_frame_count);
		fprintf(logfile, "Buffer allocations:    %d\n", alloc_buffer_count);
		fprintf(logfile, "Spatial transforms:    %d\n", spatial_transform_count);
		fprintf(logfile, "Temporal transforms:   %d\n", temporal_transform_count);
		fprintf(logfile, "Spatial decoding:      %d\n", spatial_decoding_count);
		fprintf(logfile, "Temporal decoding:     %d\n", temporal_decoding_count);
		fprintf(logfile, "Progressive encoding:  %d\n", progressive_encode_count);
		fprintf(logfile, "Progressive decoding:  %d\n", progressive_decode_count);
		fprintf(logfile, "PutVlcByte count:      %d\n", putvlcbyte_count);
		fprintf(logfile, "PutZeroRun count:      %d\n", putzerorun_count);

		// Print a blank line after the last counter
		fprintf(logfile, "\n");
	}
}

void DoThreadTiming(int startend)
{
#if !DEBUG
	static float start=0.0,end=0.0;
	static int lastthread=0;
	static int beforelastthread=0;
	static float lasttime=0.0;
	static float beforelasttime=0.0;
	DWORD thread = GetCurrentThreadId();
	LARGE_INTEGER current_time;
	FILE *fp;
	static TIMER starttime = 0;
	TIMER currtime;
	float ftime,fdiff=0.0;

	QueryPerformanceCounter(&current_time);

	currtime = current_time.QuadPart;

	if(starttime == 0)
		starttime = currtime;

	currtime -= starttime;
	ftime = MS(currtime);

	if(thread == beforelastthread)
		fdiff = ftime - beforelasttime;

	if(thread == lastthread)
		fdiff = ftime - lasttime;

	fp = fopen("c:/thread.txt", "a");
	if(startend == 0)
		fprintf(fp,"thread ID = %d, start time = %.3fms\n", thread, ftime);
	else if(startend == 1)
		fprintf(fp,"            %d, end time = %.3fms, diff = %.3fms\n", thread, ftime, fdiff);
	else if(startend == 2)
		start = ftime;
	else if(startend == 3)
		fprintf(fp,"  = %.3fms\n", ftime-start);

	fclose(fp);		

	if(startend == 0)
	{
		beforelasttime = lasttime;
		lasttime = ftime;

		beforelastthread = lastthread;
		lastthread = thread;
	}
#endif
}

#endif
