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

/* Will report file writing progress percentage to *progress (runs in background thread) */
void beginWritingVideoFile(AVEncoder_t * encoder, char * path, double * progress)
{
    if (encoder->currently_writing) return;

    encoder->currently_writing = 1;

    NSURL * outURL = [NSURL fileURLWithPath:[NSString stringWithFormat: @"%s", path]];

    NSLog(outURL.absoluteString);

    NSError * error = nil;
    AVAssetWriter * videoWriter = [[AVAssetWriter alloc] initWithURL: outURL
                                  fileType:AVFileTypeQuickTimeMovie
                                                              error:&error];
    
    NSDictionary * videoSettings = [NSDictionary dictionaryWithObjectsAndKeys:
                                   encoder->codec, AVVideoCodecKey,
                                   [NSNumber numberWithInt:encoder->width], AVVideoWidthKey,
                                   [NSNumber numberWithInt:encoder->height], AVVideoHeightKey,
                                   nil ];
    
    AVAssetWriterInput * videoWriterInput = [AVAssetWriterInput
                                            assetWriterInputWithMediaType:AVMediaTypeVideo
                                            outputSettings:videoSettings];
    
    
    AVAssetWriterInputPixelBufferAdaptor * adaptor = [AVAssetWriterInputPixelBufferAdaptor
                                                     assetWriterInputPixelBufferAdaptorWithAssetWriterInput:videoWriterInput
                                                     sourcePixelBufferAttributes:nil];

    videoWriterInput.expectsMediaDataInRealTime = YES;
    [videoWriter addInput:videoWriterInput];
    
    //Start a session:
    [videoWriter startWriting];
    NSLog(@"Write Started");
    [videoWriter startSessionAtSourceTime:kCMTimeZero];


    //Video encoding

    CVPixelBufferRef buffer = NULL;

    setMlvAlwaysUseAmaze(App->videoMLV);

    for(uint64_t f = 0; f < getMlvFrames(App->videoMLV); ++f)
    {
        /* temp */
        getMlvProcessedFrame16(App->videoMLV, f, encoder->data);
        CVReturn success = CVPixelBufferCreateWithBytes( kCFAllocatorDefault,
                                                         encoder->width,
                                                         encoder->height,
                                                         kCVPixelFormatType_48RGB,
                                                         encoder->data,
                                                         sizeof(uint16_t) * encoder->width * 3,
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
            if (adaptor.assetWriterInput.readyForMoreMediaData)  {
                //print out status:
                NSLog(@"Processing video frame (%d, attempt %d)", (int)f, j);
                
                CMTime frameTime = CMTimeMake(f * 5000.0, (int32_t)(encoder->fps * 1000.0));
                append_ok = [adaptor appendPixelBuffer:buffer withPresentationTime:frameTime];
                if(!append_ok){
                    NSError *error = videoWriter.error;
                    if(error!=nil) {
                        NSLog(@"Unresolved error %@,%@.", error, [error userInfo]);
                    }
                }
                while(!adaptor.assetWriterInput.readyForMoreMediaData) [NSThread sleepForTimeInterval:0.0001];
            }
            else {
                printf("adaptor not ready %d, %d\n", (int)f, j);
                while(!adaptor.assetWriterInput.readyForMoreMediaData) [NSThread sleepForTimeInterval:0.0001];
            }
            j++;
        }
        if (!append_ok) {
            printf("error appending image %d times %d\n, with error.", (int)f, j);
        }
    }

    [videoWriterInput markAsFinished];
    [videoWriter finishWriting];

    [videoWriterInput release];
    [videoWriter release];

    // [m_PictArray removeAllObjects];

    NSLog(@"Write Ended");

    encoder->currently_writing = 0;
}