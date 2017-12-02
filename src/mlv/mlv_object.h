#ifndef _video_object_
#define _video_object_

/* For MLV headers */
#include "mlv.h"
/* TO have processingObject_t */
#include "../processing/processing_object.h"
#include "llrawproc/llrawproc_object.h"

/* I guess this has to happen for pthread_t */
#include "pthread.h"


/* cache states */
#define MLV_FRAME_NOT_CACHED 0
#define MLV_FRAME_IS_CACHED 1
#define MLV_FRAME_BEING_CACHED 2

typedef struct
{
    int chunk_num;           /* Number of MLV chunk */
    uint32_t frame_size;     /* Size of the raw video frame data */
    uint64_t frame_offset;   /* Offset to the start of raw frame data */
} frame_index_t;

/* An awkward structure for handling an MLV
 * TODO: adapt for .M00 .M01 stuff */
typedef struct {

    /* Will get used when adapted to handle .M00 .M01 */
    int filenum;
    int block_num; /* How many file blocks in MLV file */

    /* 0=no, 1=yes, mlv file open */
    int is_active;

    /* MLV/Lite file(s) */
    FILE ** file;
    char * path;
    pthread_mutex_t * main_file_mutex; /* One for each file */
    pthread_mutex_t g_mutexFind; /* 'g' mutexes should prevent pink frames */
    pthread_mutex_t g_mutexCount;

    /* For access to MLV headers */
    mlv_file_hdr_t    MLVI;
    mlv_audf_hdr_t    AUDF; /* Last AUDF header read */
    mlv_rawi_hdr_t    RAWI;
    mlv_rawc_hdr_t    RAWC;
    mlv_vidf_hdr_t    VIDF; /* One of many VIDFs(don't know if they're different) */
    mlv_wavi_hdr_t    WAVI;
    mlv_expo_hdr_t    EXPO;
    mlv_lens_hdr_t    LENS;
    mlv_wbal_hdr_t    WBAL;
    mlv_rtci_hdr_t    RTCI;
    mlv_idnt_hdr_t    IDNT;
    mlv_info_hdr_t    INFO;
    mlv_diso_hdr_t    DISO;

    /* Video info */
    double      real_frame_rate; /* ...Because framerate is not explicitly stored in the file */
    double      frame_rate;      /* User may want to override it */
    uint32_t    frames;          /* Number of frames */
    uint32_t    frame_size;      /* NOT counting compression factor */
    frame_index_t * frame_index;

    /* Audio info */
    uint32_t    audios;          /* Number of audio blocks */
    frame_index_t * audio_index;

    /* Image processing object pointer (it is to be made separately) */
    processingObject_t * processing;
    llrawprocObject_t * llrawproc;


    /************************************************************
     *** CACHE AREA - used by getMlvProcessedFrame and things ***
     ************************************************************/

    /* 0 = no, 1 = (yes... cache threads are alive right now) */
    int is_caching;
    int cache_thread_count; /* Total active cache threads */
    uint64_t cache_next; /* Like a cache request (any non-zero frame) */
    pthread_mutex_t cache_mutex;
    /* Will be set to 1 for cache threads to stop (probably only by freeMlvObject) */
    int stop_caching;


    /* Decides whether or not AMaZE *has* to be used or not, normally disabled for smooth playback */
    int use_amaze;

    /* Basically how much we can cache(can be set by MB or frames or bytes) */
    uint64_t cache_limit_bytes;
    uint64_t cache_limit_frames;
    uint64_t cache_limit_mb; /* How many MB of frames can be cached... 
     * Debayered frames are cached with 16 bit channel bitdepth (48bpp) */

    /* Not used, cache always starts at frame zero... for now */
    uint64_t cache_start_frame;

    uint8_t * cached_frames; /* Basically an array with as many elements as frames, cache states are defined above */
    uint16_t ** rgb_raw_frames; /* Pointers to 16bit cached RGB frames */

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
