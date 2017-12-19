#import "Cocoa/Cocoa.h"
#import <AVFoundation/AVFoundation.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#include "avf_lib.h"

/* Encode a video using AVFoundation */
AVEncoder_t * initAVEncoder(int width, int height, int codec, int colourSpace, double fps)
{
    AVEncoder_t * encoder = (AVEncoder_t *)calloc( sizeof(AVEncoder_t), 1 );

    encoder->fps = fps;
    encoder->width = width;
    encoder->height = height;
    encoder->size = CGSizeMake(width, height);
    encoder->encode_buffer = calloc(width * height * 4, sizeof(uint16_t)); /* RGBA64 */

    switch (codec)
    {
        case AVF_CODEC_PRORES_422:
            encoder->codec = AVVideoCodecAppleProRes422;
            break;
        case AVF_CODEC_PRORES_4444:
            encoder->codec = AVVideoCodecAppleProRes4444;
            break;
        case AVF_CODEC_H264:
            encoder->codec = AVVideoCodecH264;
            break;
/* OSX >= 10.13, @Ilia, you know how to check that?!
        case AVF_CODEC_HEVC:
            encoder->codec = AVVideoCodecHEVC;
            break;
*/
        default:
            encoder->codec = AVVideoCodecAppleProRes422;
            break;
    }

    CFStringRef colour_space_name;
    switch (colourSpace)
    {
        case AVF_COLOURSPACE_SRGB:
            colour_space_name = kCGColorSpaceSRGB;
            break;
        case AVF_COLOURSPACE_ADOBE_RGB:
            colour_space_name = kCGColorSpaceAdobeRGB1998;
            break;
            /* OSX >= 10.11, @Ilia, you know how to check that?! (compiles on ElCapitan, but not on Mavericks)
        case AVF_COLOURSPACE_DCIP3:
            colour_space_name = kCGColorSpaceDCIP3;
            break;
        case AVF_COLOURSPACE_REC_709:
            colour_space_name = kCGColorSpaceITUR_709;
            break;
        case AVF_COLOURSPACE_REC_2020:
            colour_space_name = kCGColorSpaceITUR_2020;
            break;*/
        default:
            colour_space_name = kCGColorSpaceSRGB;
            break;
    }
    encoder->colour_space = CGColorSpaceCreateWithName(colour_space_name);
    encoder->colour_profile_data = CGColorSpaceCopyICCProfile(encoder->colour_space);

    return encoder;
}

void freeAVEncoder(AVEncoder_t * encoder)
{
    CGColorSpaceRelease(encoder->colour_space);
    CFRelease(encoder->colour_profile_data);
    free(encoder->encode_buffer);
    free(encoder);
}

void beginWritingVideoFile(AVEncoder_t * encoder, char * path)
{
    if (encoder->currently_writing) return;
    encoder->currently_writing = 1;

    NSError *error = nil;

    NSURL * outURL = [NSURL fileURLWithPath:[NSString stringWithFormat: @"%s", path]];
    NSLog(@"%s", [outURL.absoluteString UTF8String]);

    CGSize imageSize = CGSizeMake(encoder->width, encoder->height);
    
    encoder->video_writer = [ [AVAssetWriter alloc] initWithURL: outURL
                              fileType:AVFileTypeQuickTimeMovie
                              error:&error ];
    
    NSDictionary * videoSettings = [ NSDictionary dictionaryWithObjectsAndKeys:
                                     encoder->codec, AVVideoCodecKey,
                                     [NSNumber numberWithInt:imageSize.width], AVVideoWidthKey,
                                     [NSNumber numberWithInt:imageSize.height], AVVideoHeightKey,
                                     nil ];
    
    encoder->video_writer_input = [ AVAssetWriterInput
                                    assetWriterInputWithMediaType:AVMediaTypeVideo
                                    outputSettings:videoSettings];
    
    
    encoder->adaptor = [ AVAssetWriterInputPixelBufferAdaptor
                         assetWriterInputPixelBufferAdaptorWithAssetWriterInput:encoder->video_writer_input
                         sourcePixelBufferAttributes:nil];

    encoder->video_writer_input.expectsMediaDataInRealTime = NO;
    [encoder->video_writer addInput:encoder->video_writer_input];

    [encoder->video_writer startWriting];
    [encoder->video_writer startSessionAtSourceTime:kCMTimeZero];
}

/* Append a frame in 16 bit */
void addFrameToVideoFile(AVEncoder_t * encoder, uint16_t * frame)
{
    CVPixelBufferRef buffer = NULL;

    /* Copy it to 'encode' buffer in ARGB64 format */
    uint8_t * original = (uint8_t *)frame;
    uint8_t * new = (uint8_t *)(encoder->encode_buffer + 1);
    int pixels = encoder->width * encoder->height;

    for (int i = 0; i < pixels; ++i, new+=8, original+=6)
    { /* Byteswap needed too!!!! WTF */
        /* Red */
        new[0] = original[1];
        new[1] = original[0];
        /* Green */
        new[2] = original[3];
        new[3] = original[2];
        /* Blue */
        new[4] = original[5];
        new[5] = original[4];
        /* Alpha */
        new[6] = 255;
        new[7] = 255;
    }

    /* Now arrange it as argb64 */
    CVReturn success = CVPixelBufferCreateWithBytes( kCFAllocatorDefault,
                                                     encoder->width,
                                                     encoder->height,
                                                     kCVPixelFormatType_64ARGB,
                                                     encoder->encode_buffer,
                                                     sizeof(uint16_t) * encoder->width * 4,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     &buffer );

    if (success != kCVReturnSuccess || buffer == NULL) NSLog(@"Failed to create pixel buffer.");
    NSDictionary * colour_attachment = @{(id)kCVImageBufferICCProfileKey : (id)encoder->colour_profile_data};
    CVBufferSetAttachments(buffer, (CFDictionaryRef)colour_attachment, kCVAttachmentMode_ShouldPropagate);
    
    BOOL append_ok = NO;
    int j = 0;
    while (!append_ok && j < 30) {
        if (encoder->adaptor.assetWriterInput.readyForMoreMediaData)  {
            NSLog(@"Processing video frame %d",encoder->frames_encoded);

            CMTime frameTime = CMTimeMake(encoder->frames_encoded * 10000.0, (int32_t)(encoder->fps * 10000.0));
            append_ok = [encoder->adaptor appendPixelBuffer:buffer withPresentationTime:frameTime];
            if(!append_ok){
                NSError * error = encoder->video_writer.error;
                if(error!=nil) {
                    NSLog(@"Unresolved error %@,%@.", error, [error userInfo]);
                }
            }
        }
        else {
            printf("adaptor not ready %d, %d\n", encoder->frames_encoded, j);
            [NSThread sleepForTimeInterval:0.1];
        }
        j++;
    }
    if (!append_ok) {
        printf("error appending image %d times %d\n, with error.", encoder->frames_encoded, j);
    }
    encoder->frames_encoded++;
}

void endWritingVideoFile(AVEncoder_t * encoder)
{
    [encoder->video_writer_input markAsFinished];
    [encoder->video_writer finishWriting];
    NSLog(@"Write Ended");

    encoder->currently_writing = 0;
}

void endWritingVideoFileWithAudio(AVEncoder_t * encoder, uint16_t * audioData, uint64_t audioDataSize)
{
    endWritingVideoFile(encoder);
}
