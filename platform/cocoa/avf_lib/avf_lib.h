#ifndef _avlib_h_
#define _avlib_h_

/* AVFoundation */
#import <AVFoundation/AVFoundation.h>
/* The AVEncoder object */
#include "avencoder.h"

/* This is a C wrapper for encoding ProRes and H.264 using macOS AVFoundation */

/* Encode a video using AVFoundation */
// int encodeClipWithAVFoundation( int width,
//                                 int height,
//                                 int frames,
//                                 double fps,
//                                 int codec,
//                                 uint16_t * (* frameRequestFunction)(uint32_t frameIndex), /* A frame request function */
//                                 /* Output Parameters */
//                                 double * progress, /* Outputs progress here */
//                                 int cancel /* Will alawys check this before continuing, true = cancel */ );

/* codec options */
#define AVF_CODEC_H264 0
#define AVF_CODEC_HEVC 1 /* H.265 */
#define AVF_CODEC_PRORES_422 2
#define AVF_CODEC_PRORES_4444 3

/* New AVEncoder object */
AVEncoder_t * initAVEncoder(int width, int height, int codec, double fps);

/* Set bitrate of H.264 and HEVC encoding in Kbit/s, no effect if using ProRes */
// void setBitRate(AVEncoder_t * encoder, int bitRate);

/* To begin/open a video file encoding session */
void beginWritingVideoFile(AVEncoder_t * encoder, char * path, double * progress);

// /* Add a 16bit RGB frame to file (will be exported at bitdepth of set format) */
// void addFrameToVideoFile(AVEncoder_t * encoder, uint16_t * frame);

// /* Once all frames are added to video file, this will end encoding session */
// void endWritingVideoFile(AVEncoder_t * encoder);

/* Done */
void freeAVEncoder(AVEncoder_t * encoder);

#endif