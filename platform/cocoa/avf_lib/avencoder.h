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

    CGColorSpaceRef colour_space;
    CFDataRef colour_profile_data;
    NSDictionary * colour_attachment; /* To attach to the CVPixelBuffer or whatever */

    uint16_t * data; /* Image data is put here b4 encoding (3x16bit RGB only) */

    double write_progress; /* Check to see progress in percent */
    BOOL keep_writing; /* Flag to stop writing if needed */
    
    AVAssetWriter * assetWriter; /* Asset writer */
    AVAssetWriterInput * asset_writer_input; /* Whatever this does */
    uint32_t bitrate; /* H.264 bitrate in Kb/s */
    NSString * codec;
    /* Some 'codec keys':
     * H.264: https://developer.apple.com/documentation/avfoundation/avvideocodech264?language=objc
     * H.265 / HEVC: https://developer.apple.com/documentation/avfoundation/avvideocodechevc?language=objc
     * ProRes 422: https://developer.apple.com/documentation/avfoundation/avvideocodecappleprores422?language=objc
     * ProRes 4444: https://developer.apple.com/documentation/avfoundation/avvideocodecappleprores4444?language=objc */

    CMSampleBufferRef * image; /* Image for encoding */
    CMSampleBufferRef * audio; /* Audio for encoding */

} AVEncoder_t;

#endif