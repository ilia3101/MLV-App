#ifndef _video_mlv_
#define _video_mlv_

#include "raw.h"
#include "mlv.h"
#include "../processing/raw_processing.h"

/* mlvObject_t */
#include "mlv_object.h"

/* mlvObject_t is (currently) single use only, as in it needs to be destoyed: freeMlvObject(), 
 * and another created: initMlvObjectWithClip(), if you want to work with another MLV video */

/* All functions in one */
mlvObject_t * initMlvObjectWithClip(char * mlvPath);

/* Initialises an MLV object. That's all you need to know */
mlvObject_t * initMlvObject();

/* Prints everything you'll ever need to know */
void printMlvInfo(mlvObject_t * video);

/* Reads an MLV file in to a video object(mlvObject_t struct) 
 * only puts frame indexes and metadata in to the mlvObject_t, 
 * no debayering or processing */
void openMlvClip(mlvObject_t * video, char * mlvPath);

/* Goes through the files and record's every frame's offset from
 * start of the file in the .frame_offsets property */
void mapMlvFrames(mlvObject_t * video, int limit);

/* Frees all memory and closes file */
void freeMlvObject(mlvObject_t * video);

/* Links processing settings() with an MLV object */
void setMlvProcessing(mlvObject_t * video, processingObject_t * processing);

/* Functions for getting processed MLV frames - uses the 'processing' module,
 * Avalible in 8 and 16 bit! Neither is faster as processing for both is done in 16 bit */
void getMlvProcessedFrame8(mlvObject_t * video, int frameIndex, uint8_t * outputFrame);
void getMlvProcessedFrame16(mlvObject_t * video, int frameIndex, uint16_t * outputFrame);

/* Unpacks the bits of a frame to get a bayer B&W image (without black level correction)
 * Needs memory to return to, sized: sizeof(float) * getMlvHeight(urvid) * getMlvWidth(urvid)
 * Output values will be in range 0-65535 (16 bit), float is only because AMAzE uses it */
void getMlvRawFrameFloat(mlvObject_t * video, int frameIndex, float * outputFrame);

/* Used for processing, gets the matrix that does camera -> XYZ */
void getMlvXyzToCameraMatrix(mlvObject_t * video, double * outputMatrix);
/* To get a nice/generic XYZ to RGB matrix */
void getMlvNiceXyzToRgbMatrix(mlvObject_t * video, double * outputMatrix);

/* Useful getting macros */
#define getMlvWidth(video) (video)->RAWI.xRes
#define getMlvHeight(video) (video)->RAWI.yRes
#define getMlvFrames(video) (video)->frames
#define getMlvBitdepth(video) (video)->RAWI.raw_info.bits_per_pixel
#define getMlvFramerate(video) (video)->frame_rate
#define getMlvLens(video) (video)->LENS.lensName
#define getMlvCamera(video) (video)->IDNT.cameraName
#define getMlvLensSerial(video) (video)->LENS.lensName
#define getMlvCameraSerial(video) (video)->IDNT.cameraSerial
#define getMlvVersion(video) (video)->MLVI.versionString
#define getMlvBlackLevel(video) (video)->RAWI.raw_info.black_level
#define getMlvWhiteLevel(video) (video)->RAWI.raw_info.white_level

/* Useful setting macros (functions) */

/* Set/reset framerate */
#define setMlvFramerateCustom(mlvObject, newFramerate) (mlvObject)->frame_rate = (newFramerate)
#define setMlvFramerateDefault(mlvObject) (mlvObject)->frame_rate = (mlvObject)->frame_rate_default

/* How many MB can be used to cache RAW images(debayered but unprocessed) */
#define setMlvCacheStartFrame(mlvObject, startFrame) (mlvObject)->cache_start_frame = (startFrame)
/* How many MegaBytes can be cached */
#define setMlvRawCacheLimit(mlvObject, megaByteLimit) (mlvObject)->cache_limit_mb = (megaByteLimit)

/* How many cores CPU has (defaultly set to 4 which works for laptop i5 to big i7) */
#define setMlvCpuCores(mlvObject, cores) (mlvObject)->cpu_cores = (cores)
#define getMlvCpuCores(mlvObject) (mlvObject)->cpu_cores


#endif