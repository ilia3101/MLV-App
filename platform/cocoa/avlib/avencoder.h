#ifndef _avencoder_h_
#define _avencoder_h_

/* The AVEncoder object used by AVFoundation Lib */

typedef struct {

    /* Video class */
    int video_class;
    /* Video configuration based on video_class */
    AVFileType av_file_type; /* https://developer.apple.com/documentation/avfoundation/avfiletype?language=objc */
    AVVideoCodecKey av_video_codec; /* https://developer.apple.com/documentation/avfoundation/avvideocodeckey?language=objc */
    /* Some 'codec keys':
     * H.264: https://developer.apple.com/documentation/avfoundation/avvideocodech264?language=objc
     * H.265 / HEVC: https://developer.apple.com/documentation/avfoundation/avvideocodechevc?language=objc
     * ProRes 422: https://developer.apple.com/documentation/avfoundation/avvideocodecappleprores422?language=objc
     * ProRes 4444: https://developer.apple.com/documentation/avfoundation/avvideocodecappleprores4444?language=objc */

    /* H.264 bitrate in Kb/s */
    uint32_t h264_bitrate;

    /* Asset writer */
    AVAssetWriter * assetWriter;
    AVAssetWriterInput asset_writer_input;

    /* Image for encoding */
    CMSampleBufferRef * image;
    /* Audio for encoding */
    CMSampleBufferRef * audio;

} AVEncoder_t;

#endif