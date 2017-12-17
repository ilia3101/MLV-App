#ifndef _avencoder_h_
#define _avencoder_h_

/* The AVEncoder object used by AVFoundation Lib */

typedef struct
{
    int currently_writing; /* Currently writing?????? */

    int width;
    int height;
    CGSize size;
    double fps;
    int frames_encoded;

    CGColorSpaceRef colour_space;
    CFDataRef colour_profile_data;
    NSDictionary * colour_attachment; /* To attach to the CVPixelBuffer or whatever */

    uint16_t * encode_buffer; /* copied from data in to a ARGB64 arrangement */

    double write_progress; /* Check to see progress in percent */
    BOOL keep_writing; /* Flag to stop writing if needed */
    
    AVAssetWriter * video_writer; /* Asset writer */
    AVAssetWriterInput * video_writer_input; /* Whatever this does */
    AVAssetWriterInputPixelBufferAdaptor * adaptor;
    uint32_t bitrate; /* H.264 bitrate in Kb/s (?) */
    NSString * codec;
    /* Some 'codec keys':
     * H.264: https://developer.apple.com/documentation/avfoundation/avvideocodech264?language=objc
     * H.265 / HEVC: https://developer.apple.com/documentation/avfoundation/avvideocodechevc?language=objc
     * ProRes 422: https://developer.apple.com/documentation/avfoundation/avvideocodecappleprores422?language=objc
     * ProRes 4444: https://developer.apple.com/documentation/avfoundation/avvideocodecappleprores4444?language=objc */

} AVEncoder_t;

#endif