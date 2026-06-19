/*! @file draw.c

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

#if _GRAPHICS

#include "timing.h"

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif
#define TIMING (1 && _TIMING)
#define XMMOPT (1 && _XMMOPT)

#include <stdlib.h>
#include <string.h>		// Use memcpy to copy runs of image pixels
#include <memory.h>
#include <assert.h>
#include <limits.h>
#include <emmintrin.h>
#include <math.h>
#if __APPLE__
#include <dlfcn.h>
#endif

#include "image.h"
#include "codec.h"
#include "debug.h"
#include "color.h"
#include "allocator.h"

#if DANCAIRO
#include "cairo-lib.h"

#include "metadata.h"
#include "swap.h"
#include "draw.h"
#include "convert.h"
#include "exception.h"


//TODO: Must enable exactly one graphics implementation

// Use the Cairo library for overlay text and graphics by default
#ifndef _CAIRO
#define _CAIRO	1
#endif

void HistogramCairoRender(DECODER *decoder, uint8_t *output, int pitch, int x1, int y1, int x2, int y2, int alpha);
void WaveformCairoRender(DECODER *decoder, uint8_t *output, int pitch, int x1, int y1, int x2, int y2, int alpha);
void VectorscopeCairoRender(DECODER *decoder, uint8_t *output, int pitch, int x1, int y1, int x2, int y2, int alpha);
void GridCairoRender(DECODER *decoder, uint8_t *output, int pitch, int x1, int y1, int x2, int y2, int style, float size);

typedef struct rect 
{
	int x1;
	int y1;
	int x2;
	int y2;
	int parallax;
	char lastString[256];
	MDParams lastParams;
} CRECT;


#if _CAIRO

// Use the Cairo library to implement overlay text and graphics

typedef int (*lpCairoLib)(CAIROlib *lib);

typedef struct CairoStuff
{
	CAIROlib cairo;
	uint8_t *cairoless_buffer;
	cairo_surface_t *surface;
	cairo_surface_t *surface2X;
	cairo_t *cr;
	cairo_t *cr2X;
	int surface_w;
	int surface_h;
	int decoder_w;
	int decoder_h;
	int rects; 
	CRECT rectarray[64];
} CAIROSTUFF;

#define OVERSAMPLE	2  // 3 doesn't seem to work
#define VERTREDUCE	10

int DrawOpen(DECODER *decoder)
{
	void *pLib = NULL;
	char filePath[_MAX_PATH] = {'\0'};
	CAIROSTUFF *CS = NULL;
#ifdef _WIN32
	lpCairoLib	cairoLib = NULL;
#endif
	CAIROlib *cairo = NULL;
	cairo_t *cr = NULL;
	cairo_t *cr2X = NULL;
	char szName[260];
	int targetWidth, targetHeight, ret = 0;

	if(decoder->cairoHandle == NULL)
	{
	#if _ALLOCATOR
		decoder->cairoHandle = (void *)Alloc(decoder->allocator, sizeof(CAIROSTUFF));
	#else
		decoder->cairoHandle = (void *)MEMORY_ALLOC(sizeof(CAIROSTUFF));
	#endif
		if(decoder->cairoHandle)
		{
			CAIROSTUFF *CS = (CAIROSTUFF *)decoder->cairoHandle;
			memset(CS, 0, sizeof(CAIROSTUFF));
		}
		else
		{
			return 0;
		}
	}
	
	CS = (CAIROSTUFF *)decoder->cairoHandle;
	
	if(CS == NULL)
		return 0;

	CS->decoder_w = decoder->frame.width;
	CS->decoder_h = decoder->frame.height;
	if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL ||
	   decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER)
	{
		CS->decoder_w *= 2;
	}
	
	targetWidth = CS->decoder_w;
	targetHeight = CS->decoder_h;
	if(decoder->channel_mix_half_res)
	{
		targetWidth *= 2;
		targetHeight *= 2;
	}

	if(!decoder->cairo_loaded)
	{
		#ifdef _WIN32
		GetEnvironmentVariable("PROGRAMFILES",szName,255);

		strcat(filePath, szName);
		  #ifdef _WIN64
			strcat(filePath, " (x86)\\CineForm\\Tools\\libcairo14-64.dll");
		  #else
			strcat(filePath, "\\CineForm\\Tools\\libcairo14.dll");
		  #endif
		#endif

#if __APPLE__
		pLib = dlopen("/usr/x11/lib/libCairo.dylib", RTLD_NOW);
#endif
#ifdef _WIN32
		pLib = (void*)LoadLibrary(filePath);
		if(pLib == NULL) 
		{
			//try locally
		#ifdef _WIN64
			pLib = (void*)LoadLibrary("libcairo14-64.dll");
		#else
			pLib = (void*)LoadLibrary("libcairo14.dll");
		#endif
		}
		if(pLib)
		{
			cairoLib = (lpCairoLib)GetProcAddress((HMODULE)pLib, "_cairo_lib");
			if(cairoLib(&CS->cairo))
			{
				decoder->cairo_loaded = 1;
			}
		}
#endif
#if __APPLE__
		if(pLib)
		{
			// cairoLib = (lpCairoLib)dlsym(pLib, "_cairo_lib");
			if(cairoLib(&CS->cairo,pLib))
			{
				decoder->cairo_loaded = 1;
			}
		}
#endif
	}
	
	if((decoder->cairo_loaded || CS->cairoless_buffer) &&
		CS->surface_w == targetWidth &&
		CS->surface_h == targetHeight) 
		return 1;

	cairo = &CS->cairo;

	if(CS->surface_w || CS->surface_h)
	{
		if(CS->cr && decoder->cairo_loaded)
			cairo->destroy(CS->cr);
		if(CS->cr2X && decoder->cairo_loaded)
			cairo->destroy(CS->cr2X);

		CS->cr = NULL;
		CS->cr2X = NULL;
		
		if(CS->cairoless_buffer)
		{
	#if _ALLOCATOR
			FreeAligned(decoder->allocator, CS->cairoless_buffer);
	#else
			MEMORY_ALIGNED_FREE(CS->cairoless_buffer);
	#endif
		}
		CS->cairoless_buffer = NULL;
	}
	
	CS->surface_w = decoder->frame.width;
	CS->surface_h = decoder->frame.height;
	if(decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL ||
	   decoder->frame.resolution == DECODED_RESOLUTION_HALF_HORIZONTAL_DEBAYER)
		CS->surface_w *= 2; //HACK


	if(decoder->cairo_loaded)
	{
		CS->surface = cairo->image_surface_create(CAIRO_FORMAT_ARGB32, CS->surface_w, CS->surface_h);
	}
	else
	{
		size_t size = CS->surface_w * CS->surface_h * 4;
#if _ALLOCATOR
		CS->cairoless_buffer = (uint8_t *)AllocAligned(decoder->allocator, size, 16);
#else
		CS->cairoless_buffer = (uint8_t *)MEMORY_ALIGNED_ALLOC(size, 16);
#endif
	}

	if(CS->surface && decoder->cairo_loaded)
	{
		if((CS->cr = cairo->create(CS->surface)))
		{
			cr = CS->cr;

			cairo->set_source_rgba (cr, 0, 0, 0, 0); // transparent
			cairo->set_operator (cr, CAIRO_OPERATOR_SOURCE); // paint the alpha channel, not just mix.
			cairo->paint (cr);
			cairo->set_operator (cr, CAIRO_OPERATOR_OVER);
			
			cairo->scale (cr, CS->surface_h, CS->surface_h);
			cairo->select_font_face (cr, decoder->MDPcurrent.font,
			  CAIRO_FONT_SLANT_NORMAL,
			  CAIRO_FONT_WEIGHT_NORMAL);

			if(CAIRO_STATUS_SUCCESS != cairo->status(cr))
			{
				cairo->select_font_face (cr, "Courier New Bold",
				  CAIRO_FONT_SLANT_NORMAL,
				  CAIRO_FONT_WEIGHT_NORMAL);
				  
				  if(CAIRO_STATUS_SUCCESS == cairo->status(cr))
				  {
				  	strcpy(decoder->MDPcurrent.font, "Courier New Bold");
				  }
			}
			
			cairo->set_font_size (cr, decoder->MDPcurrent.fontsize);
		}
	

		CS->surface2X = cairo->image_surface_create(CAIRO_FORMAT_ARGB32, CS->surface_w*OVERSAMPLE, CS->surface_h/VERTREDUCE*OVERSAMPLE);
		if(CS->surface2X)
		{
			if((CS->cr2X = cairo->create(CS->surface2X)))
			{
				cairo_font_options_t *font_options;
				cairo_pattern_t *pat;
				cr2X = CS->cr2X;

				cairo->set_source_rgba (cr2X, 0, 0, 0, 0); // transparent
				cairo->set_operator (cr2X, CAIRO_OPERATOR_SOURCE); // paint the alpha channel, not just mix.
				cairo->paint (cr2X);

				/*
				pat = cairo->pattern_create_linear (0.0, 0.0,  0.0, surface_h/VERTREDUCE*OVERSAMPLE);
				cairo->pattern_add_color_stop_rgba (pat, 1, 0, 0, 0, 1);
				cairo->pattern_add_color_stop_rgba (pat, 0, 1, 1, 1, 1);
				cairo->rectangle (cr2X, 0, 0, surface_w*OVERSAMPLE, surface_h/VERTREDUCE*OVERSAMPLE);
				cairo->set_source (cr2X, pat);
				cairo->fill (cr2X);
				cairo->pattern_destroy (pat);
				*/

				cairo->set_operator (cr2X, CAIRO_OPERATOR_OVER);
				
				cairo->scale (cr2X, CS->surface_h*OVERSAMPLE, CS->surface_h*OVERSAMPLE);
				cairo->select_font_face (cr, decoder->MDPcurrent.font,
				  CAIRO_FONT_SLANT_NORMAL,
				  CAIRO_FONT_WEIGHT_NORMAL);

				cairo->set_font_size (cr, decoder->MDPcurrent.fontsize);

				font_options = cairo->font_options_create ();
				cairo->get_font_options (cr2X, font_options);
				cairo->font_options_set_antialias (font_options, CAIRO_ANTIALIAS_GRAY);
				cairo->set_font_options (cr2X, font_options);
				cairo->font_options_destroy (font_options);					
			}
		}
	}

	ret = (decoder->cairo_loaded || CS->cairoless_buffer) ? 1 : 0;
	return ret;	
}


void DrawClose(DECODER *decoder)
{
	void *pLib = NULL;
	CAIROSTUFF *CS = NULL;
	lpCairoLib	cairoLib;
	CAIROlib *cairo;
	cairo_t *cr;
	cairo_t *cr2X;

	if(decoder->cairoHandle)
	{
		CAIROSTUFF *CS = (CAIROSTUFF *)decoder->cairoHandle;
		cairo = &CS->cairo;

		if(decoder->cairo_loaded)
		{
			if(CS->surface_w || CS->surface_h)
			{

				cairo->surface_destroy(CS->surface);
				cairo->surface_destroy(CS->surface2X);
				cairo->destroy(CS->cr);
				cairo->destroy(CS->cr2X);

				CS->cr = NULL;
				CS->cr2X = NULL;
				CS->surface = NULL;
				CS->surface2X = NULL;

				CS->surface_w = 0;
				CS->surface_h = 0;
			}

			if(decoder->vs_surface)
			{
				cairo->surface_destroy((cairo_surface_t *)decoder->vs_surface);
				cairo->destroy((cairo_t *)decoder->vs_cr);

				decoder->vs_surface = NULL;
				decoder->vs_cr = NULL;
			}
		}
		else if(CS->cairoless_buffer)
		{
		#if _ALLOCATOR
			FreeAligned(decoder->allocator, CS->cairoless_buffer);
		#else
			MEMORY_ALIGNED_FREE(CS->cairoless_buffer);
		#endif
		}

	#if _ALLOCATOR
		Free(decoder->allocator, decoder->cairoHandle);
	#else
		MEMORY_FREE(decoder->cairoHandle);
	#endif
		decoder->cairoHandle = NULL;
	}

}


void DrawInit(DECODER *decoder)
{
	CAIROSTUFF *CS = NULL;
	int j;
	
	for(j=0; j<16; j++)
		decoder->last_xypos[j & 0xf][0] = decoder->last_xypos[j & 0xf][1] = -1;

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(CS)
		CS->rects = 0;
}



THREAD_PROC(DrawThreadProc, lpParam)
{
	DECODER *decoder = (DECODER *)lpParam;
	FILE *logfile = decoder->logfile;
	THREAD_ERROR error = THREAD_ERROR_OKAY;
	int thread_index;

	// Set the handler for system exceptions
#ifdef _WIN32
	SetDefaultExceptionHandler();
#endif

	// Determine the index of this worker thread
	error = PoolThreadGetIndex(&decoder->draw_thread.pool, &thread_index);
	assert(error == THREAD_ERROR_OKAY);

	// Check that the thread index is consistent with the size of the thread pool
	assert(0 <= thread_index && thread_index < decoder->draw_thread.pool.thread_count);

#if (1 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Thread data: 0x%p, index: %d\n", (int)lpParam, thread_index);
	}
#endif

	// The worker thread stays active while waiting for a message to start processing
	for (;;)
	{
		// Wait for the signal to begin processing a transform
		THREAD_MESSAGE message = THREAD_MESSAGE_NONE;
		error = PoolThreadWaitForMessage(&decoder->draw_thread.pool, thread_index, &message);

#if (1 && DEBUG)
		if (logfile) {
			fprintf(logfile, "Thread index: %d, received message: %d\n", thread_index, message);
		}
#endif
		// Received a signal to begin inverse transform processing?
		if (error == THREAD_ERROR_OKAY && message == THREAD_MESSAGE_START)
		{
		
			// Lock access to the transform data
			//Lock(&decoder->worker_thread.lock);
			DrawInit(decoder);
	
			if(decoder->cfhddata.BurninFlags & 3) //overlays / tool
				DrawMetadataObjects(decoder);		

			// Unlock access to the transform data
			//Unlock(&decoder->worker_thread.lock);

			// Signal that this thread is done
			PoolThreadSignalDone(&decoder->draw_thread.pool, thread_index);

			// Loop and wait for the next message
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

#if (1 && DEBUG)
	if (logfile) {
		fprintf(logfile, "Thread index: %d, returning error code: %d\n", thread_index, error);
	}
#endif

	return (THREAD_RETURN_TYPE)error;
}



//Convert any decompressed planar ROWs of PIXEL16U into most output formats.
void DrawStartThreaded(DECODER *decoder)
{
#if _THREADED
	if(DrawOpen(decoder))
	{
		if(decoder->draw_thread.pool.thread_count == 0)
		{
			CreateLock(&decoder->draw_thread.lock);
			// Initialize the pool of transform worker threads
			ThreadPoolCreate(&decoder->draw_thread.pool,
							1, // on thread
							DrawThreadProc,
							decoder);
		}

		// Post a message to the mailbox
		// Set the work count to the number of rows to process
		ThreadPoolSetWorkCount(&decoder->draw_thread.pool, 1);

		// Start the transform worker threads
		ThreadPoolSendMessage(&decoder->draw_thread.pool, THREAD_MESSAGE_START);
	}
#endif
}



//Convert any decompressed planar ROWs of PIXEL16U into most output formats.
void DrawWaitThreaded(DECODER *decoder)
{
#if _THREADED
	if(decoder->draw_thread.pool.thread_count > 0)
	{			
		// Wait for all of the worker threads to finish
		ThreadPoolWaitAllDone(&decoder->draw_thread.pool);
	}
#endif
}

void DrawPNG(DECODER *decoder, char *path, float scaleX, float scaleY, int parallax, int *ret_width, int *ret_height, char *ret_path) 
{
	double posx,posy,fsize;
	cairo_text_extents_t extents;
	CAIROSTUFF *CS = NULL;
	lpCairoLib	cairoLib;
	CAIROlib *cairo;
	cairo_t *cr;
	int count,i;

	
	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS)
		return;

	if(!decoder->cairo_loaded)
		return;

	cairo = &CS->cairo;
	cr = CS->cr;

	posx = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][0];
	posy = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][1];

	if(posx == -1.0 || posy == -1.0)
	{
		posx = 0.5 * (float)CS->surface_w / (float)CS->surface_h;
		posy = 0.5;
		if(decoder->MDPcurrent.justication & JUSTIFY_LEFT)
			posx = decoder->OverlaySafe[0] * (float)CS->surface_w / (float)CS->surface_h;
		if(decoder->MDPcurrent.justication & JUSTIFY_RIGHT)
			posx = (1.0 - decoder->OverlaySafe[0]) * (float)CS->surface_w / (float)CS->surface_h;
		if(decoder->MDPcurrent.justication & JUSTIFY_TOP)
			posy = decoder->OverlaySafe[1];
		if(decoder->MDPcurrent.justication & JUSTIFY_BOTTOM)
			posy = (1.0 - decoder->OverlaySafe[1]);
	}
	decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][0] = posx;
	decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][1] = posy;


	{
		float neww = scaleX, newh = scaleY;		



		if(CS->rects < 64)
		{
			int x1,x2,y1,y2;
			double fx1,fx2,fy1,fy2;
			cairo_surface_t *image = NULL;
			int w=0,h=0;
			
			if(*ret_width == 0 || *ret_height == 0 || 0 != strcmp(ret_path, path) )
			{
				image = cairo->image_surface_create_from_png (path);
				w = cairo->image_surface_get_width (image);
				h = cairo->image_surface_get_height (image);

				*ret_width = w;
				*ret_height = h;
				strcpy(ret_path, path);
			}

			w = *ret_width;
			h = *ret_height;

			
			neww = scaleX * ((float)w / (float)CS->decoder_w);
			newh = scaleY * ((float)h / (float)CS->decoder_h);

		/*	if(decoder->channel_mix_half_res)
			{
				neww *= 2.0;
				newh *= 2.0;
			}*/
			switch(decoder->frame.resolution)
			{
			default:
			case DECODED_RESOLUTION_FULL:
				break;
			case DECODED_RESOLUTION_HALF:
				neww *= 0.5;
				newh *= 0.5;
				break;
			case DECODED_RESOLUTION_QUARTER:
				neww *= 0.25;
				newh *= 0.25;
				break;
			}

				

			if(decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][0] == posx && decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][1] == posy)
			{
				// unmoved: auto line increment.
				if(decoder->MDPcurrent.justication & JUSTIFY_TOP)
				{
					posy = decoder->last_container_y2[decoder->MDPcurrent.justication & 0xf];
				}
				else if(decoder->MDPcurrent.justication & JUSTIFY_BOTTOM)
				{
					posy = decoder->last_container_y1[decoder->MDPcurrent.justication & 0xf];
				}
				else // centre
				{
					posy = decoder->last_container_y2[decoder->MDPcurrent.justication & 0xf] + newh*0.5;
				}
			}
			decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][0] = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication & 0xf][0];
			decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][1] = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication & 0xf][1];

			if(decoder->MDPcurrent.justication & JUSTIFY_LEFT)
			{
				fx1 = (posx);
				fx2 = fx1 + neww;
			}
			else if(decoder->MDPcurrent.justication & JUSTIFY_RIGHT)
			{
				fx1 = posx - neww;
				fx2 = (posx);
			}
			else
			{
				fx1 = (posx-neww*0.5);
				fx2 = fx1 + neww;
			}

			if(decoder->MDPcurrent.justication & JUSTIFY_TOP)
			{
				fy1 = (posy);
				fy2 = fy1 + newh;
			}			
			else if(decoder->MDPcurrent.justication & JUSTIFY_BOTTOM)
			{
				fy1 = posy - newh;
				fy2 = (posy);
			}
			else
			{
				fy1 = posy - newh*0.5;
				fy2 = fy1 + newh;
			}


			decoder->last_container_y1[decoder->MDPcurrent.justication & 0xf] = fy1;
			decoder->last_container_y2[decoder->MDPcurrent.justication & 0xf] = fy2;

			x1 = ((int)(fx1*CS->surface_h));// & ~1;
			y1 = fy1*CS->surface_h;
			x2 = (1+(int)(fx2*CS->surface_h));// & ~1;
			y2 = fy2*CS->surface_h;

			if(decoder->channel_mix_half_res)
				parallax *= 2;
			switch(decoder->frame.resolution)
			{
			default:
			case DECODED_RESOLUTION_FULL:
				break;
			case DECODED_RESOLUTION_HALF:
				parallax >>= 1;
				break;
			case DECODED_RESOLUTION_QUARTER:
				parallax >>= 2;
				break;
			}

			CS->rectarray[CS->rects].parallax = parallax;

			if( 0 == strcmp(&CS->rectarray[CS->rects].lastString[0], path) &&
					CS->rectarray[CS->rects].x1 == x1 &&
					CS->rectarray[CS->rects].y1 == y1 &&
					CS->rectarray[CS->rects].x2 == x2 &&
					CS->rectarray[CS->rects].y2 == y2)
			{
				// do nothing
				CS->rectarray[CS->rects].lastParams.display_opacity = decoder->MDPcurrent.display_opacity * decoder->MDPcurrent.fcolor[3]; //fill color's alpha

				CS->rects++;
			}
			else
			{
				cairo_matrix_t matrix;
				cairo_matrix_t nmatrix;
				float newscaleX;
				float newscaleY;

				memcpy(&CS->rectarray[CS->rects].lastParams, &decoder->MDPcurrent, sizeof(MDParams));
				CS->rectarray[CS->rects].lastParams.display_opacity = decoder->MDPcurrent.display_opacity * decoder->MDPcurrent.fcolor[3]; //fill color's alpha

				if(image == NULL)
				{
					image = cairo->image_surface_create_from_png (path);
					w = cairo->image_surface_get_width (image);
					h = cairo->image_surface_get_height (image);

					*ret_width = w;
					*ret_height = h;
					strcpy(ret_path, path);
				}

				if(w && h)
				{		
					//cairo->surface_set_filter (image, CAIRO_FILTER_BEST); // Laster version of cairo needed
					newscaleX = ((float)w / (float)CS->surface_h) / neww;
					newscaleY = ((float)h / (float)CS->surface_h) / newh;

					strcpy(CS->rectarray[CS->rects].lastString, path);
					CS->rectarray[CS->rects].x1 = x1;
					CS->rectarray[CS->rects].x2 = x2;
					CS->rectarray[CS->rects].y1 = y1;
					CS->rectarray[CS->rects].y2 = y2;
		
					
					cairo->get_matrix(cr, &matrix);
					cairo->get_matrix(cr, &nmatrix);
					nmatrix.xx = 1.0/newscaleX;
					nmatrix.yy = 1.0/newscaleY;
					cairo->set_matrix(cr, &nmatrix);
					
					cairo->set_operator (cr, CAIRO_OPERATOR_SOURCE);
					cairo->set_source_surface (cr, image, CS->rectarray[CS->rects].x1*newscaleX, CS->rectarray[CS->rects].y1*newscaleY);
					cairo->rectangle (cr, CS->rectarray[CS->rects].x1*newscaleX, CS->rectarray[CS->rects].y1*newscaleY, 
						(CS->rectarray[CS->rects].x2-CS->rectarray[CS->rects].x1)*newscaleX, (CS->rectarray[CS->rects].y2-CS->rectarray[CS->rects].y1)*newscaleY);
					cairo->fill (cr);

					cairo->set_matrix(cr, &matrix);
					CS->rects++;
				}
			}
			
			if(image)
				cairo->surface_destroy (image);
		}
	}
}




void DrawPrepareTool(DECODER *decoder, char *tool, char *subtype, float scaleX, float scaleY, int parallax) 
{
	double posx,posy,fsize;
	cairo_text_extents_t extents;
	CAIROSTUFF *CS = NULL;
	lpCairoLib	cairoLib;
	CAIROlib *cairo;
	cairo_t *cr;
	int count,i;
	
	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS)
		return;

	if(!decoder->cairo_loaded)
		return;

	cairo = &CS->cairo;
	cr = CS->cr;

	posx = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][0];
	posy = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][1];

	if(posx == -1.0 || posy == -1.0)
	{
		posx = 0.5 * (float)CS->surface_w / (float)CS->surface_h;
		posy = 0.5;
		if(decoder->MDPcurrent.justication & JUSTIFY_LEFT)
			posx = decoder->OverlaySafe[0] * (float)CS->surface_w / (float)CS->surface_h;
		if(decoder->MDPcurrent.justication & JUSTIFY_RIGHT)
			posx = (1.0 - decoder->OverlaySafe[0]) * (float)CS->surface_w / (float)CS->surface_h;
		if(decoder->MDPcurrent.justication & JUSTIFY_TOP)
			posy = decoder->OverlaySafe[1];
		if(decoder->MDPcurrent.justication & JUSTIFY_BOTTOM)
			posy = (1.0 - decoder->OverlaySafe[1]);
	}
	decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][0] = posx;
	decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][1] = posy;


	{
		float neww, newh;		

		if(CS->rects < 64)
		{
			int x1,x2,y1,y2;
			double fx1,fx2,fy1,fy2;
			cairo_surface_t *image = NULL;
			float w=0,h=0;
			bool fullscreen = false;
			
			if(0 == strcmp(tool, "Tool:Histogram"))
			{
				w = 0.6*CS->decoder_w; h = 0.3*CS->decoder_h;
			}
			if(0 == strcmp(tool, "Tool:Waveform"))
			{
				w = 0.6*CS->decoder_w; h = 0.3*CS->decoder_h;
			}
			if(0 == strcmp(tool, "Tool:Vectorscope"))
			{
				w = 0.6*CS->decoder_h; h = 0.6*CS->decoder_h;
			}
			if(0 == strcmp(tool, "Tool:Vectorscope2"))
			{
				w = 0.6*CS->decoder_h; h = 0.6*CS->decoder_h;
			}
			neww = scaleX * ((float)w / (float)CS->decoder_w);
			newh = scaleY * ((float)h / (float)CS->decoder_h);

			if(0 == strncmp(tool, "Tool:Grid", 9))
			{
				w = CS->decoder_w; h = CS->decoder_h;
				neww = 1.0 * (float)CS->decoder_w/ (float)CS->decoder_h;
				newh = 1.0;
				fullscreen = true;
				decoder->MDPcurrent.justication = 0;
				posx = 0.0;
				posy = 1.0;
				fx1 = (posx);
				fx2 = fx1 + neww;
				fy1 = posy - newh;
				fy2 = (posy);
			}

		
			if(!fullscreen)
			{

				if(decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][0] == posx && decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][1] == posy)
				{
					// unmoved: auto line increment.
					if(decoder->MDPcurrent.justication & JUSTIFY_TOP)
					{
						posy = decoder->last_container_y2[decoder->MDPcurrent.justication & 0xf];
					}
					else if(decoder->MDPcurrent.justication & JUSTIFY_BOTTOM)
					{
						posy = decoder->last_container_y1[decoder->MDPcurrent.justication & 0xf];
					}
					else // centre
					{
						posy = decoder->last_container_y2[decoder->MDPcurrent.justication & 0xf] + newh*0.5;
					}
				}
				decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][0] = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication & 0xf][0];
				decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][1] = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication & 0xf][1];

				if(decoder->MDPcurrent.justication & JUSTIFY_LEFT)
				{
					fx1 = (posx);
					fx2 = fx1 + neww;
				}
				else if(decoder->MDPcurrent.justication & JUSTIFY_RIGHT)
				{
					fx1 = posx - neww;
					fx2 = (posx);
				}
				else
				{
					fx1 = (posx-neww*0.5);
					fx2 = fx1 + neww;
				}

				if(decoder->MDPcurrent.justication & JUSTIFY_TOP)
				{
					fy1 = (posy);
					fy2 = fy1 + newh;
				}			
				else if(decoder->MDPcurrent.justication & JUSTIFY_BOTTOM)
				{
					fy1 = posy - newh;
					fy2 = (posy);
				}
				else
				{
					fy1 = posy - newh*0.5;
					fy2 = fy1 + newh;
				}
				decoder->last_container_y1[decoder->MDPcurrent.justication & 0xf] = fy1;
				decoder->last_container_y2[decoder->MDPcurrent.justication & 0xf] = fy2;
			}


			x1 = ((int)(fx1*CS->surface_h));// & ~1;
			y1 = fy1*CS->surface_h;
			x2 = (1+(int)(fx2*CS->surface_h));// & ~1;
			y2 = fy2*CS->surface_h;

			if(decoder->channel_mix_half_res)
				parallax *= 2;
			switch(decoder->frame.resolution)
			{
			default:
			case DECODED_RESOLUTION_FULL:
				break;
			case DECODED_RESOLUTION_HALF:
				parallax >>= 1;
				break;
			case DECODED_RESOLUTION_QUARTER:
				parallax >>= 2;
				break;
			}

			CS->rectarray[CS->rects].x1 = x1;
			CS->rectarray[CS->rects].x2 = x2;
			CS->rectarray[CS->rects].y1 = y1;
			CS->rectarray[CS->rects].y2 = y2;

			CS->rectarray[CS->rects].parallax = parallax;

			strcpy(CS->rectarray[CS->rects].lastString, tool);
			CS->rectarray[CS->rects].lastParams.display_opacity = decoder->MDPcurrent.display_opacity * decoder->MDPcurrent.fcolor[3]; //fill color's alpha
			CS->rectarray[CS->rects].lastParams.fontsize = scaleY;
			CS->rects++;
		}
	}
}

void DrawSubtitlePNG(DECODER *decoder, char *path, int TopLeftX, int TopLeftY, int width, int height, float opacity, int parallax) 
{
	double posx,posy,fsize;
	cairo_text_extents_t extents;
	CAIROSTUFF *CS = NULL;
	lpCairoLib	cairoLib;
	CAIROlib *cairo;
	cairo_t *cr;
	int count,i;

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS)
		return;

	if(!decoder->cairo_loaded)
		return;

	cairo = &CS->cairo;
	cr = CS->cr;

	if(CS->rects < 64)
	{
		int x1,x2,y1,y2;
		double fx1,fx2,fy1,fy2;

		if(decoder->channel_mix_half_res)
			parallax *= 2;
		switch(decoder->frame.resolution)
		{
		default:
		case DECODED_RESOLUTION_FULL:
			break;
		case DECODED_RESOLUTION_HALF:
			TopLeftX >>= 1;
			TopLeftY >>= 1;
			width >>= 1;
			height >>= 1;
			parallax >>= 1;
			break;
		case DECODED_RESOLUTION_QUARTER:
			TopLeftX >>= 2;
			TopLeftY >>= 2;
			width >>= 2;
			height >>= 2;
			parallax >>= 2;
			break;
		}

		x1 = TopLeftX;
		x2 = TopLeftX + width;
		y1 = TopLeftY;
		y2 = TopLeftY + height;

		fx1 = (double)x1 / (double)CS->surface_h;
		fy1 = (double)y1 / (double)CS->surface_h;
		fx2 = (double)x2 / (double)CS->surface_h;
		fy2 = (double)y2 / (double)CS->surface_h;

		CS->rectarray[CS->rects].lastParams.display_opacity = opacity;
		CS->rectarray[CS->rects].parallax = parallax;

		if( 0 == strcmp(&CS->rectarray[CS->rects].lastString[0], path) &&
				CS->rectarray[CS->rects].x1 == x1 &&
				CS->rectarray[CS->rects].y1 == y1 &&
				CS->rectarray[CS->rects].x2 == x2 &&
				CS->rectarray[CS->rects].y2 == y2)
		{
			// do nothing
			CS->rects++;
		}
		else
		{
			cairo_matrix_t matrix;
			cairo_matrix_t nmatrix;
			cairo_surface_t *image;
			int w,h;
			float newscaleX;
			float newscaleY;

			image = cairo->image_surface_create_from_png (path);
			w = cairo->image_surface_get_width (image);
			h = cairo->image_surface_get_height (image);
			if(w && h)
			{
				float neww = (float)width/(float)CS->surface_h, newh = (float)height/(float)CS->surface_h;	

				newscaleX = ((float)w / (float)CS->surface_h) / neww;
				newscaleY = ((float)h / (float)CS->surface_h) / newh;

				strcpy(CS->rectarray[CS->rects].lastString, path);
				CS->rectarray[CS->rects].x1 = x1;
				CS->rectarray[CS->rects].x2 = x2;
				CS->rectarray[CS->rects].y1 = y1;
				CS->rectarray[CS->rects].y2 = y2;
	
				
				cairo->get_matrix(cr, &matrix);
				cairo->get_matrix(cr, &nmatrix);
				nmatrix.xx = 1.0/newscaleX;
				nmatrix.yy = 1.0/newscaleY;
				cairo->set_matrix(cr, &nmatrix);
				
				cairo->set_operator (cr, CAIRO_OPERATOR_SOURCE);
				cairo->set_source_surface (cr, image, CS->rectarray[CS->rects].x1*newscaleX, CS->rectarray[CS->rects].y1*newscaleY);
				cairo->rectangle (cr, CS->rectarray[CS->rects].x1*newscaleX, CS->rectarray[CS->rects].y1*newscaleY, 
					(CS->rectarray[CS->rects].x2-CS->rectarray[CS->rects].x1)*newscaleX, (CS->rectarray[CS->rects].y2-CS->rectarray[CS->rects].y1)*newscaleY);
				cairo->fill (cr);

				cairo->set_matrix(cr, &matrix);
				CS->rects++;
			}
			cairo->surface_destroy (image);
		}
	}
}


void DrawHistogram(DECODER *decoder, int parallax)
{
	double posx,posy,fsize;
	CAIROSTUFF *CS = NULL;
	int count,i;
	char *specifier = NULL;
	int typepos = 0;
	int x1,x2,y1,y2;

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS)
		return;

#if 0
	posx = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0];
	posy = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1];

	if(posx == -1.0 || posy == -1.0)
	{	
		posx = decoder->OverlaySafe[0] * (float)CS->surface_w / (float)CS->surface_h;
		posy = (1.0 - decoder->OverlaySafe[1]);
	}
	decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0] = posx;
	decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1] = posy;


	if(CS->rects < 64)
	{
		double yoffset;
		double fx1,fx2,fy1,fy2;
		double histw = 0.4, histh = 0.15;

		if(CS->rects && decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0] == posx && decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1] == posy)
		{
			// unmoved: auto line increment.
			posy = decoder->last_container_y1[JUSTIFY_BOTTOM|JUSTIFY_LEFT];
		}
		decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0] = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0];
		decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1] = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1];

		fx1 = (posx);
		fx2 = fx1 + histw;
		fy1 = posy - (histh);
		fy2 = (posy);

		decoder->last_container_y1[JUSTIFY_BOTTOM|JUSTIFY_LEFT] = fy1;
		decoder->last_container_y2[JUSTIFY_BOTTOM|JUSTIFY_LEFT] = fy2;

		x1 = ((int)(fx1*CS->surface_h));// & ~1;
		y1 = fy1*CS->surface_h;
		x2 = (1+(int)(fx2*CS->surface_h));// & ~1;
		y2 = fy2*CS->surface_h;

		if(decoder->channel_mix_half_res)
			parallax *= 2;
		switch(decoder->frame.resolution)
		{
		default:
		case DECODED_RESOLUTION_FULL:
			break;
		case DECODED_RESOLUTION_HALF:
			parallax >>= 1;
			break;
		case DECODED_RESOLUTION_QUARTER:
			parallax >>= 2;
			break;
		}
		CS->rectarray[CS->rects].parallax = parallax;

		// always re-render the histogram
		{
			memcpy(&CS->rectarray[CS->rects].lastParams, &decoder->MDPcurrent, sizeof(MDParams));
			CS->rectarray[CS->rects].lastParams.display_opacity = 1.0;


			CS->rectarray[CS->rects].x1 = x1;
			CS->rectarray[CS->rects].x2 = x2;
			CS->rectarray[CS->rects].y1 = y1;
			CS->rectarray[CS->rects].y2 = y2;

			// draw histogram
			if(decoder->cairo_loaded)
			{
				CAIROlib *cairo = &CS->cairo;
				uint8_t *cairo_buffer = (uint8_t *)cairo->image_surface_get_data(CS->surface);
				HistogramCairoRender(decoder, cairo_buffer, CS->surface_w*4, x1, y1, x2, y2, 180);
			}
			else if(CS->cairoless_buffer)
			{	
				HistogramCairoRender(decoder, CS->cairoless_buffer, CS->surface_w*4, x1, y1, x2, y2, 180);
			}
			//cairo->surface_write_to_png(CS->surface, "C:\\Users\\Public\\surface.png");
		}
		CS->rects++;
	}
#endif

	for(i=0; i<CS->rects; i++)
	{
		if(0==strcmp(CS->rectarray[i].lastString, "Tool:Histogram"))
		{			
			x1 = CS->rectarray[i].x1;
			x2 = CS->rectarray[i].x2;
			y1 = CS->rectarray[i].y1;
			y2 = CS->rectarray[i].y2;

			if(x2 != x1 && y2 != y1)
			{
				// draw histogram
				if(decoder->cairo_loaded)
				{
					CAIROlib *cairo = &CS->cairo;
					uint8_t *cairo_buffer = (uint8_t *)cairo->image_surface_get_data(CS->surface);
					HistogramCairoRender(decoder, cairo_buffer, CS->surface_w*4, x1, y1, x2, y2, 255);
				}
				else if(CS->cairoless_buffer)
				{	
					HistogramCairoRender(decoder, CS->cairoless_buffer, CS->surface_w*4, x1, y1, x2, y2, 255);
				}
			}
			break;
		}
	}
}


void DrawWaveform(DECODER *decoder, int parallax)
{
	double posx,posy,fsize;
	CAIROSTUFF *CS = NULL;
	int count,i;
	char *specifier = NULL;
	int x1,x2,y1,y2;
	int typepos = 0;

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS)
		return;

#if 0
	posx = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0];
	posy = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1];

	if(posx == -1.0 || posy == -1.0)
	{	
		posx = decoder->OverlaySafe[0] * (float)CS->surface_w / (float)CS->surface_h;
		posy = (1.0 - decoder->OverlaySafe[1]);
	}
	decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0] = posx;
	decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1] = posy;


	if(CS->rects < 64)
	{
		double yoffset;
		double fx1,fx2,fy1,fy2;
		double histw = 0.5, histh = 0.2;

		if(CS->rects && decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0] == posx && decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1] == posy)
		{
			// unmoved: auto line increment.
			posy = decoder->last_container_y1[JUSTIFY_BOTTOM|JUSTIFY_LEFT];
		}
		decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0] = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0];
		decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1] = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1];

		fx1 = (posx);
		fx2 = fx1 + histw;
		fy1 = posy - (histh);
		fy2 = (posy);

		decoder->last_container_y1[JUSTIFY_BOTTOM|JUSTIFY_LEFT] = fy1;
		decoder->last_container_y2[JUSTIFY_BOTTOM|JUSTIFY_LEFT] = fy2;

		x1 = ((int)(fx1*CS->surface_h));// & ~1;
		y1 = fy1*CS->surface_h;
		x2 = (1+(int)(fx2*CS->surface_h));// & ~1;
		y2 = fy2*CS->surface_h;

		if(decoder->channel_mix_half_res)
			parallax *= 2;
		switch(decoder->frame.resolution)
		{
		default:
		case DECODED_RESOLUTION_FULL:
			break;
		case DECODED_RESOLUTION_HALF:
			parallax >>= 1;
			break;
		case DECODED_RESOLUTION_QUARTER:
			parallax >>= 2;
			break;
		}
		CS->rectarray[CS->rects].parallax = parallax;

		// always re-render the histogram
		{
			memcpy(&CS->rectarray[CS->rects].lastParams, &decoder->MDPcurrent, sizeof(MDParams));
			CS->rectarray[CS->rects].lastParams.display_opacity = 1.0;


			CS->rectarray[CS->rects].x1 = x1;
			CS->rectarray[CS->rects].x2 = x2;
			CS->rectarray[CS->rects].y1 = y1;
			CS->rectarray[CS->rects].y2 = y2;

			// draw histogram
			if(decoder->cairo_loaded)
			{
				CAIROlib *cairo = &CS->cairo;
				uint8_t *cairo_buffer = (uint8_t *)cairo->image_surface_get_data(CS->surface);
				WaveformCairoRender(decoder, cairo_buffer, CS->surface_w*4, x1, y1, x2, y2, 210);
			}
			else if(CS->cairoless_buffer)
			{	
				WaveformCairoRender(decoder, CS->cairoless_buffer, CS->surface_w*4, x1, y1, x2, y2, 210);
			}
			//cairo->surface_write_to_png(CS->surface, "C:\\Users\\Public\\surface.png");
		}
		CS->rects++;
	}
#endif

	for(i=0; i<CS->rects; i++)
	{
		if(0==strcmp(CS->rectarray[i].lastString, "Tool:Waveform"))
		{			
			x1 = CS->rectarray[i].x1;
			x2 = CS->rectarray[i].x2;
			y1 = CS->rectarray[i].y1;
			y2 = CS->rectarray[i].y2;

			if(x2 != x1 && y2 != y1)
			{
				// draw waveform
				if(decoder->cairo_loaded)
				{
					CAIROlib *cairo = &CS->cairo;
					uint8_t *cairo_buffer = (uint8_t *)cairo->image_surface_get_data(CS->surface);
					WaveformCairoRender(decoder, cairo_buffer, CS->surface_w*4, x1, y1, x2, y2, 255);
				}
				else if(CS->cairoless_buffer)
				{	
					WaveformCairoRender(decoder, CS->cairoless_buffer, CS->surface_w*4, x1, y1, x2, y2, 255);
				}
			}

			break;
		}
	}
}


void DrawVectorscope(DECODER *decoder, int parallax)
{
	double posx,posy,fsize;
	CAIROSTUFF *CS = NULL;
	int count,i;
	int x1,x2,y1,y2;
	char *specifier = NULL;
	int typepos = 0;

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS)
		return;

#if 0
	posx = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0];
	posy = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1];

	if(posx == -1.0 || posy == -1.0)
	{	
		posx = decoder->OverlaySafe[0] * (float)CS->surface_w / (float)CS->surface_h;
		posy = (1.0 - decoder->OverlaySafe[1]);
	}
	decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0] = posx;
	decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1] = posy;


	if(CS->rects < 64)
	{
		double yoffset;
		double fx1,fx2,fy1,fy2;
		double histw = 0.26667, histh = 0.26667;

		if(CS->rects && decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0] == posx && decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1] == posy)
		{
			// unmoved: auto line increment.
			posy = decoder->last_container_y1[JUSTIFY_BOTTOM|JUSTIFY_LEFT];
		}
		decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0] = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][0];
		decoder->last_xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1] = decoder->MDPcurrent.xypos[JUSTIFY_BOTTOM|JUSTIFY_LEFT][1];

		fx1 = (posx);
		fx2 = fx1 + histw;
		fy1 = posy - (histh);
		fy2 = (posy);

		decoder->last_container_y1[JUSTIFY_BOTTOM|JUSTIFY_LEFT] = fy1;
		decoder->last_container_y2[JUSTIFY_BOTTOM|JUSTIFY_LEFT] = fy2;

		x1 = ((int)(fx1*CS->surface_h));// & ~1;
		y1 = fy1*CS->surface_h;
		x2 = (1+(int)(fx2*CS->surface_h));// & ~1;
		y2 = fy2*CS->surface_h;

		if(decoder->channel_mix_half_res)
			parallax *= 2;
		switch(decoder->frame.resolution)
		{
		default:
		case DECODED_RESOLUTION_FULL:
			break;
		case DECODED_RESOLUTION_HALF:
			parallax >>= 1;
			break;
		case DECODED_RESOLUTION_QUARTER:
			parallax >>= 2;
			break;
		}
		CS->rectarray[CS->rects].parallax = parallax;

		// always re-render the Vectorscope
		{
			memcpy(&CS->rectarray[CS->rects].lastParams, &decoder->MDPcurrent, sizeof(MDParams));
			CS->rectarray[CS->rects].lastParams.display_opacity = 1.0;


			CS->rectarray[CS->rects].x1 = x1;
			CS->rectarray[CS->rects].x2 = x2;
			CS->rectarray[CS->rects].y1 = y1;
			CS->rectarray[CS->rects].y2 = y2;

			// draw Vectorscope
			if(decoder->cairo_loaded)
			{
				CAIROlib *cairo = &CS->cairo;
				uint8_t *cairo_buffer = (uint8_t *)cairo->image_surface_get_data(CS->surface);
				VectorscopeCairoRender(decoder, cairo_buffer, CS->surface_w*4, x1, y1, x2, y2, 210);
			}
			else if(CS->cairoless_buffer)
			{	
				VectorscopeCairoRender(decoder, CS->cairoless_buffer, CS->surface_w*4, x1, y1, x2, y2, 210);
			}
			//cairo->surface_write_to_png(CS->surface, "C:\\Users\\Public\\surface.png");
		}
		CS->rects++;
	}
#endif

	for(i=0; i<CS->rects; i++)
	{
		bool Style2 = (0==strcmp(CS->rectarray[i].lastString, "Tool:Vectorscope2"));
		if(Style2 || 0==strcmp(CS->rectarray[i].lastString, "Tool:Vectorscope"))
		{			
			x1 = CS->rectarray[i].x1;
			x2 = CS->rectarray[i].x2;
			y1 = CS->rectarray[i].y1;
			y2 = CS->rectarray[i].y2;

			// draw Vectorscope
			if(x2 != x1 && y2 != y1)
			{
				if(decoder->cairo_loaded)
				{
					CAIROlib *cairo = &CS->cairo;
					uint8_t *cairo_buffer = (uint8_t *)cairo->image_surface_get_data(CS->surface);
					VectorscopeCairoRender(decoder, cairo_buffer, CS->surface_w*4, x1, y1, x2, y2, Style2);
				}
				else if(CS->cairoless_buffer)
				{	
					VectorscopeCairoRender(decoder, CS->cairoless_buffer, CS->surface_w*4, x1, y1, x2, y2, Style2);
				}
			}
		}
	}
}



void DrawGrid(DECODER *decoder, int parallax)
{
	double posx,posy,fsize;
	CAIROSTUFF *CS = NULL;
	int count,i;
	int x1,x2,y1,y2;
	char *specifier = NULL;
	int typepos = 0;

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS)
		return;

	for(i=0; i<CS->rects; i++)
	{		
		if(0 == strncmp(CS->rectarray[i].lastString, "Tool:Grid", 9))
		{
			int style = CS->rectarray[i].lastString[9] - '0';
			float size = CS->rectarray[i].lastParams.fontsize;
			x1 = CS->rectarray[i].x1;
			x2 = CS->rectarray[i].x2;
			y1 = CS->rectarray[i].y1;
			y2 = CS->rectarray[i].y2;

            // OS: always re-render grid (fix bug where other tools are duplicated (not moved)
            // when their position is changed and the grid is enabled.
            // Original conditional that bypasses grid re-rendering:
            //   if (CS->rectarray[i].lastParams.stroke_width == size)
            // I'm leaving this conditional here in the comment because it might help influence
            // a future refactor of the burn-in tools that could be more
            // performance-oriented (e.g., render tools into their own side-buffers
            // once - for those that are static, then composite each tool on to the video frame
            // in the proper order).
			if(0)
			{
				//all rendered.
			}
			else	// draw Grod
			if(x2 != x1 && y2 != y1)
			{
				if(decoder->cairo_loaded)
				{
					CAIROlib *cairo = &CS->cairo;
					uint8_t *cairo_buffer = (uint8_t *)cairo->image_surface_get_data(CS->surface);
					GridCairoRender(decoder, cairo_buffer, CS->surface_w*4, x1, y1, x2, y2, style, size);
				}
				else if(CS->cairoless_buffer)
				{	
					GridCairoRender(decoder, CS->cairoless_buffer, CS->surface_w*4, x1, y1, x2, y2, style, size);
				}

				CS->rectarray[i].lastParams.stroke_width = size;
			}
		}
	}
}






void DrawMetadataString(DECODER *decoder, unsigned char type, int size, void *data, int parallax)
{
	double posx,posy,fsize;
	cairo_text_extents_t extents;
	CAIROSTUFF *CS = NULL;
	lpCairoLib	cairoLib;
	CAIROlib *cairo;
	cairo_t *cr;
	cairo_t *cr1X;
	cairo_t *cr2X;
	char temp[256] = "";
	char str2[256] = "";
	int count,i;
	char basefmt[64] = "";
	unsigned char *ucdata = (unsigned char *)data;
	unsigned short *usdata = (unsigned short *)data;
	uint32_t *uldata = (uint32_t *)data;
	float *fdata = (float *)data;
	double *ddata = (double *)data;
	char *specifier = NULL;
	int typepos = 0;

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS) 
		return;

	if(!decoder->cairo_loaded)
		return;

	cairo = &CS->cairo;
	cr1X = CS->cr;
	cr2X = CS->cr2X;

	posx = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][0];
	posy = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][1];

	if(posx == -1.0 || posy == -1.0)
	{
		posx = 0.5 * (float)CS->surface_w / (float)CS->surface_h;
		posy = 0.5;
		if(decoder->MDPcurrent.justication & JUSTIFY_LEFT)
			posx = decoder->OverlaySafe[0] * (float)CS->surface_w / (float)CS->surface_h;
		if(decoder->MDPcurrent.justication & JUSTIFY_RIGHT)
			posx = (1.0 - decoder->OverlaySafe[0]) * (float)CS->surface_w / (float)CS->surface_h;
		if(decoder->MDPcurrent.justication & JUSTIFY_TOP)
			posy = decoder->OverlaySafe[1];
		if(decoder->MDPcurrent.justication & JUSTIFY_BOTTOM)
			posy = (1.0 - decoder->OverlaySafe[1]);
	}
	decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][0] = posx;
	decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][1] = posy;

	fsize = decoder->MDPcurrent.fontsize;

	if(fsize > 0.1)
		cr = cr1X;
	else
		cr = cr2X;

	cairo->set_font_size(cr, fsize);
	cairo->select_font_face (cr, decoder->MDPcurrent.font,
				  CAIRO_FONT_SLANT_NORMAL,
				  CAIRO_FONT_WEIGHT_NORMAL);

	switch(type)
	{
	default:	
	case METADATA_TYPE_STRING:
		strcpy(basefmt, "%s");
		count = size;
		break;
	case METADATA_TYPE_SIGNED_BYTE:
	case METADATA_TYPE_UNSIGNED_BYTE:
		strcpy(basefmt, "%d");
		count = size;
		break;
	case METADATA_TYPE_FLOAT:
		strcpy(basefmt, "%3.3");
		count = size / sizeof(float); 
		break;
	case METADATA_TYPE_DOUBLE:
		strcpy(basefmt, "%5.5");
		count = size / sizeof(double); 
		break;
	case METADATA_TYPE_HIDDEN:
		return; // do not display
		break;
	case METADATA_TYPE_UNSIGNED_LONG_HEX:
		strcpy(basefmt, "0x%p");
		count = size / 4; 
		break;
	case METADATA_TYPE_SIGNED_LONG:
	case METADATA_TYPE_UNSIGNED_LONG:
		strcpy(basefmt, "%d");
		count = size / 4; 
		break;
	case METADATA_TYPE_SIGNED_SHORT:
	case METADATA_TYPE_UNSIGNED_SHORT:
		strcpy(basefmt, "%d");
		count = size / 2; 
		break;
	case METADATA_TYPE_GUID:
		decoder->MDPcurrent.format_str[0] = 0; //don't use other formatting
		strcpy(basefmt, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X");
		count = size / 16; 
		break;
	case METADATA_TYPE_FOURCC:
		decoder->MDPcurrent.format_str[0] = 0; //don't use other formatting
		strcpy(basefmt, "%c%c%c%c");
		count = size / 4; 
		break;
	}

	if(decoder->MDPcurrent.format_str[0])
	{
		int i;
		specifier = strchr(decoder->MDPcurrent.format_str, '%');
		if(specifier)
		{
			typepos = 1;

			//while(specifier[typepos] != NULL)
			while(specifier[typepos] != '\0')
			{
				if(	(specifier[typepos] >= 'a' && specifier[typepos] <= 'z') ||
					(specifier[typepos] >= 'A' && specifier[typepos] <= 'Z'))
				{
					break;
				}
				typepos++;
			}

			if(specifier[typepos] == 's' && type != METADATA_TYPE_STRING)
			{
				decoder->MDPcurrent.format_str[0] = 0;
			}
		}
	}
	else
	{
		specifier = NULL;
		typepos = 0;
	}
	
	if(decoder->MDPcurrent.format_str[0])
	{
		int i;
		char *specifier = strchr(decoder->MDPcurrent.format_str, '%');

		if(specifier == NULL)
		{
			strcpy(str2, decoder->MDPcurrent.format_str);
		}
		else
		{
			strncpy(temp, (char *)data, size);
			temp[size] = '\0';
			if(type == METADATA_TYPE_STRING)
			{
				if(specifier[typepos] == 's')
				{
					sprintf(str2, decoder->MDPcurrent.format_str, temp);
				}
				else
				{
					strcpy(str2, temp);
				}
			}
			else
			{
				switch(type)
				{
					case METADATA_TYPE_FLOAT:
						switch(count)
						{
						case 1:
							sprintf(str2, decoder->MDPcurrent.format_str, fdata[0]);
							break;
						case 2:
							sprintf(str2, decoder->MDPcurrent.format_str, fdata[0], fdata[1]);
							break;
						case 3:
							sprintf(str2, decoder->MDPcurrent.format_str, fdata[0], fdata[1], fdata[2]);
							break;
						case 4:
							sprintf(str2, decoder->MDPcurrent.format_str, fdata[0], fdata[1], fdata[2], fdata[3]);
							break;
						}
						break;
					case METADATA_TYPE_DOUBLE:
						switch(count)
						{
						case 1:
							sprintf(str2, decoder->MDPcurrent.format_str, ddata[0]);
							break;
						case 2:
							sprintf(str2, decoder->MDPcurrent.format_str, ddata[0], ddata[1]);
							break;
						case 3:
							sprintf(str2, decoder->MDPcurrent.format_str, ddata[0], ddata[1], ddata[2]);
							break;
						case 4:
							sprintf(str2, decoder->MDPcurrent.format_str, ddata[0], ddata[1], ddata[2], ddata[3]);
							break;
						}
						break;
						break;
						
					default:	
					case METADATA_TYPE_SIGNED_BYTE:
					case METADATA_TYPE_UNSIGNED_BYTE:
						switch(count)
						{
						case 1:
							sprintf(str2, decoder->MDPcurrent.format_str, ucdata[0]);
							break;
						case 2:
							sprintf(str2, decoder->MDPcurrent.format_str, ucdata[0], ucdata[1]);
							break;
						case 3:
							sprintf(str2, decoder->MDPcurrent.format_str, ucdata[0], ucdata[1], ucdata[2]);
							break;
						case 4:
							sprintf(str2, decoder->MDPcurrent.format_str, ucdata[0], ucdata[1], ucdata[2], ucdata[3]);
							break;
						}
						break;

					case METADATA_TYPE_SIGNED_SHORT:
					case METADATA_TYPE_UNSIGNED_SHORT:
						switch(count)
						{
						case 1:
							sprintf(str2, decoder->MDPcurrent.format_str, usdata[0]);
							break;
						case 2:
							sprintf(str2, decoder->MDPcurrent.format_str, usdata[0], usdata[1]);
							break;
						case 3:
							sprintf(str2, decoder->MDPcurrent.format_str, usdata[0], usdata[1], usdata[2]);
							break;
						case 4:
							sprintf(str2, decoder->MDPcurrent.format_str, usdata[0], usdata[1], usdata[2], usdata[3]);
							break;
						}
						break;

					case METADATA_TYPE_UNSIGNED_LONG_HEX:
					case METADATA_TYPE_SIGNED_LONG:
					case METADATA_TYPE_UNSIGNED_LONG:
						switch(count)
						{
						case 1:
							sprintf(str2, decoder->MDPcurrent.format_str, uldata[0]);
							break;
						case 2:
							sprintf(str2, decoder->MDPcurrent.format_str, uldata[0], uldata[1]);
							break;
						case 3:
							sprintf(str2, decoder->MDPcurrent.format_str, uldata[0], uldata[1], uldata[2]);
							break;
						case 4:
							sprintf(str2, decoder->MDPcurrent.format_str, uldata[0], uldata[1], uldata[2], uldata[3]);
							break;
						}
						break;
				}
			}
		}
	}
	else
	{
		int i;
		switch(type)
		{
		case METADATA_TYPE_STRING:
			strncpy(str2, (char *)data, size);
			str2[size] = '\0';
			break;
		case METADATA_TYPE_FLOAT:
			for(i=0;i<count;i++)
			{
				sprintf(temp, basefmt, fdata[i]);
				strcat(str2,temp);

				if(i+1 < count)
				{
					strcat(str2,",");
				}
			}
			break;
		case METADATA_TYPE_DOUBLE:
			for(i=0;i<count;i++)
			{
				sprintf(temp, basefmt, ddata[i]);
				strcat(str2,temp);

				if(i+1 < count)
				{
					strcat(str2,",");
				}
			}
			break;
		case METADATA_TYPE_SIGNED_BYTE:
		case METADATA_TYPE_UNSIGNED_BYTE:
			for(i=0;i<count;i++)
			{
				sprintf(temp, basefmt, ucdata[i]);
				strcat(str2,temp);

				if(i+1 < count)
				{
					strcat(str2,",");
				}
			}
			break;
		case METADATA_TYPE_GUID:
			{				
				myGUID *guid = (myGUID *)data;
				sprintf(str2, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
										guid->Data1,
										guid->Data2,
										guid->Data3,
										guid->Data4[0],
										guid->Data4[1],
										guid->Data4[2],
										guid->Data4[3],
										guid->Data4[4],
										guid->Data4[5],
										guid->Data4[6],
										guid->Data4[7]);
			}
			break;
		case METADATA_TYPE_FOURCC:
			for(i=0;i<count;i++)
			{
				sprintf(temp, "%c%c%c%c", ucdata[i*4], ucdata[i*4+1], ucdata[i*4+2], ucdata[i*4+3]);
				strcat(str2,temp);

				if(i+1 < count)
				{
					strcat(str2,",");
				}
			}
			break;
		case METADATA_TYPE_UNSIGNED_LONG_HEX:
		case METADATA_TYPE_SIGNED_LONG:
		case METADATA_TYPE_UNSIGNED_LONG:
			for(i=0;i<count;i++)
			{
				sprintf(temp, basefmt, uldata[i]);
				strcat(str2,temp);

				if(i+1 < count)
				{
					strcat(str2,",");
				}
			}
			break;
		case METADATA_TYPE_SIGNED_SHORT:
		case METADATA_TYPE_UNSIGNED_SHORT:
			for(i=0;i<count;i++)
			{
				sprintf(temp, basefmt, usdata[i]);
				strcat(str2,temp);

				if(i+1 < count)
				{
					strcat(str2,",");
				}
			}
			break;
		}
	}
	strcat(str2, " "); // added the space helps, why?



#define BORDERX	0.005		//(fontextents.height*0.16)
#define BORDERY	0.001		//(fontextents.height*0.03)

	if(CS->rects < 64)
	{
		double yoffset;
		int x1,x2,y1,y2;
		double fx1,fx2,fy1,fy2;
		cairo_font_extents_t fontextents;

		cairo->font_extents(cr, &fontextents); 
		cairo->text_extents(cr, str2, &extents);

		if(decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][0] == posx && decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][1] == posy)
		{
			// unmoved: auto line increment.
			if(decoder->MDPcurrent.justication & JUSTIFY_TOP)
			{
				posy = decoder->last_container_y2[decoder->MDPcurrent.justication & 0xf];
			}
			else if(decoder->MDPcurrent.justication & JUSTIFY_BOTTOM)
			{
				posy = decoder->last_container_y1[decoder->MDPcurrent.justication & 0xf];
			}
			else // centre
			{
				posy = decoder->last_container_y2[decoder->MDPcurrent.justication & 0xf] + fontextents.height*0.5 + BORDERY;
			}
		}
		decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][0] = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication & 0xf][0];
		decoder->last_xypos[decoder->MDPcurrent.justication & 0xf][1] = decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication & 0xf][1];

		if(decoder->MDPcurrent.justication & JUSTIFY_LEFT)
		{
			fx1 = (posx);
			fx2 = fx1 + (extents.width+BORDERX*2.0);
		}
		else if(decoder->MDPcurrent.justication & JUSTIFY_RIGHT)
		{
			fx1 = posx - (extents.width+BORDERX*2.0);
			fx2 = (posx);
		}
		else
		{
			fx1 = (posx-extents.width*0.5-BORDERX);
			fx2 = fx1 + (extents.width+BORDERX*2.0);
		}

		if(decoder->MDPcurrent.justication & JUSTIFY_TOP)
		{
			fy1 = (posy);
			fy2 = fy1 + (fontextents.height + BORDERY*2.0);
		}
		else if(decoder->MDPcurrent.justication & JUSTIFY_BOTTOM)
		{
			fy1 = posy - (fontextents.height + BORDERY*2.0);
			fy2 = (posy);
		}
		else
		{
			fy1 = posy - (fontextents.height*0.5 + BORDERY);
			fy2 = fy1 + (fontextents.height+BORDERY*2.0);
		}

		decoder->last_container_y1[decoder->MDPcurrent.justication & 0xf] = fy1;
		decoder->last_container_y2[decoder->MDPcurrent.justication & 0xf] = fy2;

		x1 = ((int)(fx1*CS->surface_h));// & ~1;
		y1 = fy1*CS->surface_h;
		x2 = (1+(int)(fx2*CS->surface_h));// & ~1;
		y2 = fy2*CS->surface_h;

		if(decoder->channel_mix_half_res)
			parallax *= 2;
		switch(decoder->frame.resolution)
		{
		default:
		case DECODED_RESOLUTION_FULL:
			break;
		case DECODED_RESOLUTION_HALF:
			parallax >>= 1;
			break;	
		case DECODED_RESOLUTION_QUARTER:
			parallax >>= 2;
			break;
		}
		CS->rectarray[CS->rects].parallax = parallax;

		if( 0 == strcmp(&CS->rectarray[CS->rects].lastString[0], str2) &&
			0 == memcmp(&CS->rectarray[CS->rects].lastParams, &decoder->MDPcurrent, sizeof(MDParams)) &&
				CS->rectarray[CS->rects].x1 == x1 &&
				CS->rectarray[CS->rects].y1 == y1 &&
				CS->rectarray[CS->rects].x2 == x2 &&
				CS->rectarray[CS->rects].y2 == y2)
		{
			//do nothing
			CS->rectarray[CS->rects].lastParams.display_opacity = decoder->MDPcurrent.display_opacity * decoder->MDPcurrent.fcolor[3]; //fill color's alpha
		}
		else 
		{
			bool slateAndStrokeMatch = false;
			strcpy(CS->rectarray[CS->rects].lastString, str2);
			memcpy(&CS->rectarray[CS->rects].lastParams, &decoder->MDPcurrent, sizeof(MDParams));
			CS->rectarray[CS->rects].lastParams.display_opacity = decoder->MDPcurrent.display_opacity * decoder->MDPcurrent.fcolor[3]; //fill color's alpha

			if(cr == cr2X)
				yoffset = fy1;
			else
				yoffset = 0.0;

			cairo->rectangle (cr, fx1 - 0.002, 
								  fy1 - yoffset,// - 0.002, 
								  (fx2-fx1) + 0.004, 
								  (fy2-fy1) + 0.004
								  );


			CS->rectarray[CS->rects].x1 = x1;
			CS->rectarray[CS->rects].x2 = x2;
			CS->rectarray[CS->rects].y1 = y1;
			CS->rectarray[CS->rects].y2 = y2;

			//cairo->save (cr);
			cairo->set_source_rgba (cr, 0, 0, 0, 0); // black 70%
			cairo->set_operator (cr, CAIRO_OPERATOR_SOURCE); // paint the alpha channel, not just mix.
			cairo->fill (cr);
			//cairo->restore (cr);

			cairo->set_operator (cr, CAIRO_OPERATOR_OVER);

			cairo->move_to (cr, fx1+BORDERX, fy1-yoffset+fontextents.height-fontextents.descent-BORDERY*1.4 );

			if(decoder->MDPcurrent.bcolor[3] > 0.0) // background slate is on.
			{
				if(	decoder->MDPcurrent.scolor[0] == decoder->MDPcurrent.bcolor[0] &&
					decoder->MDPcurrent.scolor[1] == decoder->MDPcurrent.bcolor[1] &&
					decoder->MDPcurrent.scolor[2] == decoder->MDPcurrent.bcolor[2])
				{
					slateAndStrokeMatch = true;
				}
			}
			
			if(decoder->MDPcurrent.stroke_width && !slateAndStrokeMatch)
			{
				float stroke_width = decoder->MDPcurrent.stroke_width;

				cairo->text_path (cr, str2);
		
				cairo->set_source_rgba (cr, 
					decoder->MDPcurrent.scolor[0], 
					decoder->MDPcurrent.scolor[1], 
					decoder->MDPcurrent.scolor[2], 
					1.0/*decoder->MDPcurrent.scolor[3]*/);

				switch(decoder->frame.resolution)
				{
				default:
				case DECODED_RESOLUTION_FULL:
					break;
				case DECODED_RESOLUTION_HALF:
					stroke_width *= 0.5;
					break;
				case DECODED_RESOLUTION_QUARTER:
					stroke_width *= 0.25;
					break;
				}

				cairo->set_line_width (cr, stroke_width/(float)CS->surface_h);
				cairo->stroke (cr);

				cairo->move_to (cr, fx1+BORDERX, fy1-yoffset+fontextents.height-fontextents.descent-BORDERY*1.4 );
				cairo->text_path (cr, str2);
				
				cairo->set_source_rgba (cr, 
					decoder->MDPcurrent.fcolor[0], 
					decoder->MDPcurrent.fcolor[1], 
					decoder->MDPcurrent.fcolor[2], 
					1.0/*decoder->MDPcurrent.fcolor[3]*/);
				cairo->fill_preserve (cr);
			}
			else
			{
				
				cairo->set_source_rgba (cr, 
					decoder->MDPcurrent.fcolor[0], 
					decoder->MDPcurrent.fcolor[1], 
					decoder->MDPcurrent.fcolor[2], 
					1.0/*decoder->MDPcurrent.fcolor[3]*/);
				
				/*{
					cairo_font_options_t *font_options;
					font_options = cairo->font_options_create ();
					cairo->get_font_options (cr, font_options);
					cairo->font_options_set_antialias (font_options, CAIRO_ANTIALIAS_GRAY);
					cairo->set_font_options (cr, font_options);
					cairo->font_options_destroy (font_options);		
				}*/
				
				cairo->show_text (cr, str2);
			}

			if(cr == cr2X)
			{
				int j;
				cairo_matrix_t matrix;
				cairo->get_matrix(cr1X, &matrix);
				matrix.xx = 1.0/(double)OVERSAMPLE;
				matrix.yy = 1.0/(double)OVERSAMPLE;
				cairo->set_matrix(cr1X, &matrix);


				cairo->set_operator (cr1X, CAIRO_OPERATOR_SOURCE);
				cairo->set_source_rgba (cr1X, 
					decoder->MDPcurrent.bcolor[0], 
					decoder->MDPcurrent.bcolor[1], 
					decoder->MDPcurrent.bcolor[2], 
					decoder->MDPcurrent.bcolor[3] > 0.0 ? 1.0 : 0.0);
				cairo->rectangle (cr1X, CS->rectarray[CS->rects].x1*OVERSAMPLE, 
					CS->rectarray[CS->rects].y1*OVERSAMPLE, 
					(CS->rectarray[CS->rects].x2-CS->rectarray[CS->rects].x1)*OVERSAMPLE, 
					(CS->rectarray[CS->rects].y2-CS->rectarray[CS->rects].y1)*OVERSAMPLE);
				cairo->fill (cr1X);


				cairo->set_operator (cr1X, CAIRO_OPERATOR_OVER);
				cairo->set_source_surface (cr1X, CS->surface2X, 0, CS->rectarray[CS->rects].y1*OVERSAMPLE);
				
				cairo->rectangle (cr1X, CS->rectarray[CS->rects].x1*OVERSAMPLE, CS->rectarray[CS->rects].y1*OVERSAMPLE, 
					(CS->rectarray[CS->rects].x2-CS->rectarray[CS->rects].x1)*OVERSAMPLE, (CS->rectarray[CS->rects].y2-CS->rectarray[CS->rects].y1)*OVERSAMPLE);
				cairo->fill (cr1X);
			}
		}
		CS->rects++;
	}

//	cairo->surface_write_to_png(surface2X, "C:\\Users\\Public\\surface2X.png");
//	cairo->surface_write_to_png(surface, "C:\\Users\\Public\\surface.png");
}


void DrawLine(DECODER *decoder, double fx1, double fy1, double fx2, double fy2, double width)
{
	cairo_surface_t *image;
	CAIROSTUFF *CS = NULL;
	CAIROlib *cairo;

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS)
		return;

	if(!decoder->cairo_loaded)
		return;

	cairo = &CS->cairo;

	if(CS->rects < 64)
	{
		int x1,x2,y1,y2;
		
		x1 = ((int)(fx1*CS->surface_h));// & ~1;
		y1 = fy1*CS->surface_h;
		x2 = (1+(int)(fx2*CS->surface_h));// & ~1;
		y2 = fy2*CS->surface_h;

		x1 -= 2;
		x2 += 2;
		y1 -= 1;
		y2 += 1;

		if( CS->rectarray[CS->rects].x1 == x1 &&
			CS->rectarray[CS->rects].y1 == y1 &&
			CS->rectarray[CS->rects].x2 == x2 &&
			CS->rectarray[CS->rects].y2 == y2)
		{
			//do nothing
		}
		else 
		{
			strcpy(CS->rectarray[CS->rects].lastString, "");
			memset(&CS->rectarray[CS->rects].lastParams, 0, sizeof(MDParams));

			CS->rectarray[CS->rects].x1 = x1;
			CS->rectarray[CS->rects].x2 = x2;
			CS->rectarray[CS->rects].y1 = y1;
			CS->rectarray[CS->rects].y2 = y2;

			cairo->set_operator (CS->cr, CAIRO_OPERATOR_OVER);
			cairo->set_source_rgba (CS->cr, 1.0, 1.0, 1.0, 0.5); // white 50%
			cairo->set_line_width (CS->cr, width);

			cairo->move_to (CS->cr, fx1, fy1);
			cairo->line_to (CS->cr, fx2, fy2);

			cairo->stroke (CS->cr);
		}
		CS->rects++;
	}
}


void DrawSafeMarkers(DECODER *decoder)
{
	cairo_surface_t *image;
	CAIROSTUFF *CS = NULL;
	CAIROlib *cairo;

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS)
		return;

	if(!decoder->cairo_loaded)
		return;

	cairo = &CS->cairo;

	//ActiveSafe
	if(decoder->ActiveSafe[0] > 0.0 || decoder->ActiveSafe[1] > 0.0)
	{
		double fx1,fx2,fy1,fy2;

		fx1 = (double)(decoder->ActiveSafe[0] * (float)CS->surface_w/(float)CS->surface_h);
		fx2 = (double)((1.0 - decoder->ActiveSafe[0]) * (float)CS->surface_w/(float)CS->surface_h);
		fy1 = decoder->ActiveSafe[1]; 
		fy2 = 1.0 - decoder->ActiveSafe[1]; 

		DrawLine(decoder, fx1, fy1, fx2, fy1, 0.002);
		DrawLine(decoder, fx2, fy1, fx2, fy2, 0.002);
		DrawLine(decoder, fx1, fy2, fx2, fy2, 0.002);
		DrawLine(decoder, fx1, fy1, fx1, fy2, 0.002);
	}

	//TitleSafe
	if(decoder->TitleSafe[0] > 0.0 || decoder->TitleSafe[1] > 0.0)
	{
		double fx1,fx2,fy1,fy2;

		fx1 = (double)(decoder->TitleSafe[0] * (float)CS->surface_w/(float)CS->surface_h);
		fx2 = (double)((1.0 - decoder->TitleSafe[0]) * (float)CS->surface_w/(float)CS->surface_h);
		fy1 = decoder->TitleSafe[1]; 
		fy2 = 1.0 - decoder->TitleSafe[1]; 

		DrawLine(decoder, fx1, fy1, fx2, fy1, 0.002);
		DrawLine(decoder, fx2, fy1, fx2, fy2, 0.002);
		DrawLine(decoder, fx1, fy2, fx2, fy2, 0.002);
		DrawLine(decoder, fx1, fy1, fx1, fy2, 0.002);
	}
}


void DrawMetadataObjects(DECODER *decoder)
{
	int i,j;
	CAIROSTUFF *CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(CS == NULL) return;

	if(decoder->drawmetadataobjects && decoder->metadatachunks)
	{
		unsigned int size;
		unsigned char type;
		void *data;

		for(i=0; i<decoder->drawmetadataobjects; i++)
		{
			GetDisplayParameters(decoder, decoder->dmo[i], decoder->dmo_size[i]);

			for(j=decoder->metadatachunks-1; j>=0; j--)
			{
				data = NULL;
				if(decoder->MDPcurrent.tag)
				{
					if((data = MetadataFind((unsigned int *)decoder->mdc[j], decoder->mdc_size[j], decoder->MDPcurrent.tag, &size, &type)))
					{
						break;
					}
				}
				else if(decoder->MDPcurrent.freeform[0])
				{
					if((data = MetadataFindFreeform((unsigned int *)decoder->mdc[j], decoder->mdc_size[j], decoder->MDPcurrent.freeform, &size, &type)))
					{
						if(0 == strncmp("Gfx:", &decoder->MDPcurrent.freeform[0], 4) && size < 260)
						{
							int len;
							char path[260];
							memcpy(path, (char *)data, size);
							path[size] = 0;
							len = strlen(path);
							if(len > 4)
							{
#ifdef _WIN32
								if(0 == stricmp(&path[len - 4], ".png"))
#else
								if(0 == strcasecmp(&path[len-4], ".png"))
#endif
								{
									strcpy(decoder->MDPcurrent.png_path, path);
									decoder->MDPcurrent.object_scale[0] = decoder->MDPcurrent.fontsize * 1080.0 / 100.0 * (float)CS->decoder_w / (float)CS->decoder_h;
									decoder->MDPcurrent.object_scale[1] = decoder->MDPcurrent.fontsize * 1080.0 / 100.0;
									data = NULL;
								}
							}
						}
						
						if(0 == strncmp("Tool:", &decoder->MDPcurrent.freeform[0], 5))
						{
							decoder->MDPcurrent.object_scale[0] = decoder->MDPcurrent.fontsize * 1080.0 / 100.0 * (float)CS->decoder_w / (float)CS->decoder_h;
							decoder->MDPcurrent.object_scale[1] = decoder->MDPcurrent.fontsize * 1080.0 / 100.0;
							//data = NULL; // data has subtype
						}
						break;
					}
				}
				else if(decoder->MDPcurrent.format_str[0]) // used to display any strings
				{
					data = decoder->MDPcurrent.format_str;
					size = sizeof(data);
					type = 'c';
				}
			}
			if(0 == strncmp("Tool:", &decoder->MDPcurrent.freeform[0], 5))
			{
				if(decoder->cfhddata.BurninFlags & 2) // tools
				{
					DrawPrepareTool(decoder, &decoder->MDPcurrent.freeform[0], (char *)data,
						decoder->MDPcurrent.object_scale[0], decoder->MDPcurrent.object_scale[1], decoder->MDPcurrent.parallax);
					decoder->dmo_png_width[i] = decoder->dmo_png_height[i] = 0;
				}
			}
			else if(data)
			{
				if(decoder->cfhddata.BurninFlags & 1) // overlays
				{
					DrawMetadataString(decoder, type, size, data, decoder->MDPcurrent.parallax);
					decoder->dmo_png_width[i] = decoder->dmo_png_height[i] = 0;
				}
			}
			else if(decoder->MDPcurrent.png_path[0])
			{
				if(decoder->cfhddata.BurninFlags & 1) // overlays
				{
					DrawPNG(decoder, decoder->MDPcurrent.png_path, 
						decoder->MDPcurrent.object_scale[0], decoder->MDPcurrent.object_scale[1], decoder->MDPcurrent.parallax, 
						&decoder->dmo_png_width[i], &decoder->dmo_png_height[i], &decoder->dmo_png_path[i][0]);
				}
			}
		}
	}
}



void DoDrawScreen(DECODER *decoder, uint8_t *output, int pitch, int output_format, uint8_t *cairo_buffer, int pixelsX, int pixelsY, int right, int alphaR, int alphaG, int alphaB)
{
	CAIROSTUFF *CS = NULL;
	int whitepoint = 16;
	int reversed = 0;
	CS = (CAIROSTUFF *)decoder->cairoHandle;
	
	//fprintf(stderr,"DoDrawScreen %d\n",output_format);

	if(output_format & 0x80000000) // flipped
	{
		output += pitch*(CS->decoder_h-1);
		pitch = -pitch;
	}

	switch(output_format & 0xfffffff)
	{
		case COLOR_FORMAT_UYVY:
		case COLOR_FORMAT_YUYV:
		case COLOR_FORMAT_YU64:
		case COLOR_FORMAT_V210:
		case COLOR_FORMAT_2VUY:
		case COLOR_FORMAT_CbYCrY_8bit:
			{
				int x,y,a,basesrc,basedst;
				uint8_t *bptr = output;
				int skip = 2;				
				int shift = 8;
				float fprecision = (float)(1<<shift);
				int y_rmult = fprecision * 0.183;
				int y_gmult = fprecision * 0.614;
				int y_bmult = fprecision * 0.062;
				int y_offset= 16;
				int u_rmult = fprecision * 0.101;
				int u_gmult = fprecision * 0.338;
				int u_bmult = fprecision * 0.439;
				int u_offset= 128;
				int v_rmult = fprecision * 0.439;
				int v_gmult = fprecision * 0.399;
				int v_bmult = fprecision * 0.040;
				int v_offset= 128;
				uint8_t *outputY = output;
				uint8_t *outputC = output + 1;
				uint16_t *outputY16 = (uint16_t *)output;
				uint16_t *outputC16 = outputY16 + 1;
				int ymin = CS->decoder_h; //0
				int ymax = 0;//CS->decoder_w
				int k,j;

				if(output_format == COLOR_FORMAT_UYVY ||
				   output_format == COLOR_FORMAT_2VUY ||
				   output_format == COLOR_FORMAT_CbYCrY_8bit)
				{
					outputY = output + 1;
					outputC = output;
				}
				if(output_format == COLOR_FORMAT_YU64)
				{
					pitch /= 2;
				}
				if(output_format == COLOR_FORMAT_V210)
					skip = 0;


				for(k=0; k<CS->rects; k++)
				{						
					if(ymin > CS->rectarray[k].y1)
						ymin = CS->rectarray[k].y1;
					if(ymax < CS->rectarray[k].y2)
						ymax = CS->rectarray[k].y2;
				}

				if(ymin < 0) ymin = 0;
				if(ymax > CS->decoder_h) ymax = CS->decoder_h;

				for(y=ymin/pixelsY; y<ymax/pixelsY; y++)
				{
					for(k=0; k<CS->rects; k++)
					{						
						int xmin = CS->decoder_w; //0
						int xmax = 0;//CS->decoder_w
						int cairopitch = CS->surface_w * 4;
						int opacity,oR,oG,oB,oY,oU,oV;
						int anaglyph = 1;

						if(CS->rectarray[k].lastParams.display_opacity == 0.0)
						{
							oR = alphaR;
							oG = alphaG;
							oB = alphaB;
						}
						else
						{
							oR = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaR)>>8;
							oG = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaG)>>8;
							oB = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaB)>>8;
						}
						opacity = (oR + oG + oB) / 3;

						if(oR == oG && oG == oB)
							anaglyph = 0;



						basesrc = y * pixelsY * cairopitch;
						basedst = y * pitch;

						if(CS->rectarray[k].y1 <= y*pixelsY && y*pixelsY < CS->rectarray[k].y2)
						{
							int xminout,xmaxout;
							int converge = CS->rectarray[k].parallax;
							if(right)
								converge = -converge;

							if(xmin > CS->rectarray[k].x1)
								xmin = CS->rectarray[k].x1;
							if(xmax < CS->rectarray[k].x2)
								xmax = CS->rectarray[k].x2;

							for(j=0; j<CS->rects; j++)
							{
								if(j!=k)
								{
									if(CS->rectarray[j].y1 <= y && y < CS->rectarray[j].y2)
									{
										if(CS->rectarray[j].x1 < xmax && CS->rectarray[j].x2 >= xmax)
										{
											xmax = CS->rectarray[j].x1;
										}
									}
								}
							}


							xminout = xmin+converge;
							xmaxout = xmax+converge;
							if(xmin < 0) xmin = 0;
							if(xmax > CS->decoder_w) xmax = CS->decoder_w;
							if(xminout < 0) 
							{ 
							//	xmin += -xminout; 
								xminout = 0; 
							}
							if(xmaxout > CS->decoder_w) 
							{
							//	xmax -= (xmaxout - CS->decoder_w);
								xmaxout = CS->decoder_w;
							}

					
							if(xmin < xmax)
							{
								int xpos = 0;
								int offset = ((xminout/pixelsX));
								int poffset = 0;

								if(xmin > 0)
									basesrc += 4*(xmin -(offset & 1));
								else
									basesrc += 4*xmin;

								basedst += skip*(offset & ~1);


								if(pixelsX == 2)
								{
									if(xminout & 1 && xmin > 0) // half pixel shift
										poffset = -4;
								}

								for(x=xmin/pixelsX; x<xmax/pixelsX; x+=2,xpos+=2)
								{
									int r1,g1,b1,a1;
									int r2,g2,b2,a2;
									int y1,y2,u1,u2,v1,v2;

									if(pixelsX == 2)
									{
										b1 = (cairo_buffer[basesrc+0+poffset] + cairo_buffer[basesrc+4+poffset])>>1;
										g1 = (cairo_buffer[basesrc+1+poffset] + cairo_buffer[basesrc+5+poffset])>>1;
										r1 = (cairo_buffer[basesrc+2+poffset] + cairo_buffer[basesrc+6+poffset])>>1;
										a1 = (cairo_buffer[basesrc+3+poffset] + cairo_buffer[basesrc+7+poffset])>>1;
										basesrc += 8;
										
										b2 = (cairo_buffer[basesrc+0+poffset] + cairo_buffer[basesrc+4+poffset])>>1;
										g2 = (cairo_buffer[basesrc+1+poffset] + cairo_buffer[basesrc+5+poffset])>>1;
										r2 = (cairo_buffer[basesrc+2+poffset] + cairo_buffer[basesrc+6+poffset])>>1;
										a2 = (cairo_buffer[basesrc+3+poffset] + cairo_buffer[basesrc+7+poffset])>>1;
										basesrc += 8;
									}
									else if(pixelsY == 2)
									{
										b1 = (cairo_buffer[basesrc+0] + cairo_buffer[basesrc+0+cairopitch])>>1;
										g1 = (cairo_buffer[basesrc+1] + cairo_buffer[basesrc+1+cairopitch])>>1;
										r1 = (cairo_buffer[basesrc+2] + cairo_buffer[basesrc+2+cairopitch])>>1;
										a1 = (cairo_buffer[basesrc+3] + cairo_buffer[basesrc+3+cairopitch])>>1;
										basesrc += 4;

										b2 = (cairo_buffer[basesrc+0] + cairo_buffer[basesrc+0+cairopitch])>>1;
										g2 = (cairo_buffer[basesrc+1] + cairo_buffer[basesrc+1+cairopitch])>>1;
										r2 = (cairo_buffer[basesrc+2] + cairo_buffer[basesrc+2+cairopitch])>>1;
										a2 = (cairo_buffer[basesrc+3] + cairo_buffer[basesrc+3+cairopitch])>>1;
										basesrc += 4;
									}
									else
									{
										b1 = cairo_buffer[basesrc+0];
										g1 = cairo_buffer[basesrc+1];
										r1 = cairo_buffer[basesrc+2];
										a1 = cairo_buffer[basesrc+3];
										basesrc += 4;

										b2 = cairo_buffer[basesrc+0];
										g2 = cairo_buffer[basesrc+1];
										r2 = cairo_buffer[basesrc+2];
										a2 = cairo_buffer[basesrc+3];
										basesrc += 4;
									}

									if(a1 || a2)
									{
										int newY, newU, newV, newY2, newU2, newV2;
										int xoffset = (((xminout/pixelsX) + xpos) & ~1);
										uint32_t *lptr = (uint32_t *)&outputY[basedst];											
										lptr += (xoffset/6)*4;
										
										if(COLOR_FORMAT_V210 == output_format)
										{							
											int temp;		
											switch(xoffset % 6)
											{
											case 0:
												newU = ((*lptr>>22) & 0xff); 
												newY = ((*lptr>>12) & 0xff);
												newV = ((*lptr>>02) & 0xff); 
												lptr++;
												newY2= ((*lptr>>02) & 0xff);
												lptr--;
												break;
											case 2:
												lptr++;
												newY = ((*lptr>>22) & 0xff);
												lptr++;
												newV = ((*lptr>>22) & 0xff);
												newY2= ((*lptr>>12) & 0xff);
												newU = ((*lptr>>02) & 0xff); 
												lptr -= 2;
												break;
											case 4:
												lptr+=2;
												newV = ((*lptr>>22) & 0xff); 
												lptr++;
												newY2= ((*lptr>>22) & 0xff); 
												newU = ((*lptr>>12) & 0xff); 
												newY = ((*lptr>>02) & 0xff);
												lptr -= 3;
												break;
											}
											temp = newV;
											newV = newU;
											newU = temp;
										}
										else if(COLOR_FORMAT_YU64 == output_format)
										{
											newY = outputY16[basedst]>>8;
											newV = outputC16[basedst]>>8;
											newY2= outputY16[basedst+2]>>8;
											newU = outputC16[basedst+2]>>8;
										}
										else
										{
											newY = outputY[basedst];
											newU = outputC[basedst];
											newY2= outputY[basedst+2];
											newV = outputC[basedst+2];
										}

										newY -= y_offset;
										newY2 -= y_offset;
										newU -= u_offset;
										newV -= v_offset;

										newU2 = newU;
										newV2 = newV;

										if(a1 == 255 && opacity == 256)
										{
											y1 = (((( y_rmult * r1)>>shift) + (( y_gmult * g1)>>shift) + (( y_bmult * b1)>>shift)));
											u1 = ((((-u_rmult * r1)>>shift) + ((-u_gmult * g1)>>shift) + (( u_bmult * b1)>>shift)));
											u2 = ((((-u_rmult * r2)>>shift) + ((-u_gmult * g2)>>shift) + (( u_bmult * b2)>>shift)));

											newY = y1;
											newU = (u1+u2)>>1;
										}
										else if(a1 && anaglyph == 0)
										{
											y1 = (((( y_rmult * r1)>>shift) + (( y_gmult * g1)>>shift) + (( y_bmult * b1)>>shift)));
											u1 = ((((-u_rmult * r1)>>shift) + ((-u_gmult * g1)>>shift) + (( u_bmult * b1)>>shift)));
											u2 = ((((-u_rmult * r2)>>shift) + ((-u_gmult * g2)>>shift) + (( u_bmult * b2)>>shift)));

											a1 *= opacity; a1 >>= 8;
											a1++;
											
											newY = (y1*a1 + newY*(256-a1))>>8;
											newU = (((u1+u2)>>1)*a1 + newU*(256-a1))>>8;
										}
										else
										{
											int aR = ((a1+1) * oR)>>8;
											int aG = ((a1+1) * oG)>>8;
											int aB = ((a1+1) * oB)>>8;
											int rD,gD,bD;
											int r,g,b;	

											rD = (9535*newY + 14688*newV)>>13;
											gD = (9535*newY - 4375 *newV - 1745*newU)>>13;
											bD = (9535*newY + 17326*newU)>>13;

											if(bD < 0) bD = 0; if(bD > 255) bD = 255;
											if(gD < 0) gD = 0; if(gD > 255) gD = 255;
											if(rD < 0) rD = 0; if(rD > 255) rD = 255;

											b = (b1*aB + bD*(256-aB))>>8;
											g = (g1*aG + gD*(256-aG))>>8;
											r = (r1*aR + rD*(256-aR))>>8;
											
											if(b < 0) b = 0; if(b > 255) b = 255;
											if(g < 0) g = 0; if(g > 255) g = 255;
											if(r < 0) r = 0; if(r > 255) r = 255;
											
											newY = (((( y_rmult * r)>>shift) + (( y_gmult * g)>>shift) + (( y_bmult * b)>>shift)));
											newU = ((((-u_rmult * r)>>shift) + ((-u_gmult * g)>>shift) + (( u_bmult * b)>>shift)));
										}

										
										if(a2 == 255 && opacity == 256)
										{
											y2 = (((( y_rmult * r2)>>shift) + (( y_gmult * g2)>>shift) + (( y_bmult * b2)>>shift)));
											v2 = (((( v_rmult * r2)>>shift) + ((-v_gmult * g2)>>shift) + ((-v_bmult * b2)>>shift)));
											v1 = (((( v_rmult * r1)>>shift) + ((-v_gmult * g1)>>shift) + ((-v_bmult * b1)>>shift)));

											newY2 = y2;
											newV = (v1+v2)>>1;
										}										
										else if(a2 && anaglyph == 0)
										{
											y2 = (((( y_rmult * r2)>>shift) + (( y_gmult * g2)>>shift) + (( y_bmult * b2)>>shift)));
											v2 = (((( v_rmult * r2)>>shift) + ((-v_gmult * g2)>>shift) + ((-v_bmult * b2)>>shift)));
											v1 = (((( v_rmult * r1)>>shift) + ((-v_gmult * g1)>>shift) + ((-v_bmult * b1)>>shift)));

											a2 *= opacity; a2 >>= 8;
											a2++;
											
											newY2 = (y2*a2 + newY2*(256-a2))>>8;
											newV = (((v1+v2)>>1)*a2 + newV2*(256-a2))>>8;											
										}
										else
										{
											int aR = ((a2+1) * oR)>>8;
											int aG = ((a2+1) * oG)>>8;
											int aB = ((a2+1) * oB)>>8;
											int rD,gD,bD;
											int r,g,b;
															
											rD = (9535*newY2 + 14688*newV2)>>13;
											gD = (9535*newY2 - 4375 *newV2 - 1745*newU2)>>13;
											bD = (9535*newY2 + 17326*newU2)>>13;

											if(bD < 0) bD = 0; if(bD > 255) bD = 255;
											if(gD < 0) gD = 0; if(gD > 255) gD = 255;
											if(rD < 0) rD = 0; if(rD > 255) rD = 255;
											
											b = (b2*aB + bD*(256-aB))>>8;
											g = (g2*aG + gD*(256-aG))>>8;
											r = (r2*aR + rD*(256-aR))>>8;

											if(b < 0) b = 0; if(b > 255) b = 255;
											if(g < 0) g = 0; if(g > 255) g = 255;
											if(r < 0) r = 0; if(r > 255) r = 255;
											
											newY2 = (((( y_rmult * r)>>shift) + (( y_gmult * g)>>shift) + (( y_bmult * b)>>shift)));
											newV = (((( v_rmult * r)>>shift) + ((-v_gmult * g)>>shift) + ((-v_bmult * b)>>shift)));

										}
										
										newY += y_offset;
										newY2 += y_offset;
										newU += u_offset;
										newV += v_offset;
										
										if(COLOR_FORMAT_V210 == output_format)
										{		
											int temp;
											temp = newV;
											newV = newU;
											newU = temp;
											switch(xoffset % 6)
											{
											case 0:
												*lptr = (newU<<22) | (newY << 12) | (newV << 02);
												lptr++;
												*lptr &= ~(0x3ff<<00);
												*lptr |=  (newY2<<02);
												break;
											case 2:
												lptr++;
												*lptr &= ~(0x3ff<<20);
												*lptr |= (newY<<22);		
												lptr++;
												*lptr = (newV<<22) | (newY2 << 12) | (newU << 02);
												break;
											case 4:
												lptr+=2;
												*lptr &= ~(0x3ff<<20);
												*lptr |=  (newV<<22);
												lptr++;
												*lptr = (newY2<<22) | (newU << 12) | (newY << 02);
												break;
											}
										}
										else if(COLOR_FORMAT_YU64 == output_format)
										{
											outputY16[basedst] = newY<<8;
											outputC16[basedst] = newV<<8;
											outputY16[basedst+2] = newY2<<8;
											outputC16[basedst+2] = newU<<8;
										}
										else
										{
											outputY[basedst] = newY;
											outputC[basedst] = newU;
											outputY[basedst+2] = newY2;
											outputC[basedst+2] = newV;
										}
									}
									basedst += skip*2;
								}
							}
						}
					}
				}
			}
			break;

		case COLOR_FORMAT_RGB24:
		case COLOR_FORMAT_RGB32:	
			{
				int x,y,a,basesrc,basedst;
				uint8_t *bptr = output;
				int skip = 4;
				int k,j;
				int ymin = CS->decoder_h; //0
				int ymax = 0;//CS->decoder_w


				if(output_format == COLOR_FORMAT_RGB24)
					skip = 3;

				for(k=0; k<CS->rects; k++)
				{						
					if(ymin > CS->rectarray[k].y1)
						ymin = CS->rectarray[k].y1;
					if(ymax < CS->rectarray[k].y2)
						ymax = CS->rectarray[k].y2;
				}
				
				if(ymin < 0) ymin = 0;
				if(ymax > CS->decoder_h) ymax = CS->decoder_h;

				for(y=ymin/pixelsY; y<ymax/pixelsY; y++)
				{

					for(k=0; k<CS->rects; k++)
					{						
						int xmin = CS->decoder_w; //0
						int xmax = 0;//CS->decoder_w
						int cairopitch = CS->surface_w * 4;
						int opacity,oR,oG,oB;
						int anaglyph = 1;

						if(CS->rectarray[k].lastParams.display_opacity == 0.0)
						{
							oR = alphaR;
							oG = alphaG;
							oB = alphaB;
						}
						else
						{
							oR = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaR)>>8;
							oG = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaG)>>8;
							oB = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaB)>>8;
						}
						opacity = (oR + oG + oB) / 3;

						if(oR == oG && oG == oB)
							anaglyph = 0;


						basesrc = y * pixelsY * cairopitch;
						basedst = (CS->decoder_h/pixelsY - y - 1) * pitch;


						if(CS->rectarray[k].y1 <= y*pixelsY && y*pixelsY < CS->rectarray[k].y2)
						{
							int xminout,xmaxout;
							int converge = CS->rectarray[k].parallax;
							if(right)
								converge = -converge;

							if(xmin > CS->rectarray[k].x1)
								xmin = CS->rectarray[k].x1;
							if(xmax < CS->rectarray[k].x2)
								xmax = CS->rectarray[k].x2;

							for(j=0; j<CS->rects; j++)
							{
								if(j!=k)
								{
									if(CS->rectarray[j].y1 <= y && y < CS->rectarray[j].y2)
									{
										if(CS->rectarray[j].x1 < xmax && CS->rectarray[j].x2 >= xmax)
										{
											xmax = CS->rectarray[j].x1;
										}
									}
								}
							}

							xminout = xmin+converge;
							xmaxout = xmax+converge;
							if(xmin < 0) xmin = 0;
							if(xmax > CS->decoder_w) xmax = CS->decoder_w;
							if(xminout < 0) 
							{ 
							//	xmin += -xminout; 
								xminout = 0; 
							}
							if(xmaxout > CS->decoder_w) 
							{
							//	xmax -= (xmaxout - CS->decoder_w);
								xmaxout = CS->decoder_w;
							}

					
							if(xmin < xmax)
							{
								basesrc += 4*xmin;
								basedst += skip*(xminout/pixelsX);

								if(pixelsX == 2)
								{
									int poffset = 0;
									if(xminout & 1 && xmin > 0) // half pixel shift
										poffset = -4;
										
									for(x=xmin; x<xmax; x+=pixelsX)
									{
										a = (cairo_buffer[basesrc+3] + cairo_buffer[basesrc+7])>>1;
										if(a)
										{
											if(a == 255 && opacity == 256)
											{
												output[basedst+0] = ((cairo_buffer[basesrc+0+poffset]+cairo_buffer[basesrc+4+poffset])>>1);
												output[basedst+1] = ((cairo_buffer[basesrc+1+poffset]+cairo_buffer[basesrc+5+poffset])>>1);
												output[basedst+2] = ((cairo_buffer[basesrc+2+poffset]+cairo_buffer[basesrc+6+poffset])>>1);
											}
											else
											{
												int aR = ((a+1) * oR)>>8;
												int aG = ((a+1) * oG)>>8;
												int aB = ((a+1) * oB)>>8;
												output[basedst+0] = (((cairo_buffer[basesrc+0+poffset]+cairo_buffer[basesrc+4+poffset])>>1)*aB + output[basedst+0+poffset]*(256-aB))>>8;
												output[basedst+1] = (((cairo_buffer[basesrc+1+poffset]+cairo_buffer[basesrc+5+poffset])>>1)*aG + output[basedst+1+poffset]*(256-aG))>>8;
												output[basedst+2] = (((cairo_buffer[basesrc+2+poffset]+cairo_buffer[basesrc+6+poffset])>>1)*aR + output[basedst+2+poffset]*(256-aR))>>8;
											}
										}
										basesrc += 8;
										basedst += skip;
									}
								}								
								else if(pixelsY == 2)
								{
									for(x=xmin/pixelsX; x<xmax/pixelsX; x++)
									{
										a = (cairo_buffer[basesrc+3] + cairo_buffer[basesrc+3+cairopitch])>>1;
										if(a)
										{
											if(a == 255 && opacity == 256)
											{
												output[basedst+0] = ((cairo_buffer[basesrc+0]+cairo_buffer[basesrc+0+cairopitch])>>1);
												output[basedst+1] = ((cairo_buffer[basesrc+1]+cairo_buffer[basesrc+1+cairopitch])>>1);
												output[basedst+2] = ((cairo_buffer[basesrc+2]+cairo_buffer[basesrc+2+cairopitch])>>1);
											}
											else
											{
												int aR = ((a+1) * oR)>>8;
												int aG = ((a+1) * oG)>>8;
												int aB = ((a+1) * oB)>>8;
												output[basedst+0] = (((cairo_buffer[basesrc+0]+cairo_buffer[basesrc+0+cairopitch])>>1)*aB + output[basedst+0]*(256-aB))>>8;
												output[basedst+1] = (((cairo_buffer[basesrc+1]+cairo_buffer[basesrc+1+cairopitch])>>1)*aG + output[basedst+1]*(256-aG))>>8;
												output[basedst+2] = (((cairo_buffer[basesrc+2]+cairo_buffer[basesrc+2+cairopitch])>>1)*aR + output[basedst+2]*(256-aR))>>8;
											}
										}
										basesrc += 4;
										basedst += skip;
									}
								}
								else 
								{
									for(x=xmin/pixelsX; x<xmax/pixelsX; x++)
									{
										if((a = cairo_buffer[basesrc+3]))
										{
											if(a == 255 && opacity == 256)
											{
												output[basedst+0] = cairo_buffer[basesrc+0];
												output[basedst+1] = cairo_buffer[basesrc+1];
												output[basedst+2] = cairo_buffer[basesrc+2];
											}
											else
											{
												int aR = ((a+1) * oR)>>8;
												int aG = ((a+1) * oG)>>8;
												int aB = ((a+1) * oB)>>8;
												output[basedst+0] = (cairo_buffer[basesrc+0]*aB + output[basedst+0]*(256-aB))>>8;
												output[basedst+1] = (cairo_buffer[basesrc+1]*aG + output[basedst+1]*(256-aG))>>8;
												output[basedst+2] = (cairo_buffer[basesrc+2]*aR + output[basedst+2]*(256-aR))>>8;
											}
										}
										basesrc += 4;
										basedst += skip;
									}
								}
							}
						}
					}
				}
			}
			break;

		case COLOR_FORMAT_AR10:
			reversed = 1;
		case COLOR_FORMAT_RG30:
		case COLOR_FORMAT_R210:
		case COLOR_FORMAT_DPX0:
		case COLOR_FORMAT_AB10:
			{
				int x,y,a,basesrc,basedst;
				uint32_t *ulptr = (uint32_t *)output;
				int swapped = 0,shifted = 0;
				int k,j;
				int ymin = CS->decoder_h; //0
				int ymax = 0;//CS->decoder_w

				if(output_format == COLOR_FORMAT_R210)
					swapped = 1;
				if(output_format == COLOR_FORMAT_DPX0)
					swapped = 1, shifted = 1;

				for(k=0; k<CS->rects; k++)
				{						
					if(ymin > CS->rectarray[k].y1)
						ymin = CS->rectarray[k].y1;
					if(ymax < CS->rectarray[k].y2)
						ymax = CS->rectarray[k].y2;
				}
				
				if(ymin < 0) ymin = 0;
				if(ymax > CS->decoder_h) ymax = CS->decoder_h;

				for(y=ymin/pixelsY; y<ymax/pixelsY; y++)
				{

					for(k=0; k<CS->rects; k++)
					{						
						int xmin = CS->decoder_w; //0
						int xmax = 0;//CS->decoder_w
						int cairopitch = CS->surface_w * 4;
						int opacity,oR,oG,oB;
						int anaglyph = 1;

						if(CS->rectarray[k].lastParams.display_opacity == 0.0)
						{
							oR = alphaR;
							oG = alphaG;
							oB = alphaB;
						}
						else
						{
							oR = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaR)>>(whitepoint-8);
							oG = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaG)>>(whitepoint-8);
							oB = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaB)>>(whitepoint-8);
						}
						opacity = (oR + oG + oB) / 3;

						if(oR == oG && oG == oB)
							anaglyph = 0;


						basesrc = y * pixelsY * cairopitch;
						basedst = y * pitch/4;


						if(CS->rectarray[k].y1 <= y*pixelsY && y*pixelsY < CS->rectarray[k].y2)
						{
							int xminout,xmaxout;
							int R,G,B;
							int converge = CS->rectarray[k].parallax;
							if(right)
								converge = -converge;

							if(xmin > CS->rectarray[k].x1)
								xmin = CS->rectarray[k].x1;
							if(xmax < CS->rectarray[k].x2)
								xmax = CS->rectarray[k].x2;

							for(j=0; j<CS->rects; j++)
							{
								if(j!=k)
								{
									if(CS->rectarray[j].y1 <= y && y < CS->rectarray[j].y2)
									{
										if(CS->rectarray[j].x1 < xmax && CS->rectarray[j].x2 >= xmax)
										{
											xmax = CS->rectarray[j].x1;
										}
									}
								}
							}

							xminout = xmin+converge;
							xmaxout = xmax+converge;
							if(xmin < 0) xmin = 0;
							if(xmax > CS->decoder_w) xmax = CS->decoder_w;
							if(xminout < 0) 
							{ 
							//	xmin += -xminout; 
								xminout = 0; 
							}
							if(xmaxout > CS->decoder_w) 
							{
							//	xmax -= (xmaxout - CS->decoder_w);
								xmaxout = CS->decoder_w;
							}

					
							if(xmin < xmax)
							{

								basesrc += 4*xmin;
								basedst += (xminout/pixelsX);

								if(pixelsX == 2)
								{
									int poffset = 0;
									if(xminout & 1 && xmin > 0) // half pixel shift
										poffset = -4;
										
									for(x=xmin; x<xmax; x+=pixelsX)
									{
										a = (cairo_buffer[basesrc+3] + cairo_buffer[basesrc+7])>>1;
										if(a)
										{
											if(a == 255 && opacity == 256)
											{
												uint32_t val;

												R = ((cairo_buffer[basesrc+0+poffset]+cairo_buffer[basesrc+4+poffset])>>1);
												G = ((cairo_buffer[basesrc+1+poffset]+cairo_buffer[basesrc+5+poffset])>>1);
												B = ((cairo_buffer[basesrc+2+poffset]+cairo_buffer[basesrc+6+poffset])>>1);
											
												if(reversed)
													val = (B<<22) | (G<<12) | (R<<2);
												else
													val = (R<<22) | (G<<12) | (B<<2);
												if(shifted)
													val <<= 2;
												if(swapped)
													val = SwapInt32BtoN(val);

												ulptr[basedst] = val;
											}
											else
											{
												int aR = ((a+1) * oR)>>8;
												int aG = ((a+1) * oG)>>8;
												int aB = ((a+1) * oB)>>8;
												uint32_t val = ulptr[basedst+poffset];

												if(swapped)
													val = SwapInt32BtoN(val);
												if(shifted)
													val >>= 2;
												if(reversed)
												{	
													B = (val>>22)&0xff;
													G = (val>>12)&0xff;
													R = (val>>02)&0xff;	
												}
												else
												{
													R = (val>>22)&0xff;
													G = (val>>12)&0xff;
													B = (val>>02)&0xff;	
												}

												B = (((cairo_buffer[basesrc+0+poffset]+cairo_buffer[basesrc+4+poffset])>>1)*aB + (B)*(256-aB))>>8;
												G = (((cairo_buffer[basesrc+1+poffset]+cairo_buffer[basesrc+5+poffset])>>1)*aG + (G)*(256-aG))>>8;
												R = (((cairo_buffer[basesrc+2+poffset]+cairo_buffer[basesrc+6+poffset])>>1)*aR + (R)*(256-aR))>>8;

												if(reversed)
													val = (B<<22) | (G<<12) | (R<<2);
												else
													val = (R<<22) | (G<<12) | (B<<2);
												if(shifted)
													val <<= 2;
												if(swapped)
													val = SwapInt32BtoN(val);

												ulptr[basedst] = val;
											}
										}
									}
									basesrc += 8;
									basedst++;
								}								
								else if(pixelsY == 2)
								{
									for(x=xmin/pixelsX; x<xmax/pixelsX; x++)
									{
										a = (cairo_buffer[basesrc+3] + cairo_buffer[basesrc+3+cairopitch])>>1;
										if(a)
										{
											if(a == 255 && opacity == 256)
											{
												uint32_t val;

												B = ((cairo_buffer[basesrc+0]+cairo_buffer[basesrc+0+cairopitch])>>1);
												G = ((cairo_buffer[basesrc+1]+cairo_buffer[basesrc+1+cairopitch])>>1);
												R = ((cairo_buffer[basesrc+2]+cairo_buffer[basesrc+2+cairopitch])>>1);
											
												if(reversed)
													val = (B<<22) | (G<<12) | (R<<2);
												else
													val = (R<<22) | (G<<12) | (B<<2);
												if(shifted)
													val <<= 2;
												if(swapped)
													val = SwapInt32BtoN(val);

												ulptr[basedst] = val;
											}
											else
											{
												int aR = ((a+1) * oR)>>8;
												int aG = ((a+1) * oG)>>8;
												int aB = ((a+1) * oB)>>8;
												uint32_t val = ulptr[basedst];

												if(swapped)
													val = SwapInt32BtoN(val);
												if(shifted)
													val >>= 2;
												if(reversed)
												{	
													B = (val>>22)&0xff;
													G = (val>>12)&0xff;
													R = (val>>02)&0xff;	
												}
												else
												{
													R = (val>>22)&0xff;
													G = (val>>12)&0xff;
													B = (val>>02)&0xff;	
												}

												B = (((cairo_buffer[basesrc+0]+cairo_buffer[basesrc+0+cairopitch])>>1)*aB + (B)*(256-aB))>>8;
												G = (((cairo_buffer[basesrc+1]+cairo_buffer[basesrc+1+cairopitch])>>1)*aG + (G)*(256-aG))>>8;
												R = (((cairo_buffer[basesrc+2]+cairo_buffer[basesrc+2+cairopitch])>>1)*aR + (R)*(256-aR))>>8;
											
												if(reversed)
													val = (B<<22) | (G<<12) | (R<<2);
												else
													val = (R<<22) | (G<<12) | (B<<2);
												if(shifted)
													val <<= 2;
												if(swapped)
													val = SwapInt32BtoN(val);

												ulptr[basedst] = val;
											}
										}
										basesrc += 4;
										basedst ++;
									}
								}
								else 
								{
									for(x=xmin/pixelsX; x<xmax/pixelsX; x++)
									{
										if((a = cairo_buffer[basesrc+3]))
										{
											if(a == 255 && opacity == 256)
											{
												uint32_t val;

												B = cairo_buffer[basesrc+0];
												G = cairo_buffer[basesrc+1];
												R = cairo_buffer[basesrc+2];
											
												if(reversed)
													val = (B<<22) | (G<<12) | (R<<2);
												else
													val = (R<<22) | (G<<12) | (B<<2);
												if(shifted)
													val <<= 2;
												if(swapped)
													val = SwapInt32BtoN(val);

												ulptr[basedst] = val;
											}
											else
											{
												int aR = ((a+1) * oR)>>8;
												int aG = ((a+1) * oG)>>8;
												int aB = ((a+1) * oB)>>8;
												uint32_t val = ulptr[basedst];

												if(swapped)
													val = SwapInt32BtoN(val);
												if(shifted)
													val >>= 2;
												if(reversed)
												{	
													B = (val>>22)&0xff;
													G = (val>>12)&0xff;
													R = (val>>02)&0xff;	
												}
												else
												{
													R = (val>>22)&0xff;
													G = (val>>12)&0xff;
													B = (val>>02)&0xff;	
												}

												B = (cairo_buffer[basesrc+0]*aB + (B)*(256-aB))>>8;
												G = (cairo_buffer[basesrc+1]*aG + (G)*(256-aG))>>8;
												R = (cairo_buffer[basesrc+2]*aR + (R)*(256-aR))>>8;
											
												if(reversed)
													val = (B<<22) | (G<<12) | (R<<2);
												else
													val = (R<<22) | (G<<12) | (B<<2);
												if(shifted)
													val <<= 2;
												if(swapped)
													val = SwapInt32BtoN(val);

												ulptr[basedst] = val;
											}
										}
										basesrc += 4;
										basedst ++;
									}
								}
							}
						}
					}
				}
			}
			break;

		case COLOR_FORMAT_WP13:
		case COLOR_FORMAT_W13A:
			whitepoint = 13;
		case COLOR_FORMAT_B64A:
		case COLOR_FORMAT_RG48:
			{
				int x,y,a,basesrc,basedst;
				uint16_t *usptr = (int16_t *)output;
				int16_t *sptr = (uint16_t *)output;
				int skip = 3;
				int k,j;
				int ymin = CS->decoder_h; //0
				int ymax = 0;//CS->decoder_w


				if(output_format == COLOR_FORMAT_B64A)
				{
					sptr++; // step over alpha
					usptr++; // step over alpha
					skip = 4;
				}
				if(output_format == COLOR_FORMAT_W13A)
				{
					skip = 4;
				}

				for(k=0; k<CS->rects; k++)
				{						
					if(ymin > CS->rectarray[k].y1)
						ymin = CS->rectarray[k].y1;
					if(ymax < CS->rectarray[k].y2)
						ymax = CS->rectarray[k].y2;
				}
				
				if(ymin < 0) ymin = 0;
				if(ymax > CS->decoder_h) ymax = CS->decoder_h;

				for(y=ymin/pixelsY; y<ymax/pixelsY; y++)
				{

					for(k=0; k<CS->rects; k++)
					{						
						int xmin = CS->decoder_w; //0
						int xmax = 0;//CS->decoder_w
						int cairopitch = CS->surface_w * 4;
						int opacity,oR,oG,oB;
						int anaglyph = 1;

						if(CS->rectarray[k].lastParams.display_opacity == 0.0)
						{
							oR = alphaR;
							oG = alphaG;
							oB = alphaB;
						}
						else
						{
							oR = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaR)>>8;
							oG = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaG)>>8;
							oB = ((int)(256.0 * CS->rectarray[k].lastParams.display_opacity) * alphaB)>>8;
						}
						opacity = (oR + oG + oB) / 3;

						if(oR == oG && oG == oB)
							anaglyph = 0;


						basesrc = y * pixelsY * cairopitch;
						basedst = y * pitch/2;


						if(CS->rectarray[k].y1 <= y*pixelsY && y*pixelsY < CS->rectarray[k].y2)
						{
							int xminout,xmaxout;
							int converge = CS->rectarray[k].parallax;
							if(right)
								converge = -converge;

							if(xmin > CS->rectarray[k].x1)
								xmin = CS->rectarray[k].x1;
							if(xmax < CS->rectarray[k].x2)
								xmax = CS->rectarray[k].x2;

							for(j=0; j<CS->rects; j++)
							{
								if(j!=k)
								{
									if(CS->rectarray[j].y1 <= y && y < CS->rectarray[j].y2)
									{
										if(CS->rectarray[j].x1 < xmax && CS->rectarray[j].x2 >= xmax)
										{
											xmax = CS->rectarray[j].x1;
										}
									}
								}
							}

							xminout = xmin+converge;
							xmaxout = xmax+converge;
							if(xmin < 0) xmin = 0;
							if(xmax > CS->decoder_w) xmax = CS->decoder_w;
							if(xminout < 0) 
							{ 
								xmin += -xminout; 
								xminout = 0; 
							}
							if(xmaxout > CS->decoder_w) 
							{
								xmax -= (xmaxout - CS->decoder_w);
								xmaxout = CS->decoder_w;
							}

					
							if(xmin < xmax)
							{

								basesrc += 4*xmin;
								basedst += skip*(xminout/pixelsX);

								if(pixelsX == 2)
								{
									int poffset = 0;
									if(xminout & 1 && xmin > 0) // half pixel shift
										poffset = -4;
										
									for(x=xmin; x<xmax; x+=pixelsX)
									{
										a = (cairo_buffer[basesrc+3+poffset] + cairo_buffer[basesrc+7+poffset])>>1;
										if(a)
										{
											if(whitepoint == 16)
											{
												if(a == 255 && opacity == 256)
												{
													usptr[basedst+2] = ((cairo_buffer[basesrc+0+poffset]+cairo_buffer[basesrc+4+poffset])<<7);
													usptr[basedst+1] = ((cairo_buffer[basesrc+1+poffset]+cairo_buffer[basesrc+5+poffset])<<7);
													usptr[basedst+0] = ((cairo_buffer[basesrc+2+poffset]+cairo_buffer[basesrc+6+poffset])<<7);
												}
												else
												{
													int aR = ((a+1) * oR)>>8;
													int aG = ((a+1) * oG)>>8;
													int aB = ((a+1) * oB)>>8;
													usptr[basedst+2] = (((cairo_buffer[basesrc+0+poffset]+cairo_buffer[basesrc+4+poffset])>>1)*aB + (usptr[basedst+2+poffset]>>8)*(256-aB));
													usptr[basedst+1] = (((cairo_buffer[basesrc+1+poffset]+cairo_buffer[basesrc+5+poffset])>>1)*aG + (usptr[basedst+1+poffset]>>8)*(256-aG));
													usptr[basedst+0] = (((cairo_buffer[basesrc+2+poffset]+cairo_buffer[basesrc+6+poffset])>>1)*aR + (usptr[basedst+0+poffset]>>8)*(256-aR));
												}
											}
											else
											{
												if(a == 255 && opacity == 256)
												{
													sptr[basedst+2] = ((cairo_buffer[basesrc+0+poffset]+cairo_buffer[basesrc+4+poffset])<<(whitepoint-9));
													sptr[basedst+1] = ((cairo_buffer[basesrc+1+poffset]+cairo_buffer[basesrc+5+poffset])<<(whitepoint-9));
													sptr[basedst+0] = ((cairo_buffer[basesrc+2+poffset]+cairo_buffer[basesrc+6+poffset])<<(whitepoint-9));
												}
												else
												{
													int aR = ((a+1) * oR)>>8;
													int aG = ((a+1) * oG)>>8;
													int aB = ((a+1) * oB)>>8;
													sptr[basedst+2] = (((cairo_buffer[basesrc+0+poffset]+cairo_buffer[basesrc+4+poffset])>>1)*aB + (sptr[basedst+2+poffset]>>(whitepoint-8))*(256-aB))>>(16-whitepoint);
													sptr[basedst+1] = (((cairo_buffer[basesrc+1+poffset]+cairo_buffer[basesrc+5+poffset])>>1)*aG + (sptr[basedst+1+poffset]>>(whitepoint-8))*(256-aG))>>(16-whitepoint);
													sptr[basedst+0] = (((cairo_buffer[basesrc+2+poffset]+cairo_buffer[basesrc+6+poffset])>>1)*aR + (sptr[basedst+0+poffset]>>(whitepoint-8))*(256-aR))>>(16-whitepoint);
												}
											}
										}
										basesrc += 8;
										basedst += skip;
									}
								}								
								else if(pixelsY == 2)
								{
									for(x=xmin/pixelsX; x<xmax/pixelsX; x++)
									{
										a = (cairo_buffer[basesrc+3] + cairo_buffer[basesrc+3+cairopitch])>>1;
										if(a)
										{
											if(whitepoint == 16)
											{
												if(a == 255 && opacity == 256)
												{
													usptr[basedst+2] = ((cairo_buffer[basesrc+0]+cairo_buffer[basesrc+0+cairopitch])<<7);
													usptr[basedst+1] = ((cairo_buffer[basesrc+1]+cairo_buffer[basesrc+1+cairopitch])<<7);
													usptr[basedst+0] = ((cairo_buffer[basesrc+2]+cairo_buffer[basesrc+2+cairopitch])<<7);
												}
												else
												{
													int aR = ((a+1) * oR)>>8;
													int aG = ((a+1) * oG)>>8;
													int aB = ((a+1) * oB)>>8;
													usptr[basedst+2] = (((cairo_buffer[basesrc+0]+cairo_buffer[basesrc+0+cairopitch])>>1)*aB + (usptr[basedst+2]>>8)*(256-aB));
													usptr[basedst+1] = (((cairo_buffer[basesrc+1]+cairo_buffer[basesrc+1+cairopitch])>>1)*aG + (usptr[basedst+1]>>8)*(256-aG));
													usptr[basedst+0] = (((cairo_buffer[basesrc+2]+cairo_buffer[basesrc+2+cairopitch])>>1)*aR + (usptr[basedst+0]>>8)*(256-aR));
												}
											}
											else
											{
												if(a == 255 && opacity == 256)
												{
													sptr[basedst+2] = ((cairo_buffer[basesrc+0]+cairo_buffer[basesrc+0+cairopitch])<<(whitepoint-9));
													sptr[basedst+1] = ((cairo_buffer[basesrc+1]+cairo_buffer[basesrc+1+cairopitch])<<(whitepoint-9));
													sptr[basedst+0] = ((cairo_buffer[basesrc+2]+cairo_buffer[basesrc+2+cairopitch])<<(whitepoint-9));
												}
												else
												{
													int aR = ((a+1) * oR)>>8;
													int aG = ((a+1) * oG)>>8;
													int aB = ((a+1) * oB)>>8;
													sptr[basedst+2] = (((cairo_buffer[basesrc+0]+cairo_buffer[basesrc+0+cairopitch])>>1)*aB + (sptr[basedst+2]>>(whitepoint-8))*(256-aB))>>(16-whitepoint);
													sptr[basedst+1] = (((cairo_buffer[basesrc+1]+cairo_buffer[basesrc+1+cairopitch])>>1)*aG + (sptr[basedst+1]>>(whitepoint-8))*(256-aG))>>(16-whitepoint);
													sptr[basedst+0] = (((cairo_buffer[basesrc+2]+cairo_buffer[basesrc+2+cairopitch])>>1)*aR + (sptr[basedst+0]>>(whitepoint-8))*(256-aR))>>(16-whitepoint);
												}
											}
										}
										basesrc += 4;
										basedst += skip;
									}
								}
								else 
								{
									for(x=xmin/pixelsX; x<xmax/pixelsX; x++)
									{
										if((a = cairo_buffer[basesrc+3]))
										{
											if(whitepoint == 16)
											{
												if(a == 255 && opacity == 256)
												{
													usptr[basedst+2] = cairo_buffer[basesrc+0]<<8;
													usptr[basedst+1] = cairo_buffer[basesrc+1]<<8;
													usptr[basedst+0] = cairo_buffer[basesrc+2]<<8;
												}
												else
												{
													int aR = ((a+1) * oR)>>8;
													int aG = ((a+1) * oG)>>8;
													int aB = ((a+1) * oB)>>8;
													usptr[basedst+2] = (cairo_buffer[basesrc+0]*aB + (usptr[basedst+2]>>8)*(256-aB));
													usptr[basedst+1] = (cairo_buffer[basesrc+1]*aG + (usptr[basedst+1]>>8)*(256-aG));
													usptr[basedst+0] = (cairo_buffer[basesrc+2]*aR + (usptr[basedst+0]>>8)*(256-aR));
												}
											}
											else
											{
												if(a == 255 && opacity == 256)
												{
													sptr[basedst+2] = cairo_buffer[basesrc+0]<<(whitepoint-8);
													sptr[basedst+1] = cairo_buffer[basesrc+1]<<(whitepoint-8);
													sptr[basedst+0] = cairo_buffer[basesrc+2]<<(whitepoint-8);
												}
												else
												{
													int aR = ((a+1) * oR)>>8;
													int aG = ((a+1) * oG)>>8;
													int aB = ((a+1) * oB)>>8;
													sptr[basedst+2] = (cairo_buffer[basesrc+0]*aB + (sptr[basedst+2]>>(whitepoint-8))*(256-aB))>>(16-whitepoint);
													sptr[basedst+1] = (cairo_buffer[basesrc+1]*aG + (sptr[basedst+1]>>(whitepoint-8))*(256-aG))>>(16-whitepoint);
													sptr[basedst+0] = (cairo_buffer[basesrc+2]*aR + (sptr[basedst+0]>>(whitepoint-8))*(256-aR))>>(16-whitepoint);
												}
											}
										}
										basesrc += 4;
										basedst += skip;
									}
								}
							}
						}
					}
				}
			}
			break;
		// TODO: Quicktime formats
		case COLOR_FORMAT_R4FL:
		case COLOR_FORMAT_BGRA32:
		case COLOR_FORMAT_QT32:
		case COLOR_FORMAT_AYUV_QTR:
		case COLOR_FORMAT_UYVA_QT:
			assert(0);
#if (0 && DEBUG)
			fprintf(stderr,"(draw.c)DoDrawScreen: Unsupported QT pixel format %d\n",output_format);
#endif
			break;
		// TODO: AVID formats
		case COLOR_FORMAT_CbYCrY_16bit:
		case COLOR_FORMAT_CbYCrY_10bit_2_8:
		case COLOR_FORMAT_CbYCrY_16bit_2_14:
		case COLOR_FORMAT_CbYCrY_16bit_10_6:
			assert(0);
#if (0 && DEBUG)
			fprintf(stderr,"(draw.c)DoDrawScreen: Unsupported AVID pixel format %d\n",output_format);
#endif
			break;
			
		case COLOR_FORMAT_BYR2:
		case COLOR_FORMAT_BYR4:
			//do nothing
			break;

		default:
			assert(0);
#if (0 && DEBUG)
			fprintf(stderr,"(draw.c)DoDrawScreen: Unsupported pixel format %d\n",output_format);
#endif
			break;
			
	}
}


void DrawScreen(DECODER *decoder, uint8_t *output, int pitch, int output_format)
{
	uint8_t *cairo_buffer = NULL;
	CAIROSTUFF *CS = NULL;
	lpCairoLib	cairoLib;
	CAIROlib *cairo;
	int channel_decodes = decoder->channel_decodes;
	int channel_blend_type = decoder->channel_blend_type;
	int swapped_flag = decoder->channel_swapped_flags & FLAG3D_SWAPPED ? 1 : 0;
	int rightonly = decoder->channel_current;
	
	//fprintf(stderr,"Drawscreen loaded %d\n",decoder->cairo_loaded);

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS)
		return;

	if(!decoder->cairo_loaded && !CS->cairoless_buffer)
		return;

	cairo = &CS->cairo;
	if(decoder->cairo_loaded)
		cairo_buffer = (uint8_t *)cairo->image_surface_get_data(CS->surface);
	else if(CS->cairoless_buffer)
		cairo_buffer = CS->cairoless_buffer;

	if(channel_decodes == 1)
		DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 1, !rightonly, 256,256,256);
	else if(channel_decodes == 2)
	{
		switch(channel_blend_type)
		{
			
		case BLEND_NONE: //double high
			if(output_format == COLOR_FORMAT_RGB24 || output_format == COLOR_FORMAT_RGB32)
				swapped_flag = !swapped_flag;

			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 1, !swapped_flag, 256,256,256); //normally left
			output += pitch * (CS->decoder_h);
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 1, swapped_flag, 256,256,256); //normally right
			break;
		case BLEND_STACKED_ANAMORPHIC: //stacked
			if(output_format == COLOR_FORMAT_RGB24 || output_format == COLOR_FORMAT_RGB32)
				swapped_flag = !swapped_flag;

			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 2, !swapped_flag, 256,256,256); //normally left
			output += pitch * (CS->decoder_h/2);
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 2, swapped_flag, 256,256,256); //normally right
			break;
		case BLEND_SIDEBYSIDE_ANAMORPHIC: //side-by-side
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 2, 1, !swapped_flag, 256,256,256); //normally left
			output += pitch/2;
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 2, 1, swapped_flag, 256,256,256); //normally right
			break;
		case BLEND_FREEVIEW: //Free view side-by-side
			output += (CS->decoder_h/4)*pitch;
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 2, 2, !swapped_flag, 256,256,256); //normally left
			output += pitch/2;
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 2, 2, swapped_flag, 256,256,256); //normally right
			break;
		case BLEND_LINE_INTERLEAVED: //fields
			DoDrawScreen(decoder, output, pitch*2, output_format, cairo_buffer, 1, 2, !swapped_flag, 256,256,256); //normally left
			output += pitch;
			DoDrawScreen(decoder, output, pitch*2, output_format, cairo_buffer, 1, 2, swapped_flag, 256,256,256); //normally right
			break;
		case BLEND_ONION: //onion/diff
		case BLEND_DIFFERENCE: //onion/diff
		case BLEND_SPLITVIEW: //split view
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 1,  swapped_flag, 128,128,128); // left
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 1, !swapped_flag, 128,128,128); // right
			break;
			
		case BLEND_ANAGLYPH_RC: //cyan/red
		case BLEND_ANAGLYPH_RC_BW: //cyan/red
		case BLEND_ANAGLYPH_DUBOIS: //cyan/red (Dubois)
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 1,  swapped_flag, 0,256,256); // left
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 1, !swapped_flag, 256,0,0); // right
			break;

		case BLEND_ANAGLYPH_AB: //Amber/Blue
		case BLEND_ANAGLYPH_AB_BW: //Amber/Blue
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 1,  swapped_flag, 0,0,256); // left
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 1, !swapped_flag, 256,256,0); // right
			break;

		case BLEND_ANAGLYPH_GM: //Green/Mangeta
		case BLEND_ANAGLYPH_GM_BW: //Green/Mangeta
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 1,  swapped_flag, 256,0,256); // left
			DoDrawScreen(decoder, output, pitch, output_format, cairo_buffer, 1, 1, !swapped_flag, 0,256,0); // right
			break;
		}
	}
}

#if 0
void HistogramRender(DECODER *decoder, uint8_t *output, int pitch, int output_format, int x, int targetW, int targetH)
{
	int alpha = 100; //alpha doesn't work (Fast) is YUY2 is stored on the graphics card ram.
	int ypos=0,upos=1,vpos=3;
	CAIROSTUFF *CS = NULL;

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(!CS)
		return;

	if(!decoder->cairo_loaded && !CS->cairoless_buffer)
		return;

	// Skip this if Cairo is loaded.   Should not get here.
	switch(output_format)
	{
		case COLOR_FORMAT_UYVY:
			ypos=1,upos=0,vpos=2;
		case COLOR_FORMAT_YUYV:
			//alpha = 0;
			{
				float xpos = (float)x * 256.0/(float)targetW;
				int y,xx = xpos;
				int xr = (int)(xpos*64.0)&63;
				float r,g,b,p=0,step = 1.0/(float)targetH;
				uint8_t *bptr = output;
				int R,G,B,Y,U,V;

				if(xx==255) xr = 0;

				y=0;
				bptr +=  (CS->decoder_h-1)*pitch;
				bptr +=  16*2;
				bptr -=  pitch * 16;
				bptr +=  x * 2;
				bptr -=  pitch * y;

				r = (float)(decoder->histR[xx])/(float)(decoder->maxR);
				g = (float)(decoder->histG[xx])/(float)(decoder->maxG);
				b = (float)(decoder->histB[xx])/(float)(decoder->maxB);

				if(alpha)
				{
					for(; y<targetH; y++,p+=step)
					{
						int index;

						R = (p < b) ? (1) : 0;
						G = (p < g) ? (1) : 0;
						B = (p < r) ? (1) : 0;

						index = R<<2|G<<1|B;
						switch(index)
						{
							case 0:	Y = 16, U = 128, V = 128; break;
							case 1:	Y = 31, U = 239, V = 118; break;
							case 2:	Y =172, U =  41, V =  26; break;
							case 3:	Y =188, U = 151, V =  16; break;
							case 4:	Y = 62, U = 102, V = 239; break;
							case 5:	Y = 78, U = 213, V = 229; break;
							case 6:	Y =219, U =  15, V = 137; break;
							case 7:	Y =235, U = 128, V = 128; break;
						}


						if(x&1)
						{
							bptr[ypos] = (Y * (255-alpha) +  bptr[ypos] * alpha)>>8;
							bptr[upos] = (U * (255-alpha) +  bptr[upos] * alpha)>>8;
							bptr[vpos] = (V * (255-alpha) +  bptr[vpos] * alpha)>>8;
						}
						else
						{
							bptr[ypos] = (Y * (255-alpha) +  bptr[ypos] * alpha)>>8;
							bptr[upos] = (V * (255-alpha) +  bptr[upos] * alpha)>>8;
							bptr[vpos] = (U * (255-alpha) +  bptr[vpos] * alpha)>>8;
						}

						bptr-=pitch;
					}
				}
				else
				{
					for(; y<targetH; y++,p+=step)
					{
						int index;

						R = (p < b) ? (1) : 0;
						G = (p < g) ? (1) : 0;
						B = (p < r) ? (1) : 0;

						index = R<<2|G<<1|B;
						switch(index)
						{
							case 0:	Y = 16, U = 128, V = 128; break;
							case 1:	Y = 31, U = 239, V = 118; break;
							case 2:	Y =172, U =  41, V =  26; break;
							case 3:	Y =188, U = 151, V =  16; break;
							case 4:	Y = 62, U = 102, V = 239; break;
							case 5:	Y = 78, U = 213, V = 229; break;
							case 6:	Y =219, U =  15, V = 137; break;
							case 7:	Y =235, U = 128, V = 128; break;
						}

						if(x&1)
						{
							bptr[ypos] = Y;
							bptr[upos] = U;
							bptr[vpos] = V;
						}
						else
						{
							bptr[ypos] = Y;
							bptr[upos] = V;
							bptr[vpos] = U;
						}
						bptr-=pitch;
					}
				}
			}
			break;

		case COLOR_FORMAT_RGB24:
			{
				float xpos = (float)x * 256.0/(float)targetW;
				int y,xx = xpos;
				int xr = (int)(xpos*64.0)&63;
				float r,g,b,p=0,step = 1.0/(float)targetH;
				uint8_t *bptr = output;

				if(xx==255) xr = 0;

				y=0;
				bptr +=  16*3;
				bptr +=  pitch * 16;
				bptr +=  x * 3;
				bptr +=  pitch * y;

				r = (float)(decoder->histR[xx])/(float)(decoder->maxR);
				g = (float)(decoder->histG[xx])/(float)(decoder->maxG);
				b = (float)(decoder->histB[xx])/(float)(decoder->maxB);

				if(alpha)
				{
					for(; y<targetH; y++,p+=step)
					{
						bptr[0] = (p < b) ? (255-alpha) : bptr[0]*alpha/255;
						bptr[1] = (p < g) ? (255-alpha) : bptr[1]*alpha/255;
						bptr[2] = (p < r) ? (255-alpha) : bptr[2]*alpha/255;
						bptr+=pitch;
					}
				}
				else
				{
					for(; y<targetH; y++,p+=step)
					{
						bptr[0] = (p < b) ? 255 : 0;
						bptr[1] = (p < g) ? 255 : 0;
						bptr[2] = (p < r) ? 255 : 0;
						bptr+=pitch;
					}
				}
			}
			break;

		case COLOR_FORMAT_RGB32:
			{
				float xpos = (float)x * 256.0/(float)targetW;
				int y,xx = xpos;
				int xr = (int)(xpos*64.0)&63;
				float r,g,b,p=0,step = 1.0/(float)targetH;
				uint8_t *bptr = output;

				y=0;
				bptr +=  16*4;
				bptr +=  pitch * 16;
				bptr +=  x * 4;
				bptr +=  pitch * y;

				r = (float)(decoder->histR[xx])/(float)(decoder->maxR);
				g = (float)(decoder->histG[xx])/(float)(decoder->maxG);
				b = (float)(decoder->histB[xx])/(float)(decoder->maxB);

				if(alpha)
				{
					for(; y<targetH; y++,p+=step)
					{
						bptr[0] = (p < b) ? (255-alpha) : bptr[0]*alpha/255;
						bptr[1] = (p < g) ? (255-alpha) : bptr[1]*alpha/255;
						bptr[2] = (p < r) ? (255-alpha) : bptr[2]*alpha/255;
						bptr[3] = 255;
						bptr+=pitch;
					}
				}
				else
				{
					for(; y<targetH; y++,p+=step)
					{
						bptr[0] = (p < b) ? 255 : 0;
						bptr[1] = (p < g) ? 255 : 0;
						bptr[2] = (p < r) ? 255 : 0;
						bptr[3] = 255;
						bptr+=pitch;
					}
				}
		}
		break;
	}
}
#endif


void CopyDrawRegion(uint8_t *output, int pitch, int w, int h, int x1, int y1, int x2, int y2, int alpha, uint8_t *src)
{
	// erase
	uint8_t *bptr = output;
	int x,y;

	if(y1 < 0) y1 = 0;
	if(y2 < 0) y2 = 0;
	if(x1 < 0) x1 = 0;
	if(x2 < 0) x2 = 0;
	if(y1 > h) y1 = h;
	if(y2 > h) y2 = h;
	if(x1 > w) x1 = w;
	if(x2 > w) x2 = w;

	if(x2 > x1)
	{
		for(y=y1; y<y2; y++)
		{
			bptr = output;
			bptr +=  pitch * y;
			memcpy(&bptr[x1*4], src, (x2-x1+1)*4);
			src += (x2-x1+1)*4;
		}
	}
}


void EraseDrawRegion(uint8_t *output, int pitch, int w, int h, int x1, int y1, int x2, int y2, int alpha)
{
	// erase
	uint8_t *bptr = output;
	int x,y;

	if(y1 < 0) y1 = 0;
	if(y2 < 0) y2 = 0;
	if(x1 < 0) x1 = 0;
	if(x2 < 0) x2 = 0;
	if(y1 > h) y1 = h;
	if(y2 > h) y2 = h;
	if(x1 > w) x1 = w;
	if(x2 > w) x2 = w;

	for(y=y1; y<y2; y++)
	{
		bptr = output;
		bptr +=  pitch * y;
		for(x=x1; x<x2; x++)
		{
			bptr[x * 4 + 0] = 0;
			bptr[x * 4 + 1] = 0;
			bptr[x * 4 + 2] = 0;
			bptr[x * 4 + 3] = alpha;
		}
	}
}


void BorderDrawRegion(uint8_t *output, int pitch, int w, int h, int x1, int y1, int x2, int y2, int alpha)
{
	// draw border / grid
	uint8_t *bptr = output;
	int x,y;

	if(y1 < 0) y1 = 0;
	if(y2 < 0) y2 = 0;
	if(x1 < 0) x1 = 0;
	if(x2 < 0) x2 = 0;
	if(y1 > h) y1 = h;
	if(y2 > h) y2 = h;
	if(x1 > w) x1 = w;
	if(x2 > w) x2 = w;

	{
		bptr = output;
		bptr +=  pitch * y1;
		for(x=x1; x<=x2; x++)
		{
			bptr[x * 4 + 0] = 128;
			bptr[x * 4 + 1] = 128;
			bptr[x * 4 + 2] = 128;
			bptr[x * 4 + 3] = alpha;
		}
		bptr +=  pitch;
		for(x=x1; x<=x2; x++)
		{
			bptr[x * 4 + 0] = 128;
			bptr[x * 4 + 1] = 128;
			bptr[x * 4 + 2] = 128;
			bptr[x * 4 + 3] = alpha;
		}

		bptr = output;
		bptr +=  pitch * (y2-2);
		for(x=x1; x<=x2; x++)
		{
			bptr[x * 4 + 0] = 128;
			bptr[x * 4 + 1] = 128;
			bptr[x * 4 + 2] = 128;
			bptr[x * 4 + 3] = alpha;
		}
		bptr +=  pitch;
		for(x=x1; x<=x2; x++)
		{
			bptr[x * 4 + 0] = 128;
			bptr[x * 4 + 1] = 128;
			bptr[x * 4 + 2] = 128;
			bptr[x * 4 + 3] = alpha;
		}

		bptr = output;
		bptr +=  pitch * y1;
		for(y=y1; y<y2; y++)
		{
			bptr[x1 * 4 + 0] = 128;
			bptr[x1 * 4 + 1] = 128;
			bptr[x1 * 4 + 2] = 128;
			bptr[x1 * 4 + 3] = alpha;
			bptr[x1 * 4 + 4] = 128;
			bptr[x1 * 4 + 5] = 128;
			bptr[x1 * 4 + 6] = 128;
			bptr[x1 * 4 + 7] = alpha;

			bptr[x2 * 4 - 4] = 128;
			bptr[x2 * 4 - 3] = 128;
			bptr[x2 * 4 - 2] = 128;
			bptr[x2 * 4 - 1] = alpha;
			bptr[x2 * 4 + 0] = 128;
			bptr[x2 * 4 + 1] = 128;
			bptr[x2 * 4 + 2] = 128;
			bptr[x2 * 4 + 3] = alpha;

			bptr +=  pitch;
		}
	}
}


void HistogramCairoRender(DECODER *decoder, uint8_t *output, int pitch, int x1, int y1, int x2, int y2, int alpha)
{
	CAIROSTUFF *CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(CS == NULL) return;

	{
		int x,y;
		ToolsHandle *tools = decoder->tools;
		int scrw = CS->decoder_w;
		int scrh = CS->decoder_h;

		if(y1 < 0) y1 = 0;
		if(y2 < 0) y2 = 0;
		if(x1 < 0) x1 = 0;
		if(x2 < 0) x2 = 0;
		if(y1 > scrh) y1 = scrh;
		if(y2 > scrh) y2 = scrh;
		if(x1 > scrw) x1 = scrw;
		if(x2 > scrw) x2 = scrw;

		for(x=x1; x<x2; x++)
		{
			float xpos = (float)(x-x1) * 256.0/(float)(x2-x1);
			int xx = xpos;
			int xr = (int)(xpos*64.0)&63;
			float r,g,b,p=0,step = 1.0/(float)(y2-y1);
			uint8_t *bptr = output;
			bptr +=  pitch * (y2-1);
			bptr +=  x * 4;
		
			r = (float)(tools->histR[xx])/(float)(tools->maxR);
			g = (float)(tools->histG[xx])/(float)(tools->maxG);
			b = (float)(tools->histB[xx])/(float)(tools->maxB);

			for(y=y2-1; y>=y1 && y>=0; y--,p+=step)
			{
				bptr[0] = (p < b) ? 255 : 0;
				bptr[1] = (p < g) ? 255 : 0;
				bptr[2] = (p < r) ? 255 : 0;
				bptr[3] = alpha;
				bptr-=pitch;
			}
		}
	}
}


void WaveformCairoRender(DECODER *decoder, uint8_t *output, int pitch, int x1, int y1, int x2, int y2, int alpha)
{
	CAIROSTUFF *CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(CS == NULL) return;

	{
		int x,y,i;
		int xa,xb;
		uint8_t *bptr = output;
		uint32_t *lptr;
		ToolsHandle *tools = decoder->tools;
		int scrw = decoder->frame.width;
		int scrh = CS->decoder_h;	
		float precentlinesCGRGB[] = {0.0,20.0,40.0,60.0,80.0,100.0};
		float precentlinesVSRGB[] = {6.0,23.5,40.6,57.8,75.0,93.0};
		float *precentlines;

		uint32_t lineweight = (alpha<<24)|0x808080;
		uint32_t halfweight = (alpha<<24)|0x404040;
		uint32_t qurtweight = (alpha<<24)|0x202020;

		if(y1 < 0) y1 = 0;
		if(y2 < 0) y2 = 0;
		if(x1 < 0) x1 = 0;
		if(x2 < 0) x2 = 0;
		if(y1 > scrh) y1 = scrh;
		if(y2 > scrh) y2 = scrh;
		if(x1 > scrw) x1 = scrw;
		if(x2 > scrw) x2 = scrw;

		if(decoder->frame.colorspace & COLOR_SPACE_VS_RGB)
			precentlines = precentlinesVSRGB;
		else
			precentlines = precentlinesCGRGB;



		EraseDrawRegion(output, pitch, scrw, scrh, x1, y1, x2, y2, alpha);
		BorderDrawRegion(output, pitch, scrw, scrh, x1, y1, x2, y2, alpha);

		for(i=0;i<6;i++)
		{
			int gain = 0;
			if(i==0 || i==5)
				gain = 1;
			if(precentlines[i] > 2.0 && precentlines[i] < 98.0)
			{
				bptr = output;
				bptr +=  pitch * (y1+(int)((float)(y2-y1)*precentlines[i]/100.0));
				lptr = (uint32_t *)bptr;
				for(x=x1+2; x<=x2-2; x++) lptr[x] = halfweight + gain*0x202020;
				lptr -= pitch/4;
				for(x=x1+2; x<=x2-2; x++) lptr[x] = qurtweight + gain*0x202020;
				lptr += pitch/2;
				for(x=x1+2; x<=x2-2; x++) lptr[x] = qurtweight + gain*0x202020;
			}
		}

		x1+=2;
		x2-=2;
		y1+=2;
		y2-=2;

		
		xa = ((x2-x1)/3)+x1;
		xb = ((x2-x1)*2/3)+x1;

		for(y=y1; y<=y2; y++)
		{
			float ypos2 = 255.0 - (float)((y-y1) * 255.0)/(float)(y2-y1);
			float ypos = 255.0 - (float)((y+1-y1) * 255.0)/(float)(y2-y1);
			float xpos = 0;
			int yy1 = ypos;
			int yy2 = ypos2+0.5;
			uint8_t *bptr = output;
			bptr +=  pitch * y;

			if(ypos > 255) ypos = 255;
			if(ypos < 0) ypos = 0;
			if(yy2 > 255) yy2 = 255;
			if(yy2 < 0) yy2 = 0;

			for(x=x1,xpos=0; x<xa; x++)
			{
				float xpos2 = (float)(x+1-x1) * (float)(tools->waveformWidth)/(float)(xa-x1);
				int xx1 = xpos;
				int xx2 = xpos2+0.5;
				int val = 0;

				
				for(yy1 = ypos; yy1<=yy2; yy1++)
					for(xx1 = xpos; xx1<=xx2; xx1++)
						val += tools->waveR[xx1][yy1];

				val *= 32;
				val /= (yy2-ypos+1+xx2-xpos+1); 
				if(val > 255) val = 255; 

				//if(val > bptr[x * 4 + 2])
				{
					if((bptr[x * 4 + 0] + val/2)<256)	bptr[x * 4 + 0] += val/2; else bptr[x * 4 + 0] = 255; //blu
					if((bptr[x * 4 + 1] + val/2)<256)	bptr[x * 4 + 1] += val/2; else bptr[x * 4 + 1] = 255; //grn
					if((bptr[x * 4 + 2] + val)<256)		bptr[x * 4 + 2] += val; else bptr[x * 4 + 2] = 255; //red
				}
				//bptr[x * 4 + 3] = alpha;

				xpos = xpos2;
			}

			for(x=xa,xpos=0; x<xb; x++)
			{
				float xpos2 = (float)(x+1-xa) * (float)(tools->waveformWidth)/(float)(xb-xa);
				int xx1 = xpos;
				int xx2 = xpos2+0.5;
				int val = 0;

				
				for(yy1 = ypos; yy1<=yy2; yy1++)
					for(xx1 = xpos; xx1<=xx2; xx1++)
						val += tools->waveG[xx1][yy1];

				val *= 32;
				val /= (yy2-ypos+1+xx2-xpos+1); 
				if(val > 255) val = 255; 

				//if(val > bptr[x * 4 + 1])
				{
					if((bptr[x * 4 + 0] + val/2)<256)	bptr[x * 4 + 0] += val/2; else bptr[x * 4 + 0] = 255; //blu
					if((bptr[x * 4 + 1] + val)<256)		bptr[x * 4 + 1] += val; else bptr[x * 4 + 1] = 255; //grn
					if((bptr[x * 4 + 2] + val/2)<256)	bptr[x * 4 + 2] += val/2; else bptr[x * 4 + 2] = 255; //red
				}
				//bptr[x * 4 + 3] = alpha;
				xpos = xpos2;
			}

			
			for(x=xb,xpos=0; x<=x2; x++)
			{
				float xpos2 = (float)(x+1-xb) * (float)(tools->waveformWidth)/(float)(x2-xb);
				int xx1 = xpos;
				int xx2 = xpos2+0.5;
				int val = 0;
				
				for(yy1 = ypos; yy1<=yy2; yy1++)
					for(xx1 = xpos; xx1<=xx2; xx1++)
						val += tools->waveB[xx1][yy1];

				val *= 32;
				val /= (yy2-ypos+1+xx2-xpos+1); 
				if(val > 255) val = 255; 

				//if(val > bptr[x * 4 + 0])
				{
					if((bptr[x * 4 + 0] + val)<256)		bptr[x * 4 + 0] += val; else bptr[x * 4 + 0] = 255; //blu
					if((bptr[x * 4 + 1] + val/2)<256)	bptr[x * 4 + 1] += val/2; else bptr[x * 4 + 1] = 255; //grn
					if((bptr[x * 4 + 2] + val/2)<256)	bptr[x * 4 + 2] += val/2; else bptr[x * 4 + 2] = 255; //red
				}
				//bptr[x * 4 + 3] = alpha;
				xpos = xpos2;
			}
		}
	}
}





void GridCairoRender(DECODER *decoder, uint8_t *output, int pitch, int x1, int y1, int x2, int y2, int style, float size)
{
	CAIROSTUFF *CS = (CAIROSTUFF *)decoder->cairoHandle;
	if(CS == NULL) return;

	{
		int x,y;
		int xa,xb;
		uint8_t *bptr = output;
		uint32_t *lptr;
		ToolsHandle *tools = decoder->tools;
		int scrw = decoder->frame.width;
		int scrh = CS->decoder_h;	
		float step = (size * (float)scrw * 0.1);
		float ysteps = (float)(scrh-1) / step;
		float xsteps = (float)(scrw-1) / step;
		int niceline = 1;
		int i;
		uint32_t lineweight = 0xc0ffffff;
		uint32_t halfweight = 0x80ffffff;
		uint32_t qurtweight = 0x40ffffff;

		if(step < 32)
			niceline = 0;

		//fprintf(stderr,"GridCairoRender output %08x pitch %d width %d height %d x1: %d x2: %d y1: %d y2: %d style: %d size: %2.3f step: %2.3f\n",
		//		output, pitch, scrw,scrh,x1,x2,y1,y2,style, size, step);
		
		if(y1 < 0) y1 = 0;
		if(y2 < 0) y2 = 0;
		if(x1 < 0) x1 = 0;
		if(x2 < 0) x2 = 0;
		if(y1 > scrh) y1 = scrh;
		if(y2 > scrh) y2 = scrh;
		if(x1 > scrw) x1 = scrw;
		if(x2 > scrw) x2 = scrw;
		

		memset(output, 0, pitch * scrh); //CMD 2010.10.20
	//	EraseDrawRegion(output, pitch, scrw, scrh, x1, y1, x2, y2, alpha);
	//	BorderDrawRegion(output, pitch, scrw, scrh, x1, y1, x2, y2, alpha);

		if(style == 2 || style == 3)
		{
			float ypos = 0.0;
			for(i=1; i<ysteps; i++)
			{
				ypos += step;
				bptr = output;
				bptr +=  pitch * (int)ypos;
				lptr = (uint32_t *)bptr;
				if(niceline)
				{
					for(x=x1; x<x2; x++) lptr[x] = halfweight;
					lptr -= pitch/4;
					for(x=x1; x<x2; x++) lptr[x] = qurtweight;
					lptr += pitch/2;
					for(x=x1; x<x2; x++) lptr[x] = qurtweight;
				}
				else
				{
					for(x=x1; x<x2; x++) lptr[x] = halfweight;
				}
			}
		}

		if(style == 1 || style == 3)
		{
			//fprintf(stderr,"grid x: %f step: %2.1f\n",xsteps,step);
			
			for(y=y1; y<y2; y++)
			{
				float xpos = 0.0;
				bptr = output;
				bptr +=  pitch * y;
				lptr = (uint32_t *)bptr;
				
				if(niceline)
				{
					for(i=1; i<xsteps; i++)
					{
						xpos += step;
						lptr = (uint32_t *)bptr;
						lptr += (int)xpos;
						lptr[-1] = qurtweight;
						lptr[0] = halfweight;
						if( (int)xpos+1<scrw) 
						{
							lptr[1] = qurtweight;
						}
					}
				}
				else
				{
					for(i=1; i<xsteps; i++)
					{
						xpos += step;
						lptr = (uint32_t *)bptr;
						lptr += (int)xpos;
						lptr[0] = lineweight;
					}
				}
			}
		}
	}
}



void VectorscopeCairoRender(DECODER *decoder, uint8_t *output, int pitch, int x1, int y1, int x2, int y2, int colorline)
{
	int x,y;
	int xa,xb;
	uint8_t *bptr = output;
	ToolsHandle *tools = decoder->tools;
	CAIROSTUFF *CS = NULL;
	CAIROlib *cairo = NULL;
	cairo_t *cr = NULL;
	int scaledvectorscope = 0;

	CS = (CAIROSTUFF *)decoder->cairoHandle;
	
	if(CS)
	{
		int scrw = CS->decoder_w;
		int scrh = CS->decoder_h;

		cairo = &CS->cairo;
		if(decoder->vs_surface && (decoder->vs_surface_w != (x2-x1+1) || decoder->vs_surface_h != (y2-y1+1)))
		{
			cairo->surface_destroy((cairo_surface_t *)decoder->vs_surface);
			cairo->destroy((cairo_t *)decoder->vs_cr);

			decoder->vs_surface = NULL;
			decoder->vs_cr = NULL;
		}

		if(decoder->vs_surface == NULL)
		{
			decoder->vs_surface = (void *)cairo->image_surface_create(CAIRO_FORMAT_ARGB32, x2-x1+1, y2-y1+1);
			cr = decoder->vs_cr = (void *)cairo->create((cairo_surface_t *)decoder->vs_surface);
			decoder->vs_surface_w = (x2-x1+1);
			decoder->vs_surface_h = (y2-y1+1);

			{
#ifndef M_PI
// usually defined in math.h
#define M_PI	3.14159265
#endif
				int R,G,B,U,V,I,Q;
				double upos,vpos,angle;
				double linewidth = (double)decoder->vs_surface_w / 100.0;
				double xc = (double)decoder->vs_surface_w/2.0;
				double yc = (double)decoder->vs_surface_h/2.0;
				double radius = yc-1.0;
				double angle90 = 90.0  * (M_PI/180.0);  /* angles are specified */
				double angle180 = 180.0 * (M_PI/180.0);  /* in radians           */
				double angle270 = 270.0 * (M_PI/180.0);  /* in radians           */
				double angle10 = 10.0 * (M_PI/180.0);  /* in radians           */
				double angle2p5 = 2.5 * (M_PI/180.0);  /* in radians           */
				double angle33 = 33 * (M_PI/180.0);  /* in radians           */
				double pixalpha = 1.0;

				cairo->set_source_rgba (cr, 0.0, 0.0, 0.0, pixalpha);
				cairo->arc (cr, xc, yc, radius, 0.0, 2*M_PI);
				cairo->fill (cr);
				cairo->set_line_width (cr, linewidth);
				
				pixalpha *= 0.4;
				cairo->arc (cr, xc, yc, radius, 0.0, 2*M_PI);
				cairo->set_source_rgba (cr, 1, 1, 0.2, pixalpha);
				cairo->stroke (cr);

				/* draw helping lines */
				cairo->set_line_width (cr, linewidth);

				// center circle
				cairo->arc (cr, xc, yc, linewidth*1.5, 0, 2*M_PI);
				cairo->fill (cr);

				cairo->arc (cr, xc, yc, radius, 0.0, 0.0);
				cairo->line_to (cr, xc+radius/2.0, yc);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius, angle90, angle90);
				cairo->line_to (cr, xc, yc+radius/2.0);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius, angle180, angle180);
				cairo->line_to (cr, xc-radius/2.0, yc);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius, angle270, angle270);
				cairo->line_to (cr, xc, yc-radius/2.0);
				cairo->stroke (cr);


				//I = 0.596*R � 0.275*G � 0.321*B
				//Q = 0.212*R � 0.523*G + 0.311*B	

				cairo->set_line_width (cr, linewidth*0.75);

				cairo->arc (cr, xc, yc, radius*1.0, angle90-angle33, angle90-angle33);
				cairo->arc (cr, xc, yc, radius/6.0, angle90-angle33, angle90-angle33);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.0, angle270-angle33, angle270-angle33);
				cairo->arc (cr, xc, yc, radius/6.0, angle270-angle33, angle270-angle33);
				cairo->stroke (cr);

				cairo->arc (cr, xc, yc, radius*1.0, -angle33, -angle33);
				cairo->arc (cr, xc, yc, radius/6.0, -angle33, -angle33);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.0, angle180-angle33, angle180-angle33);
				cairo->arc (cr, xc, yc, radius/6.0, angle180-angle33, angle180-angle33);
				cairo->stroke (cr);

				pixalpha = 1.0;
				pixalpha *= 0.7;
				
				R = 192; G = 0; B = 0;
				if(colorline)
					cairo->set_source_rgba (cr, 1.0, 0.0, 0.0, pixalpha);
				if(scaledvectorscope)
				{
					U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
					V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
				}
				else
				{
					U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
					V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
				}
				upos = (double)(U * (x2-x1+1) >> 8);
				vpos = (double)((255-V) * (y2-y1+1) >> 8);
				radius = sqrt((upos-xc)*(upos-xc) + (vpos-yc)*(vpos-yc));
				angle = angle270 - atan((upos-xc)/(vpos-yc));
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle-angle2p5);
				cairo->arc (cr, xc, yc, radius*0.975, angle-angle2p5, angle+angle2p5);
				cairo->arc (cr, xc, yc, radius*1.025, angle+angle2p5, angle+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle+angle2p5);
				cairo->stroke (cr);
				
				cairo->arc (cr, xc, yc, radius*1.1, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*1.2, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.2, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*1.1, angle+angle10, angle+angle10);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.9, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*0.8, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.8, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*0.9, angle+angle10, angle+angle10);
				cairo->stroke (cr);
	
				R = 0; G = 192; B = 0;
				if(colorline)
					cairo->set_source_rgba (cr, 0.0, 1.0, 0.0, pixalpha);
				if(scaledvectorscope)
				{
					U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
					V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
				}
				else
				{
					U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
					V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
				}
				upos = (double)(U * (x2-x1+1) >> 8);
				vpos = (double)((255-V) * (y2-y1+1) >> 8);
				radius = sqrt((upos-xc)*(upos-xc) + (vpos-yc)*(vpos-yc));
				angle = angle90 - atan((upos-xc)/(vpos-yc));
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle-angle2p5);
				cairo->arc (cr, xc, yc, radius*0.975, angle-angle2p5, angle+angle2p5);
				cairo->arc (cr, xc, yc, radius*1.025, angle+angle2p5, angle+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle+angle2p5);
				cairo->stroke (cr);

				cairo->arc (cr, xc, yc, radius*1.1, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*1.2, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.2, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*1.1, angle+angle10, angle+angle10);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.9, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*0.8, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.8, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*0.9, angle+angle10, angle+angle10);
				cairo->stroke (cr);

				R = 0; G = 0; B = 192;
				if(colorline)
					cairo->set_source_rgba (cr, 0.2, 0.2, 1.0, pixalpha);
				if(scaledvectorscope)
				{
					U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
					V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
				}
				else
				{
					U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
					V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
				}
				upos = (double)(U * (x2-x1+1) >> 8);
				vpos = (double)((255-V) * (y2-y1+1) >> 8);
				radius = sqrt((upos-xc)*(upos-xc) + (vpos-yc)*(vpos-yc));
				angle = angle90 - atan((upos-xc)/(vpos-yc));
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle-angle2p5);
				cairo->arc (cr, xc, yc, radius*0.975, angle-angle2p5, angle+angle2p5);
				cairo->arc (cr, xc, yc, radius*1.025, angle+angle2p5, angle+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle+angle2p5);
				cairo->stroke (cr);
				
				cairo->arc (cr, xc, yc, radius*1.1, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*1.2, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.2, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*1.1, angle+angle10, angle+angle10);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.9, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*0.8, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.8, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*0.9, angle+angle10, angle+angle10);
				cairo->stroke (cr);

				R = 192; G = 192; B = 0;
				if(colorline)
					cairo->set_source_rgba (cr, 1.0, 1.0, 0.0, pixalpha);
				if(scaledvectorscope)
				{
					U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
					V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
				}
				else
				{
					U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
					V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
				}
				upos = (double)(U * (x2-x1+1) >> 8);
				vpos = (double)((255-V) * (y2-y1+1) >> 8);
				radius = sqrt((upos-xc)*(upos-xc) + (vpos-yc)*(vpos-yc));
				angle = angle270 - atan((upos-xc)/(vpos-yc));
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle-angle2p5);
				cairo->arc (cr, xc, yc, radius*0.975, angle-angle2p5, angle+angle2p5);
				cairo->arc (cr, xc, yc, radius*1.025, angle+angle2p5, angle+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle+angle2p5);
				cairo->stroke (cr);
				
				cairo->arc (cr, xc, yc, radius*1.1, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*1.2, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.2, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*1.1, angle+angle10, angle+angle10);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.9, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*0.8, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.8, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*0.9, angle+angle10, angle+angle10);
				cairo->stroke (cr);

				R = 192; G = 0; B = 192;
				if(colorline)
					cairo->set_source_rgba (cr, 1.0, 0.0, 1.0, pixalpha);
				if(scaledvectorscope)
				{
					U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
					V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
				}
				else
				{
					U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
					V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
				}
				upos = (double)(U * (x2-x1+1) >> 8);
				vpos = (double)((255-V) * (y2-y1+1) >> 8);
				radius = sqrt((upos-xc)*(upos-xc) + (vpos-yc)*(vpos-yc));
				angle = angle270 - atan((upos-xc)/(vpos-yc));
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle-angle2p5);
				cairo->arc (cr, xc, yc, radius*0.975, angle-angle2p5, angle+angle2p5);
				cairo->arc (cr, xc, yc, radius*1.025, angle+angle2p5, angle+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle+angle2p5);
				cairo->stroke (cr);
				
				cairo->arc (cr, xc, yc, radius*1.1, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*1.2, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.2, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*1.1, angle+angle10, angle+angle10);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.9, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*0.8, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.8, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*0.9, angle+angle10, angle+angle10);
				cairo->stroke (cr);

				R = 0; G = 192; B = 192;
				if(colorline)
					cairo->set_source_rgba (cr, 0.0, 1.0, 1.0, pixalpha);
				if(scaledvectorscope)
				{
					U = ((((-672* R) - (2249 * G) + (2920* B))>>13)) + 128;	//* 255.0/314.0
					V = ((((3758* R) - (3416 * G) - (343 * B))>>13)) + 128;	//* 255.0/244.0
				}
				else
				{
					U = ((((-827* R) - (2769 * G) + (3596* B))>>13)) + 128;	
					V = ((((3596* R) - (3269 * G) - (328 * B))>>13)) + 128;	
				}
				upos = (double)(U * (x2-x1+1) >> 8);
				vpos = (double)((255-V) * (y2-y1+1) >> 8);
				radius = sqrt((upos-xc)*(upos-xc) + (vpos-yc)*(vpos-yc));
				angle = angle90 - atan((upos-xc)/(vpos-yc));
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle-angle2p5);
				cairo->arc (cr, xc, yc, radius*0.975, angle-angle2p5, angle+angle2p5);
				cairo->arc (cr, xc, yc, radius*1.025, angle+angle2p5, angle+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.025, angle-angle2p5, angle+angle2p5);
				cairo->stroke (cr);
				
				cairo->arc (cr, xc, yc, radius*1.1, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*1.2, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*1.2, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*1.1, angle+angle10, angle+angle10);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.9, angle-angle10, angle-angle10);
				cairo->arc (cr, xc, yc, radius*0.8, angle-angle10, angle-angle10+angle2p5);
				cairo->stroke (cr);
				cairo->arc (cr, xc, yc, radius*0.8, angle+angle10-angle2p5, angle+angle10);
				cairo->arc (cr, xc, yc, radius*0.9, angle+angle10, angle+angle10);
				cairo->stroke (cr);



			}
		}

		if(decoder->vs_surface)
		{
			uint8_t *cairo_buffer;
			cairo_buffer = (uint8_t *)cairo->image_surface_get_data(decoder->vs_surface);

			CopyDrawRegion(output, pitch, scrw, scrh, x1, y1, x2, y2, 255, cairo_buffer);
		}


		x1+=2;
		x2-=2;
		y1+=2;
		y2-=2;


		if(y1 < 0) y1 = 0;
		if(y2 < 0) y2 = 0;
		if(x1 < 0) x1 = 0;
		if(x2 < 0) x2 = 0;
		if(y1 > scrh) y1 = scrh;
		if(y2 > scrh) y2 = scrh;
		if(x1 > scrw) x1 = scrw;
		if(x2 > scrw) x2 = scrw;

		//TODO
		//Draw circle
		//Draw R,G,B,Y,C,M targets
		//Draw flesh line

		{
			int u,v;
			if(decoder->tools->blurUVdone == 0)
			{
				decoder->tools->blurUVdone = 1;
				for(u = 1; u<255; u++)
				{
					for(v = 1; v<255; v++)
					{
						if(tools->scopeUV[u][v] > 255)
						{
							tools->scopeUV[u-1][v-1] += tools->scopeUV[u][v]>>4;
							tools->scopeUV[u+1][v-1] += tools->scopeUV[u][v]>>4;
							tools->scopeUV[u-1][v+1] += tools->scopeUV[u][v]>>4;
							tools->scopeUV[u+1][v+1] += tools->scopeUV[u][v]>>4;
							tools->scopeUV[u-1][v] += tools->scopeUV[u][v]>>3;
							tools->scopeUV[u][v-1] += tools->scopeUV[u][v]>>3;
							tools->scopeUV[u+1][v] += tools->scopeUV[u][v]>>3;
							tools->scopeUV[u][v+1] += tools->scopeUV[u][v]>>3;
						}
					}
				}
				for(u = 0; u<=255; u++)
				{
					for(v = 0; v<=255; v++)
					{
						if(tools->scopeUV[u][v] > 4)
						{
							//tools->scopeUV[u][v] = sqrt(tools->scopeUV[u][v])+4;
							if(tools->scopeUV[u][v] > 32)
							{
								tools->scopeUV[u][v] >>= 3;
								tools->scopeUV[u][v] += 4;
							}
							else if(tools->scopeUV[u][v] > 16)
							{
								tools->scopeUV[u][v] >>= 2;
								tools->scopeUV[u][v] += 4;
							}
							else 
							{
								tools->scopeUV[u][v] >>= 1;
								tools->scopeUV[u][v] += 4;
							}

						}
						tools->scopeUV[u][v] <<= 4;
					}
				}
			}
		}
				

		for(y=y1; y<y2; y++)
		{
			float vpos = (float)(y-y1) * 255.9/(float)(y2-y1+1);
			float vpos2 = (float)(y+1-y1) * 255.9/(float)(y2-y1+1);
			bptr = output;
			bptr +=  pitch * y;

			for(x=x1; x<=x2; x++)
			{
				int u,v,count=0;
				float upos = (float)(x-x1) * 255.9/(float)(x2-x1+1);
				float upos2 = (float)(x+1-x1) * 255.9/(float)(x2-x1+1);
				int val = 0;

				for(u = (int)upos; u<=(int)upos2; u++)
				{
					for(v = (int)vpos; v<=(int)vpos2; v++)
					{
						val += tools->scopeUV[u][255-v];
						count++;
					}
				}
				val /= count;
				if(val > 255) val = 255;

				{
					if((bptr[x * 4 + 0] + val)<256)	bptr[x * 4 + 0] += val; else bptr[x * 4 + 0] = 255; //blu
					if((bptr[x * 4 + 1] + val)<256)	bptr[x * 4 + 1] += val; else bptr[x * 4 + 1] = 255; //grn
					if((bptr[x * 4 + 2] + val)<256)	bptr[x * 4 + 2] += val; else bptr[x * 4 + 2] = 255; //red
				}
				//bptr[x * 4 + 3] = alpha;

				//U = U2;
			}
		}
	}
}

#endif


// Routines that are common to all graphics implementations

void GetDisplayParameters(DECODER *decoder, unsigned char *ptr, int len)
{

	if(decoder && ptr && len) // overrides form database or external control
	{
		int inframe = 0, duration = 0;
		unsigned char *base = ptr;
		void *data;
		unsigned char type;
		unsigned int pos = 0, size, copysize;
		unsigned int tag;
		//void *metadatastart = data;
		float tmp, lastxy[16][2], newxypos[2] = {-1,-1};
		int terminate = 0;

		memcpy(&lastxy[0][0], &decoder->MDPcurrent.xypos[0][0], 16*2*4);
		memcpy(&decoder->MDPcurrent, &decoder->MDPdefault, sizeof(decoder->MDPcurrent));
		memcpy(&decoder->MDPcurrent.xypos[0][0], &lastxy[0][0], 16*2*4);
		decoder->MDPcurrent.display_opacity = 1.0;
		decoder->MDPcurrent.inframe = 0;
		decoder->MDPcurrent.outframe = 0;
		decoder->MDPcurrent.fadeinframes = 0;
		decoder->MDPcurrent.fadeoutframes = 0;

		while(pos+12 <= len && !terminate)
		{
			data = (void *)&ptr[8];
			type = ptr[7];
			size = ptr[4] + (ptr[5]<<8) + (ptr[6]<<16);
			tag = MAKETAG(ptr[0],ptr[1],ptr[2],ptr[3]);

			switch(tag)
			{
			case 0:
				terminate = 1;
				break;

			case TAG_DISPLAY_SCRIPT:	
			case TAG_DISPLAY_SCRIPT_FILE:
				break;
			case TAG_DISPLAY_TAG:
				decoder->MDPcurrent.tag = *((uint32_t *)data);
				decoder->MDPcurrent.freeform[0] = 0;
				break;
			case TAG_DISPLAY_FREEFORM:
				copysize = size;
				if(copysize >= FREEFORM_STR_MAXSIZE) copysize = FREEFORM_STR_MAXSIZE-1;
				strncpy(decoder->MDPcurrent.freeform, (char *)data, copysize);
				decoder->MDPcurrent.freeform[copysize] = 0;
				decoder->MDPcurrent.tag = 0;
				break;
			case TAG_DISPLAY_FONT:
				copysize = size;
				if(copysize >= FONTNAME_STR_MAXSIZE) copysize = FONTNAME_STR_MAXSIZE-1;
				strncpy(decoder->MDPcurrent.font, (char *)data, copysize);
				decoder->MDPcurrent.font[copysize] = 0;
				break;
			case TAG_DISPLAY_FONTSIZE:
				decoder->MDPcurrent.fontsize = *((float *)data);
				break;
			case TAG_DISPLAY_JUSTIFY:
				decoder->MDPcurrent.justication = *((uint32_t *)data);
				break;
			case TAG_DISPLAY_FCOLOR:
				memcpy(&decoder->MDPcurrent.fcolor[0], data, sizeof(float)*4); 
				break;
			case TAG_DISPLAY_BCOLOR:
				memcpy(&decoder->MDPcurrent.bcolor[0], data, sizeof(float)*4); 
				break;
			case TAG_DISPLAY_SCOLOR:
				memcpy(&decoder->MDPcurrent.scolor[0], data, sizeof(float)*4); 
				break;
			case TAG_DISPLAY_STROKE_WIDTH:
				decoder->MDPcurrent.stroke_width = *((float *)data);
				break;
			case TAG_DISPLAY_XPOS:
				newxypos[0] = *((float *)data);
				break;
			case TAG_DISPLAY_YPOS:
				newxypos[1] = *((float *)data);
				break;
			case TAG_DISPLAY_XYPOS:
				memcpy(&newxypos[0], data, sizeof(float)*2); 
				break;
			case TAG_DISPLAY_FORMAT:
				copysize = size;
				if(copysize >= FORMAT_STR_MAXSIZE) copysize = FORMAT_STR_MAXSIZE-1;
				strncpy(decoder->MDPcurrent.format_str, (char *)data, copysize);
				decoder->MDPcurrent.format_str[copysize] = 0;
				break;
			case TAG_DISPLAY_PNG_PATH:
				copysize = size;
				if(copysize >= PNG_PATH_MAXSIZE) copysize = PNG_PATH_MAXSIZE-1;
				strncpy(decoder->MDPcurrent.png_path, (char *)data, copysize);
				decoder->MDPcurrent.png_path[copysize] = 0;
				break;
			case TAG_DISPLAY_PNG_SIZE:
				memcpy(&decoder->MDPcurrent.object_scale[0], data, sizeof(float)*2); 
				break;
			case TAG_DISPLAY_PARALLAX:
				decoder->MDPcurrent.parallax = *((uint32_t *)data);
				break;
			case TAG_DISPLAY_TIMING_IN:
				inframe = *((uint32_t *)data);
				decoder->MDPcurrent.inframe = inframe;
				if(decoder->codec.unique_framenumber < inframe)
				{
					decoder->MDPcurrent.tag = 0;
					decoder->MDPcurrent.freeform[0] = 0;
					decoder->MDPcurrent.format_str[0] = 0;
				}
				break;
			case TAG_DISPLAY_TIMING_DUR:
				duration = *((uint32_t *)data);
				decoder->MDPcurrent.outframe = inframe+duration;
				if(decoder->codec.unique_framenumber > inframe+duration)
				{
					decoder->MDPcurrent.tag = 0;
					decoder->MDPcurrent.freeform[0] = 0;
					decoder->MDPcurrent.format_str[0] = 0;
				}
				break;
			case TAG_DISPLAY_T_FADEIN:
				decoder->MDPcurrent.fadeinframes = *((uint32_t *)data);
				if(decoder->codec.unique_framenumber >= inframe && decoder->codec.unique_framenumber < inframe + decoder->MDPcurrent.fadeinframes)
				{
					float opacity = 1.0 - (float)(inframe + decoder->MDPcurrent.fadeinframes - decoder->codec.unique_framenumber) / (float)decoder->MDPcurrent.fadeinframes;
					if(opacity == 0.0) opacity = 0.0001;
					if(opacity < decoder->MDPcurrent.display_opacity)
						decoder->MDPcurrent.display_opacity = opacity;
				}
				break;
			case TAG_DISPLAY_T_FADEOUT:
				decoder->MDPcurrent.fadeoutframes = *((uint32_t *)data);
				if(decoder->codec.unique_framenumber <= decoder->MDPcurrent.outframe && decoder->codec.unique_framenumber > decoder->MDPcurrent.outframe - decoder->MDPcurrent.fadeoutframes)
				{
					float opacity = 1.0 - (float)(decoder->codec.unique_framenumber - (decoder->MDPcurrent.outframe - decoder->MDPcurrent.fadeoutframes)) / (float)decoder->MDPcurrent.fadeoutframes;
					if(opacity == 0.0) opacity = 0.0001;
					if(opacity < decoder->MDPcurrent.display_opacity)
						decoder->MDPcurrent.display_opacity = opacity;
				}
				break;
			}

			if(!terminate)
			{
				ptr += (8 + size + 3) & 0xfffffc;
				pos += (8 + size + 3) & 0xfffffc;
			}
		}

		if(newxypos[0] != -1)
			decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][0] = newxypos[0];
		if(newxypos[1] != -1)
			decoder->MDPcurrent.xypos[decoder->MDPcurrent.justication][1] = newxypos[1];
	}
}

#endif

#endif
