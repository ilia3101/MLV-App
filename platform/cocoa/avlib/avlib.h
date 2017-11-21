#ifndef _avlib_h_
#define _avlib_h_

/* AVFoundation */
#import <AVFoundation/AVFoundation.h>
/* The AVEncoder object */
#include "encoder.h"

/* This is a C wrapper for encoding ProRes and H.264 using macOS AVFoundation */

/* New AVEncoder object */
AVEncoder_t * initAVEncoder(int videoWidth, int videoHeight, int videoClass);

/* videoClass options: */
#define VIDEO_H264 0
#define VIDEO_HEVC 1 /* H.265 */
#define VIDEO_PRORES_422 2
#define VIDEO_PRORES_4444 3

/* Set bitrate of H.264 and HEVC encoding in Kbit/s, no effect if using ProRes */
void setBitRate(AVEncoder_t * encoder, int bitRate);

/* To begin/open a video file encoding session */
void beginWritingVideoFile(AVEncoder_t * encoder, char * path);

/* Add a 16bit RGB frame to file (will be exported at bitdepth of set format) */
void addFrameToVideoFile(AVEncoder_t * encoder, uint16_t * frame);

/* Once all frames are added to video file, this will end encoding session */
void stopWritingVideoFile(AVEncoder_t * encoder);

/* Done */
void freeAVEncoder(AVEncoder_t * encoder);

#endif