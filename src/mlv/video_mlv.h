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

/* Goes through the files and records every frame's offset from
 * start of the file in the ->frame_offsets property (AUDFs also) */
void mapMlvFrames(mlvObject_t * video, uint64_t limit);

/* Frees all memory and closes file */
void freeMlvObject(mlvObject_t * video);

/* To enable and disable caching */
void disableMlvCaching(mlvObject_t * video);
void enableMlvCaching(mlvObject_t * video);
/* For setting how much can be cached - "MegaBytes" == MebiBytes (thanks dmilligan) */
void setMlvRawCacheLimitMegaBytes(mlvObject_t * video, uint64_t megaByteLimit);
void setMlvRawCacheLimitFrames(mlvObject_t * video, uint64_t frameLimit);
/* Useful maybe */
#define getMlvRawCacheLimitMegaBytes(video) (video)->cache_limit_mb
#define getMlvRawCacheLimitFrames(video) (video)->cache_limit_frames
#define isMlvObjectCaching(video) (video)->is_caching
/* And here's an UNUSED (at this moment) macrofuntion - ignored */
#define setMlvCacheStartFrame(video, startFrame) (video)->cache_start_frame = (startFrame)

/* Links processing settings() with an MLV object */
void setMlvProcessing(mlvObject_t * video, processingObject_t * processing);

/* Functions for getting processed MLV frames - uses the 'processing' module,
 * Avalible in 8 and 16 bit! Neither is faster as processing for both is done in 16 bit */
void getMlvProcessedFrame8(mlvObject_t * video, uint64_t frameIndex, uint8_t * outputFrame);
void getMlvProcessedFrame16(mlvObject_t * video, uint64_t frameIndex, uint16_t * outputFrame);

/* Unpacks the bits of a frame to get a bayer B&W image (without black level correction)
 * Needs memory to return to, sized: sizeof(float) * getMlvHeight(urvid) * getMlvWidth(urvid)
 * Output values will be in range 0-65535 (16 bit), float is only because AMAzE uses it */
void getMlvRawFrameFloat(mlvObject_t * video, uint64_t frameIndex, float * outputFrame);

/* Gets a debayered 16 bit frame - used in getMlvProcessedFrame8 and 16 (when that begins to exist) */
void getMlvRawFrameDebayered(mlvObject_t * video, uint64_t frameIndex, uint16_t * outputFrame);

/* For processing only, no use to average library user ;) Camera RGB -> sRGB */
void getMlvCameraTosRGBMatrix(mlvObject_t * video, double * outputMatrix); /* Still havent had any success here */

/* Writes the MLV's audio in WAVE format to a given file path */
void writeMlvAudioToWave(mlvObject_t * video, char * path);
/* When allocating memory for audio use this */
uint64_t getMlvAudioSize(mlvObject_t * video);
/* Gets all audio data 4 u */
void getMlvAudioData(mlvObject_t * video, int16_t * outputAudio);

/* Do something like this before doing things: if (isMlvActive(your_mlvObject)) */
#define isMlvActive(video) (video)->is_active

/* Useful getting macros */
#define getMlvWidth(video) (video)->RAWI.xRes
#define getMlvHeight(video) (video)->RAWI.yRes
#define getMlvFrames(video) (video)->frames
#define getMlvBitdepth(video) (video)->RAWI.raw_info.bits_per_pixel
#define getMlvVideoClass(video) (video)->MLVI.videoClass
#define getMlvFramerate(video) (video)->frame_rate
#define getMlvLens(video) (video)->LENS.lensName
#define getMlvCamera(video) (video)->IDNT.cameraName
#define getMlvLensSerial(video) (video)->LENS.lensName
#define getMlvCameraSerial(video) (video)->IDNT.cameraSerial
#define getMlvVersion(video) (video)->MLVI.versionString
#define getMlvBlackLevel(video) (video)->RAWI.raw_info.black_level
#define getMlvWhiteLevel(video) (video)->RAWI.raw_info.white_level
#define getMlvIso(video) (video)->EXPO.isoValue
#define getMlvShutter(video) (video)->EXPO.shutterValue
#define getMlvAperture(video) (video)->LENS.aperture
#define doesMlvHaveAudio(video) (((video)->MLVI.audioClass) && ((video)->WAVI.channels > 0)) //TODO: this is a temporary fix
#define getMlvSampleRate(video) (video)->WAVI.samplingRate
#define getMlvAudioChannels(video) (video)->WAVI.channels

/* Useful setting macros (functions) */

/* Set/reset framerate */
#define setMlvFramerateCustom(video, newFrameRate) (video)->frame_rate = (newFrameRate)
#define setMlvFramerateDefault(video) (video)->frame_rate = (video)->frame_rate_default

/* How many cores CPU has (defaultly set to 4 which works from laptop i5 up to big i7) */
#define setMlvCpuCores(video, cores) (video)->cpu_cores = (cores)
#define getMlvCpuCores(video) (video)->cpu_cores

/* Use setMlvAlwaysUseAmaze() to always get AMaZE frames, for best quality always */
#define setMlvAlwaysUseAmaze(video) (video)->use_amaze = 1
/* Or this one for speed/ultimate playback performance, will give AMaZE if it is in cache, 
 * or bilinear if cached AMaZE frame is not avalible in cache */
#define setMlvDontAlwaysUseAmaze(video) (video)->use_amaze = 0

/* This is pretty much private */
#define doesMlvAlwaysUseAmaze(video) (video)->use_amaze
#define getMlvVideoClass(video) (video)->MLVI.videoClass


/******************************** 
 ********* PRIVATE AREA *********
 ********************************/

void cache_mlv_frames(mlvObject_t * video);

/* Gets a debayered frame; how is it different from getMlvRawFrameDebayered?... it doesn't get it from cache ever
 * also you must allocate it some temporary memory because that's how it works and you shouldnt be looking anyway */
void get_mlv_raw_frame_debayered( mlvObject_t * video, 
                                  int frame_index, 
                                  float * temp_memory, 
                                  uint16_t * output_frame, 
                                  int debayer_type ); /* Debayer type: 0=bilinear 1=amaze */


#endif
