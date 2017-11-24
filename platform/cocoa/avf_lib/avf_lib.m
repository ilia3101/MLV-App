#import "Cocoa/Cocoa.h"
#import <AVFoundation/AVFoundation.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#include "avf_lib.h"

#include "../../../src/mlv_include.h"
#include "../godobject.h"
extern godObject_t * App;

/* Encode a video using AVFoundation */
AVEncoder_t * initAVEncoder(int width, int height, int codec, int colourSpace, double fps)
{
    AVEncoder_t * encoder = (AVEncoder_t *)calloc( sizeof(AVEncoder_t), 1 );

    encoder->fps = fps;
    encoder->width = width;
    encoder->height = height;
    encoder->size = CGSizeMake(width, height);
    encoder->data = malloc(width * height * sizeof(uint16_t) * 3);

    switch (codec)
    {
        case AVF_CODEC_H264:
            encoder->codec = AVVideoCodecH264;
            break;
        case AVF_CODEC_HEVC:
            encoder->codec = AVVideoCodecHEVC;
            break;
        case AVF_CODEC_PRORES_422:
            encoder->codec = AVVideoCodecAppleProRes422;
            break;
        case AVF_CODEC_PRORES_4444:
            encoder->codec = AVVideoCodecAppleProRes4444;
            break;
    }

    CFStringRef colour_space_name;
    switch (colourSpace)
    {
        case AVF_COLOURSPACE_SRGB:
            colour_space_name = kCGColorSpaceSRGB;
            break;
        case AVF_COLOURSPACE_DCIP3:
            colour_space_name = kCGColorSpaceDCIP3;
            break;
        case AVF_COLOURSPACE_ADOBE_RGB:
            colour_space_name = kCGColorSpaceAdobeRGB1998;
            break;
        case AVF_COLOURSPACE_REC_709:
            colour_space_name = kCGColorSpaceITUR_709;
            break;
        case AVF_COLOURSPACE_REC_2020:
            colour_space_name = kCGColorSpaceITUR_2020;
            break;
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
    free(encoder->data);
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
    
    NSLog(@"Start building video from defined frames.");
    
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

    NSLog(@"Writing");
    [encoder->video_writer startWriting];
    [encoder->video_writer startSessionAtSourceTime:kCMTimeZero];
    
    CVPixelBufferRef buffer = NULL;

    int frameCount = 0;

    for (uint64_t f = 0; f < getMlvFrames(App->videoMLV); ++f)
    {
        //UIImage * img = frm._imageFrame;
        // buffer = [self pixelBufferFromCGImage:[img CGImage]];
        getMlvProcessedFrame8(App->videoMLV, f, (uint8_t *)encoder->data);
        CVReturn success = CVPixelBufferCreateWithBytes( kCFAllocatorDefault,
                                                         encoder->width,
                                                         encoder->height,
                                                         kCVPixelFormatType_24RGB,
                                                         encoder->data,
                                                         sizeof(uint8_t) * encoder->width * 3,
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
                NSLog(@"Processing video frame %d",frameCount);

                CMTime frameTime = CMTimeMake(f * 10000.0, (int32_t)(encoder->fps * 10000.0));
                append_ok = [encoder->adaptor appendPixelBuffer:buffer withPresentationTime:frameTime];
                if(!append_ok){
                    NSError * error = encoder->video_writer.error;
                    if(error!=nil) {
                        NSLog(@"Unresolved error %@,%@.", error, [error userInfo]);
                    }
                }
            }
            else {
                printf("adaptor not ready %d, %d\n", frameCount, j);
                [NSThread sleepForTimeInterval:0.1];
            }
            j++;
        }
        if (!append_ok) {
            printf("error appending image %d times %d\n, with error.", frameCount, j);
        }
        frameCount++;
    }
}


void endWritingVideoFile(AVEncoder_t * encoder)
{
    [encoder->video_writer_input markAsFinished];
    [encoder->video_writer finishWriting];
    NSLog(@"Write Ended");

    encoder->currently_writing = 0;
}