#ifndef _macros_h_
#define _macros_h_

/* mlvObject_t */
#include "mlv_object.h"

#define getMlvRawCacheLimitMegaBytes(video) (video)->cache_limit_mb
#define getMlvRawCacheLimitFrames(video) (video)->cache_limit_frames
#define isMlvObjectCaching(video) (video)->cache_thread_count
/* And here's an UNUSED (at this moment) macrofuntion - ignored */
#define setMlvCacheStartFrame(video, startFrame) (video)->cache_start_frame = (startFrame)

/* Do something like this before doing things: if (isMlvActive(your_mlvObject)) */
#define isMlvActive(video) (video)->is_active

/* Useful getting macros */
#define getMlvWidth(video) (video)->RAWI.xRes
#define getMlvHeight(video) (video)->RAWI.yRes
#define getMlvMaxWidth(video) ((video)->RAWI.raw_info.active_area.x2 - (video)->RAWI.raw_info.active_area.x1)
#define getMlvMaxHeight(video) ((video)->RAWI.raw_info.active_area.y2 - (video)->RAWI.raw_info.active_area.y1)
#define getMlvFrames(video) (video)->frames
#define getMlvBitdepth(video) (video)->RAWI.raw_info.bits_per_pixel
#define getMlvCompression(video) !((video)->MLVI.videoClass & MLV_VIDEO_CLASS_FLAG_LJ92) ? "Uncompressed" : "Lossless"
#define isMlvCompressed(video) ((video)->MLVI.videoClass & MLV_VIDEO_CLASS_FLAG_LJ92) ? 1 : 0
#define getMlvFramerate(video) (video)->frame_rate
#define getMlvFramerateOrig(video) (double)(video->MLVI.sourceFpsNom / (double)video->MLVI.sourceFpsDenom)
#define getMlvFrameNumber(video, frame_index) (video)->video_index[(frame_index)].frame_number
#define getMlvLens(video) (video)->LENS.lensName
#define getMlvCamera(video) (video)->IDNT.cameraName
#define getMlvCameraModel(video) (video)->IDNT.cameraModel
#define getMlvCameraSerial(video) (video)->IDNT.cameraSerial
#define getMlvLensSerial(video) (video)->LENS.lensSerial
#define getMlvCameraSerial(video) (video)->IDNT.cameraSerial
#define getMlvVersion(video) (video)->MLVI.versionString
#define getMlvBlackLevel(video) (video)->RAWI.raw_info.black_level
#define getMlvWhiteLevel(video) (video)->RAWI.raw_info.white_level
#define setMlvBlackLevel(video, blackLevel) (video)->RAWI.raw_info.black_level = (blackLevel)
#define setMlvWhiteLevel(video, whiteLevel) (video)->RAWI.raw_info.white_level = (whiteLevel)
#define getMlvOriginalBlackLevel(video) (video)->original_black_level
#define getMlvOriginalWhiteLevel(video) (video)->original_white_level
#define getMlvIso(video) (video)->EXPO.isoValue
#define getMlv2ndIso(video) (video)->DISO.isoValue
#define getMlvFocalLength(video) (video)->LENS.focalLength
#define getMlvShutter(video) (video)->EXPO.shutterValue
#define getMlvAperture(video) (video)->LENS.aperture
#define doesMlvHaveAudio(video) (((video)->MLVI.audioClass) && ((video)->audios) && ((video)->WAVI.channels != 0))
#define getMlvSampleRate(video) (video)->WAVI.samplingRate
#define getMlvAudioChannels(video) (video)->WAVI.channels
#define getMlvAudioBytesPerSecond(video) (video)->WAVI.bytesPerSecond
#define getMlvAudioBitsPerSample(video) (video)->WAVI.bitsPerSample
#define getMlvAudioData(video) (video)->audio_data
#define getMlvAudioSize(video) (video)->audio_size
#define getMlvTmYear(video)    ((video)->RTCI.tm_year+1900)
#define getMlvTmMonth(video)   ((video)->RTCI.tm_mon+1)
#define getMlvTmDay(video)     (video)->RTCI.tm_mday
#define getMlvTmHour(video)    (video)->RTCI.tm_hour
#define getMlvTmMin(video)     (video)->RTCI.tm_min
#define getMlvTmSec(video)     (video)->RTCI.tm_sec
#define getMlvWbMode(video)    (video)->WBAL.wb_mode
#define getMlvWbKelvin(video)  (video)->WBAL.kelvin
#define getMlvWbRgain(video)   (video)->WBAL.wbgain_r
#define getMlvWbGgain(video)   (video)->WBAL.wbgain_g
#define getMlvWbBgain(video)   (video)->WBAL.wbgain_b
#define getLosslessBpp(video)  (video)->lossless_bpp
#define getMlvFocalLengthMin(video) (video)->ELNS.focalLengthMin
#define getMlvFocalLengthMax(video) (video)->ELNS.focalLengthMax
#define getMlvApertureMin(video) (video)->ELNS.apertureMin
#define getMlvApertureMax(video) (video)->ELNS.apertureMax

/* Useful setting macros (functions) */

/* Set/reset framerate */
#define setMlvFramerateCustom(video, newFrameRate) (video)->frame_rate = (newFrameRate)
#define setMlvFramerateDefault(video) (video)->frame_rate = (video)->frame_rate_default

/* How many cores CPU has (defaultly set to 4 which works from laptop i5 up to big i7) */
#define setMlvCpuCores(video, cores) (video)->cpu_cores = (cores)
#define getMlvCpuCores(video) (video)->cpu_cores

/* Use setMlvAlwaysUseAmaze() to always get AMaZE frames, for best quality always */
#define setMlvAlwaysUseAmaze(video) (video)->use_amaze = 1; (video)->current_cached_frame_active = 0
/* Or this one for speed/ultimate playback performance, will give AMaZE if it is in cache, 
 * or bilinear if cached AMaZE frame is not avalible in cache */
#define setMlvDontAlwaysUseAmaze(video) (video)->use_amaze = 0; (video)->current_cached_frame_active = 0
/* Use the non debayer for best speed + no quality :-P */
#define setMlvUseNoneDebayer(video) (video)->use_amaze = 2; (video)->current_cached_frame_active = 0
/* Use the easy debayer for best speed + a little quality */
#define setMlvUseSimpleDebayer(video) (video)->use_amaze = 3; (video)->current_cached_frame_active = 0
/* Use the LMMSE debayer */
#define setMlvUseLmmseDebayer(video) (video)->use_amaze = 4; (video)->current_cached_frame_active = 0
/* Use the IGV debayer */
#define setMlvUseIgvDebayer(video) (video)->use_amaze = 5; (video)->current_cached_frame_active = 0
/* Use the AHD debayer */
#define setMlvUseAhdDebayer(video) (video)->use_amaze = 6; (video)->current_cached_frame_active = 0
/* Use the RCD debayer */
#define setMlvUseRcdDebayer(video) (video)->use_amaze = 7; (video)->current_cached_frame_active = 0

/* Set CA correction parameters */
#define setMlvCaCorrectionRed(video, value) (video)->ca_red = (value)
#define setMlvCaCorrectionBlue(video, value) (video)->ca_blue = (value)

/* Reset the current cached frame. Needed if a raw correction parameter changed */
#define resetMlvCachedFrame(video) (video)->current_cached_frame_active = 0

/* This is pretty much private */
#define doesMlvAlwaysUseAmaze(video) (video)->use_amaze
#define getMlvVideoClass(video) (video)->MLVI.videoClass

#endif
