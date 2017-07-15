#include <math.h>
#include <string.h>
#include <mach/mach.h>

#import "Cocoa/Cocoa.h"

#include "gui_stuff/app_design.h"

#include "main_methods.h"
#include "../../src/mlv_include.h"

#include "mac_info.h"

#include "background_thread.h"

/* Methods for user interface interactions 
 * this is where some real code goes */

/* Make sure we hav these - or dusnt work :[ */
extern mlvObject_t * videoMLV;
extern processingObject_t * processingSettings;

extern NSImage * rawImageObject;
/* Holds a (the) processed frame that is displayed
 * Will be changed with methods from this file */
extern NSBitmapImageRep * rawBitmap;
/* The image data of ^^^^^^^^^^^^^^^ */
extern uint8_t * rawImage;

/* This is the view, so we can refresh it
 * by doing: 
 *     [previewWindow setImage: tempImage];
 *     [previewWindow setImage: rawImageObject]; 
 * Sets an empty image, then back to the proper one */
extern NSImageView * previewWindow;

/* App window */
extern NSWindow * window;

/* ++ this on changes such as settings adjustments or on playback to go to next frame */
extern int frameChanged;
/* To stop frame rendeirng if needed */
extern int dontDraw;

/* What frame we r on */
extern int currentFrameIndex;

/* We always set MLV object to this amount of cache */
extern int cacheSizeMB;

/* Button methods */

@implementation NSButton (mainMethods)

/* Open file dialog + set new MLV clip */
-(void)openMlvDialog
{
    /* Create open panel */
    NSOpenPanel * panel = [[NSOpenPanel openPanel] retain];

    [panel setCanChooseFiles: YES];
    [panel setCanChooseDirectories: NO];
    [panel setAllowsMultipleSelection: YES];

    /* Can only choose MLV files */
    [panel setAllowedFileTypes: [NSArray arrayWithObject: @"mlv"]];

    [panel beginWithCompletionHandler: ^ (NSInteger result) 
    {
        if (result == NSFileHandlingPanelOKButton)
        {
            /* What stackoverflow said */
            for (NSURL * fileURL in [panel URLs])
            {
                const char * mlvPathString = [fileURL.path UTF8String];
                const char * mlvFileName = [[[fileURL.path lastPathComponent] stringByDeletingPathExtension] UTF8String];

                int pathLength = strlen(mlvPathString);
                NSLog(@"New MLV file: %s, strlen: %i", mlvPathString, pathLength);

                /* Set app name to include MLV clip's name */
                [window setTitle: [NSString stringWithFormat: @ APP_NAME " | %s", mlvFileName]];

                /* Don't allow drawing frames, incase of adjustments during loading */
                dontDraw = 1;

                /* Destroy it just for simplicity... and make a new one */
                freeMlvObject(videoMLV);

                /* Create a NEW object with a NEW MLV clip! */
                videoMLV = initMlvObjectWithClip( (char *)mlvPathString );

                /* This funtion really SHOULD be integrated with the one above */
                mapMlvFrames(videoMLV, 0);

                /* If use has terminal this is useful */
                printMlvInfo(videoMLV);

                /* This needs to be joined (or segmentation fault 11 :D) */
                setMlvProcessing(videoMLV, processingSettings);

                /* Limit frame cache to 38% of RAM size (its fast anyway) */
                setMlvRawCacheLimitMegaBytes(videoMLV, (uint64_t)(cacheSizeMB));
                /* Tell it how many cores we habe so it can be optimal */
                setMlvCpuCores(videoMLV, MAC_CORES);

                /* Adjust image size(probably) */
                [previewWindow setImage: nil];

                /* I think this is like free() */
                [rawImageObject release];
                [rawBitmap release];

                /* Size may need changing */
                free(rawImage);
                rawImage = malloc( sizeof(uint8_t) * 3 * getMlvWidth(videoMLV) * getMlvHeight(videoMLV) );

                /* Now reallocate and set up all imagey-objecty things */
                rawBitmap = [ [NSBitmapImageRep alloc]
                              initWithBitmapDataPlanes: (unsigned char * _Nullable * _Nullable)&rawImage 
                              /* initWithBitmapDataPlanes: NULL */
                              pixelsWide: getMlvWidth(videoMLV)
                              pixelsHigh: getMlvHeight(videoMLV)
                              bitsPerSample: 8
                              samplesPerPixel: 3
                              hasAlpha: NO 
                              isPlanar: NO
                              colorSpaceName: @"NSDeviceRGBColorSpace"
                              bitmapFormat: 0
                              bytesPerRow: getMlvWidth(videoMLV) * 3
                              bitsPerPixel: 24 ];

                rawImageObject = [[NSImage alloc] initWithSize: NSMakeSize(getMlvWidth(videoMLV),getMlvHeight(videoMLV)) ];
                [rawImageObject addRepresentation:rawBitmap];

                [previewWindow setImage: rawImageObject];

                /* Allow drawing frames */
                dontDraw = 0;
                /* All MLV frames have at least one frame, so this should be safe */
                currentFrameIndex = 0;
                /* So view updates and shows new clip */
                frameChanged++;

                /* End of MLV opening stuff */
            }
        }
        [panel release];
    } ];
}

/* (unfinished btw) */
-(void)exportBmpSequence 
{
    /* Create open panel */
    NSOpenPanel * panel = [[NSOpenPanel openPanel] retain];

    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setAllowsMultipleSelection: YES];

    /* Can only choose MLV files */
    [panel setAllowedFileTypes: [NSArray arrayWithObject: @"mlv"]];

    [panel beginWithCompletionHandler: ^ (NSInteger result) 
    {
        if (result == NSFileHandlingPanelOKButton)
        {
            /* What stackoverflow said */
            for (NSURL * fileURL in [panel URLs])
            {
                const char * mlvPathString = [fileURL.path UTF8String];
                const char * mlvFileName = [[[fileURL.path lastPathComponent] stringByDeletingPathExtension] UTF8String];

                NSLog(@"Export to: %s", mlvPathString);
            }
        }
        [panel release];
    } ];
}

@end

/* Slider methods */

@implementation NSSlider (mainMethods)

/* Change frame based on time slider */
-(void)timelineSliderMethod
{
    currentFrameIndex = (int)( [self doubleValue] * (getMlvFrames(videoMLV)-1) );

    frameChanged++;
}

/* Following are all processing adjustments */

-(void)exposureSliderMethod 
{
    /* Slider value is from 0-1 (floatdouble), and the
     * exposure range shall be from -4 to 4(stops) */

    /* Center the 0-1 value and * 8 to get a range of -4 to 4 (stops) 
     * Also featuring the +1.2 stop lie that makes highlights 'recoverable' */
    double exposureValueStops = ([self doubleValue] - 0.5) * 8.0 + 1.2;

    /* Set processing object's exposure now */
    processingSetExposureStops(processingSettings, exposureValueStops);

    frameChanged++;
}

-(void)saturationSliderMethod 
{
    /* Make it so that saturation of 1.0(original) is in the middle 
     * of the slider, and it can go down to 0.0 and up to 3.6 */
    double saturationSliderValue = [self doubleValue] * 2.0;
    double saturationValue = pow( saturationSliderValue, log(3.6)/log(2.0) );

    /* Yea whatever */
    processingSetSaturation(processingSettings, saturationValue);

    frameChanged++;
}

-(void)kelvinSliderMethod 
{
    /* Slider has to be in range 2500 - 10000, cos my function is limited to that */
    double kelvinValue = [self doubleValue] * 7500.0 + 2500.0;

    /* Set processing object white balance */
    processingSetWhiteBalanceKelvin(processingSettings, kelvinValue);

    frameChanged++;
}

-(void)tintSliderMethod
{
    /* Slider has to be in range -10 to 10, as that sounds about right */
    double tintSliderValue = [self doubleValue] - 0.5;
    /* Control should be more fine when its closer to zero... */
    double tintValue = tintSliderValue;
    /* Invert for power if negative */
    if (tintValue < 0) tintValue = -tintValue;
    /* Power */
    tintValue = pow(tintValue, 1.7) * 20.0;
    /* Invert back if needed */
    if (tintSliderValue < 0) tintValue = -tintValue;

    /* Set processing object white balance */
    processingSetWhiteBalanceTint(processingSettings, tintValue);

    frameChanged++;
}

/* All contrast/curve settings */
-(void)darkStrengthMethod {
    processingSetDCFactor(processingSettings, [self doubleValue] * 22.5);
    frameChanged++;
} -(void)darkRangeMethod {
    processingSetDCRange(processingSettings, [self doubleValue]);
    frameChanged++;
} -(void)lightStrengthMethod {
    processingSetLCFactor(processingSettings, [self doubleValue] * 11.2);
    frameChanged++;
} -(void)lightRangeMethod {
    processingSetLCRange(processingSettings, [self doubleValue]);
    frameChanged++;
} -(void)lightenMethod {
    processingSetLightening(processingSettings, [self doubleValue] * 0.6);
    frameChanged++;
}

@end