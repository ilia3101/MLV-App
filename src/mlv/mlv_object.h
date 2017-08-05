#ifndef _video_object_
#define _video_object_

/* For MLV headers */
#include "mlv.h"
/* TO have processingObject_t */
#include "../processing/processing_object.h"

/* I guess this has to happen for pthread_t */
#include "pthread.h"


/* An awkward structure for handling an MLV
 * Would be difficult to adapt for .RAW 
 * ToDo: adapt for .M00 .M01 stuff */
typedef struct {

    /* Will get used when adapted to handle .M00 .M01 */
    int filenum;
    int block_num; /* How many file blocks in MLV file */

    /* 0=no, 1=yes, mlv file open */
    int is_active;

    /* MLV/Lite file(s) */
    FILE * file;

    /* For access to MLV headers */
    mlv_file_hdr_t    MLVI;
    mlv_audf_hdr_t    AUDF;
    mlv_rawi_hdr_t    RAWI;
    mlv_vidf_hdr_t    VIDF; /* One of many VIDFs(don't know if they're different) */
    mlv_wavi_hdr_t    WAVI;
    mlv_expo_hdr_t    EXPO;
    mlv_lens_hdr_t    LENS;
    mlv_rtci_hdr_t    RTCI;
    mlv_idnt_hdr_t    IDNT;
    mlv_info_hdr_t    INFO;
    mlv_diso_hdr_t    DISO;

    /* Useful info for using */
    double      real_frame_rate; /* ...Because framerate is not explicitly stored in the file */
    double      frame_rate; /* User may want to override it */
    uint32_t    frames; /* Number of frames */
    uint64_t  * frame_offsets; /* Offsets to the start of frames in the file, computed on opening file */
    uint32_t  * frame_sizes; /* Frame sizes - only exists if video is losslessly compressed */
    uint32_t    frame_size; /* NOT counting compression factor */


    /* Image processing object pointer (it is to be made separately) */
    processingObject_t * processing;


    /************************************************************
     *** CACHE AREA - used by getMlvProcessedFrame and things ***
     ************************************************************/

    /* 0 = no, 1 = (yes... cache thread is alive right now) */
    int is_caching;
    pthread_t cache_thread;

    /* Will be set to 1 for cache thread to stop (probably only by freeMlvObject) */
    int stop_caching;

    /* Decides whether or not AMaZE *has* to be used or not, normally disabled for smooth playback */
    int use_amaze;

    /* If a frame is currently being cached in the background this will indicate which frame index, so frame request can wait for it to finish */
    uint32_t currently_caching;

    /* Basically how much we can cache(can be set by MB or frames or bytes) */
    uint64_t cache_limit_bytes;
    uint64_t cache_limit_frames;
    uint64_t cache_limit_mb; /* How many MB of frames can be cached... 
     * Debayered frames are cached with 16 bit channel bitdepth (48bpp) */

    /* Not used, cache always starts at frame zero... for now */
    uint64_t cache_start_frame;

    uint8_t * cached_frames; /* Basically an array with as many elements as frames, 
     * for each frame: 0(false) = frame is cached, 1 or more(true) = frame is cached */
    uint16_t ** rgb_raw_frames; /* Pointers to 16/48bpp debayered RGB frames */

    /* A single cached frame, speeds up when asking for the same (non-cached) frame over and over again */
    int current_cached_frame_active;
    uint64_t current_cached_frame; int times_requested;
    uint16_t * rgb_raw_current_frame;

    /* Massive block of memory for all frames that will be cached, pointers in rgb_raw_frames will point within here, 
     * using one big block block to try and avoid fragmentation (I feel that may be one of the causes of growth) */
    uint16_t * cache_memory_block;

    /* How many cores, will not neccesarily determine number of threads made in any case, but helps */
    int cpu_cores; /* Default 4 */


} mlvObject_t;

#endif
