#ifndef _video_mlv_
#define _video_mlv_

#include "raw.h"
#include "mlv.h"
#include "../processing/raw_processing.h"

/* mlvObject_t */
#include "mlv_object.h"

/* Usefull macros */
#include "macros.h"

/* mlvObject_t is (currently) single use only, as in it needs to be destoyed: freeMlvObject(), 
 * and another created: initMlvObjectWithClip(), if you want to work with another MLV video */

/* All functions in one */
mlvObject_t * initMlvObjectWithClip(char * mlvPath, int preview, int * err, char * error_message);

/* Initialises an MLV object. That's all you need to know */
mlvObject_t * initMlvObject();

/* Prints everything you'll ever need to know */
void printMlvInfo(mlvObject_t * video);

/* Reads an MLV file in to a video object(mlvObject_t struct)
 * only puts frame indexes and metadata in to the mlvObject_t, 
 * no debayering or processing */
int openMlvClip(mlvObject_t * video, char * mlvPath, int open_mode, char * error_message);
/* return error codes of and open modes of openMlvClip() */
enum mlv_err { MLV_ERR_NONE, MLV_ERR_OPEN, MLV_ERR_INVALID, MLV_ERR_IO };
enum open_mode { MLV_OPEN_FULL, MLV_OPEN_MAPP, MLV_OPEN_PREVIEW };

/* Functions for saving cut or averaged MLV */
int saveMlvHeaders(mlvObject_t * video, FILE * output_mlv, int export_audio, int export_mode, uint32_t frame_start, uint32_t frame_end, const char * version, char * error_message);
int saveMlvAVFrame(mlvObject_t * video, FILE * output_mlv, int export_audio, int export_mode, uint32_t frame_start, uint32_t frame_end, uint32_t frame_index, uint32_t * avg_buf, char * error_message);
enum export_mode { MLV_FAST_PASS, MLV_COMPRESSED, MLV_AVERAGED_FRAME, MLV_DF_INT };
/* from darkframe.c */
extern int df_init(mlvObject_t * video);

/* Frees all memory and closes file */
void freeMlvObject(mlvObject_t * video);

/* To enable and disable caching */
void disableMlvCaching(mlvObject_t * video);
void enableMlvCaching(mlvObject_t * video);
/* Reset cache, to recache all frames, clears simgle frame cache too */
void resetMlvCache(mlvObject_t * video);
/* For setting how much can be cached - "MegaBytes" == MebiBytes (thanks dmilligan) */
void setMlvRawCacheLimitMegaBytes(mlvObject_t * video, uint64_t megaByteLimit);
void setMlvRawCacheLimitFrames(mlvObject_t * video, uint64_t frameLimit);

/* Links processing settings() with an MLV object */
void setMlvProcessing(mlvObject_t * video, processingObject_t * processing);

/* Functions for getting processed MLV frames - uses the 'processing' module,
 * Avalible in 8 and 16 bit! Neither is faster as processing for both is done in 16 bit,
 * only use more than one thread in threads argument for speedier preview, not for export
 * as it may have minor artifacts (though I haven't found them yet) */
void getMlvProcessedFrame8(mlvObject_t * video, uint64_t frameIndex, uint8_t * outputFrame, int threads);
void getMlvProcessedFrame16(mlvObject_t * video, uint64_t frameIndex, uint16_t * outputFrame, int threads);

/* Unpacks the bits of a frame to get a bayer B&W image (without black level correction)
 * Needs memory to return to, sized: sizeof(float) * getMlvHeight(urvid) * getMlvWidth(urvid)
 * Output values will be in range 0-65535 (16 bit), float is only because AMAzE uses it */
int getMlvRawFrameUint16(mlvObject_t * video, uint64_t frameIndex, uint16_t * unpackedFrame);
void getMlvRawFrameFloat(mlvObject_t * video, uint64_t frameIndex, float * outputFrame);

/* Gets a debayered 16 bit frame - used in getMlvProcessedFrame8 and 16 (when that begins to exist) */
void getMlvRawFrameDebayered(mlvObject_t * video, uint64_t frameIndex, uint16_t * outputFrame);

/* For processing only, no use to average library user ;) Camera RGB -> sRGB */
void getMlvCameraTosRGBMatrix(mlvObject_t * video, double * outputMatrix); /* Still havent had any success here */

/* Gets image aspect ratio according to RAWC block info, calculating from binnin + skipping values */
float getMlvAspectRatio(mlvObject_t * video);

/******************************** 
 ********* PRIVATE AREA *********
 ********************************/

/* Add as many of these as you want :) */
void an_mlv_cache_thread(mlvObject_t * video);

/* Marks all frames as not cached */
void mark_mlv_uncached(mlvObject_t * video);

/* Clears cache by freeing then reallocating (RAM usage down until frames written) */
void clear_mlv_cache(mlvObject_t * video);

/* Returns 1 on success, or 0 if all are cached */
int find_mlv_frame_to_cache(mlvObject_t * video, uint64_t *index); /* Outputs to *index */

/* Adds one thread, active total can be checked in mlvObject->cache_thread_count */
void add_mlv_cache_thread(mlvObject_t * video);

/* OLD DEPRACTEDFSDJKHJKLAJSKDLJ KLSDJKL AJSD LKSAJDLKSAJDLK DKJS */
void cache_mlv_frames(mlvObject_t * video);

/* Gets a debayered frame; how is it different from getMlvRawFrameDebayered?... it doesn't get it from cache ever
 * also you must allocate it some temporary memory because that's how it works and you shouldnt be looking anyway */
void get_mlv_raw_frame_debayered( mlvObject_t * video, 
                                  int frame_index, 
                                  float * temp_memory, 
                                  uint16_t * output_frame, 
                                  int debayer_type ); /* Debayer type: 0=bilinear 1=amaze */


#endif
