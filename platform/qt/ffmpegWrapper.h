/*
    ensures extern "C" for ffmpeg
*/

#ifndef FFMPEGWRAPPER_H
#define FFMPEGWRAPPER_H

#include <stdint.h>

extern "C" {
#include "/usr/local/include/libavcodec/avcodec.h"
//#include "/usr/local/include/libavformat/avformat.h"
//#include "/usr/local/include/libswscale/swscale.h"
//#include "/usr/local/include/libavutil/avutil.h"
}

#endif // FFMPEGWRAPPER_H
