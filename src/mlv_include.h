/* All necessary includes for MLV reading and processing */
#ifndef _mlv_includes_
#define _mlv_includes_

#ifdef __cplusplus
extern "C" {
#endif

/* MLV reading part */
#include "mlv/video_mlv.h"
#include "mlv/audio_mlv.h"

/* RAW processing part */
#include "processing/raw_processing.h"
#include "debayer/debayer.h"
#include "mlv/llrawproc/llrawproc.h"
#include "dng/dng.h"

#ifdef __cplusplus
}
#endif

#endif
