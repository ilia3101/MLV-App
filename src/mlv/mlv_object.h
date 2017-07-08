#ifndef _video_object_
#define _video_object_

/* For MLV headers */
#include "mlv.h"
/* TO have processingObject_t */
#include "../processing/processing_object.h"

/* An awkward structure for handling an MLV
 * Would be difficult to adapt for .RAW 
 * ToDo: adapt for .M00 .M01 stuff */
typedef struct {

    /* Will get used when adapted to handle .M00 .M01 */
    int filenum;
    int block_num; /* How many file blocks in MLV file */

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
    uint32_t  * frame_offsets; /* Offsets to the start of frames in the file, computed on opening file */


    /* Image processing object pointer (it is to be made separately) */
    processingObject_t * processing;


    /* Cache area - "PRIVATE", used by getMlvProcessedFrame and things (or will be...) */

    int cache_limit_mb; /* How many MB of frames can be cached... 
     * Debayered frames are cached with 16 bit channel bitdepth (48bpp) */
    int cache_start_frame;
    uint8_t * cached_frames; /* Basically an array with as many elements as frames, 
     * for each frame: 0(false) = frame is cached, 1 or more(true) = frame is cached */
    uint16_t ** rgb_raw_frames; /* Pointers to 16/48bpp debayered RGB frames */

    /* How many cores, will not neccesarily determine number of threads made in any case, but helps */
    int cpu_cores; /* Default 4 */


} mlvObject_t;

#endif