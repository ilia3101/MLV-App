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
AVEncoder_t * initAVEncoder(int width, int height, int codec, double fps)
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
        case AVF_CODEC_HEVC:
            encoder->codec = AVVideoCodecHEVC;
        case AVF_CODEC_PRORES_422:
            encoder->codec = AVVideoCodecAppleProRes422;
        case AVF_CODEC_PRORES_4444:
            encoder->codec = AVVideoCodecAppleProRes4444;
    }

    encoder->provider = CGDataProviderCreateWithData(nil, encoder->data, width * height * sizeof(uint16_t) * 3, nil);

    return encoder;
}

void freeAVEncoder(AVEncoder_t * encoder)
{
    CGDataProviderRelease(encoder->provider);
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
    AVAssetWriter * videoWriter = [ [AVAssetWriter alloc] initWithURL: outURL 
                                    fileType:AVFileTypeQuickTimeMovie
                                    error:&error ];    
    // NSParameterAssert(videoWriter);

    NSDictionary * videoSettings = [ NSDictionary dictionaryWithObjectsAndKeys:
                                     encoder->codec, AVVideoCodecKey,
                                     [NSNumber numberWithInt:encoder->size.width], AVVideoWidthKey,
                                     [NSNumber numberWithInt:encoder->size.height], AVVideoHeightKey,
                                     nil ];

    AVAssetWriterInput * videoWriterInput = [ [AVAssetWriterInput
                                              assetWriterInputWithMediaType: AVMediaTypeVideo
                                              outputSettings:videoSettings] retain];

    /* Catchy naming, Apple */
    AVAssetWriterInputPixelBufferAdaptor * adaptor = [ AVAssetWriterInputPixelBufferAdaptor
                                                       assetWriterInputPixelBufferAdaptorWithAssetWriterInput:videoWriterInput
                                                       sourcePixelBufferAttributes:nil];

    // NSParameterAssert(videoWriterInput);
    // NSParameterAssert([videoWriter canAddInput:videoWriterInput]);

    videoWriterInput.expectsMediaDataInRealTime = NO;
    [videoWriter addInput:videoWriterInput];

    //Start a session:
    [videoWriter startWriting];
    NSLog(@"Write Started");
    [videoWriter startSessionAtSourceTime:kCMTimeZero];


    //Video encoding

    CVPixelBufferRef buffer = NULL;

    //convert uiimage to CGImage.

    int frameCount = 0;

    setMlvAlwaysUseAmaze(App->videoMLV);

    for(int i = 0; i<getMlvFrames(App->videoMLV); i++)
    {
        /* temp */
        getMlvProcessedFrame16(App->videoMLV, i, encoder->data);

        CVReturn success = CVPixelBufferCreate( kCFAllocatorDefault,
                                                encoder->width,
                                                encoder->height,
                                                kCVPixelFormatType_48RGB,
                                                NULL, 
                                                &buffer );

        if (success != kCVReturnSuccess) NSLog(@"Failed to create pixel buffer.");

        BOOL append_ok = NO;
        int j = 0;

        while (append_ok == NO && j < 30) /* Try appending it 30 times? */
        {
            if (adaptor.assetWriterInput.readyForMoreMediaData) 
            {
                printf("appending %d attemp %d\n", frameCount, j);

                CMTime frameTime = CMTimeMake(frameCount * 5000.0, (int32_t)(encoder->fps * 1000.0));

                append_ok = [adaptor appendPixelBuffer:buffer withPresentationTime:frameTime];

                
                while(!adaptor.assetWriterInput.readyForMoreMediaData) [NSThread sleepForTimeInterval:0.0001];
            } 
            else 
            {
                printf("adaptor not ready %d, %d\n", frameCount, j);
                [NSThread sleepForTimeInterval:0.1];
            }
            j++;
        }
        if (append_ok == NO) 
        {
            printf("error appending image %d times %d\n", frameCount, j);
        }
        frameCount++;

        CVBufferRelease(buffer);
    }

    [videoWriterInput markAsFinished];
    [videoWriter finishWriting];

    [videoWriterInput release];
    [videoWriter release];

    // [m_PictArray removeAllObjects];

    NSLog(@"Write Ended");

    encoder->currently_writing = 0;
}