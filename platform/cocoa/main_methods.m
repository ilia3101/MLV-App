#include <math.h>
#include <string.h>
#include <unistd.h>

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

extern char * MLVClipName;

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
extern double frameSliderPosition;

/* We always set MLV object to this amount of cache */
extern int cacheSizeMB;


/* This is now a function so that it can be accessedd from any part of the app, not just a button press */
void setAppNewMlvClip(char * mlvPathString, char * mlvFileName)
{
    free(MLVClipName);
    MLVClipName = malloc( strlen(mlvFileName) );
    memcpy(MLVClipName, mlvFileName, strlen(mlvFileName));

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

    /* Limit frame cache to amount of RAM we decided earlier */
    setMlvRawCacheLimitMegaBytes(videoMLV, (uint64_t)(cacheSizeMB));
    /* Tell it slightly less cores than we have, so background caching does not slow down UI interaction */
    setMlvCpuCores(videoMLV, (MAC_CORES / 2 + 1));

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
    /* Set current frame where slider is at */
    currentFrameIndex = (int)( frameSliderPosition * (getMlvFrames(videoMLV)-1) );
    /* So view updates and shows new clip */
    frameChanged++;

    /* End of MLV opening stuff */
}

/* Button methods */

@implementation NSButton (mainMethods)

/* Enables/disables always AMaZE requirement */
-(void)toggleAlwaysAmaze
{
    if ([self state] == NSOnState) 
    {
        setMlvAlwaysUseAmaze(videoMLV);
        frameChanged++;
    }
    else 
    {
        setMlvDontAlwaysUseAmaze(videoMLV);
        frameChanged++;
    }
}

/* Enables/disables highlight reconstruction */
-(void)toggleHighlightReconstruction
{
    if ([self state] == NSOnState) 
    {
        processingEnableHighlightReconstruction(processingSettings);
        frameChanged++;
    }
    else 
    {
        processingDisableHighlightReconstruction(processingSettings);
        frameChanged++;
    }
}

/* Enable/disable tonemapping */
-(void)toggleTonemapping
{
    if ([self state] == NSOnState) 
    {
        processingEnableTonemapping(processingSettings);
        frameChanged++;
    }
    else 
    {
        processingDisableTonemapping(processingSettings);
        frameChanged++;
    }
}

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

                setAppNewMlvClip((char *)mlvPathString, (char *)mlvFileName);
            }
        }
        [panel release];
    } ];
}


/* Exports to Prores 4444 (currently with ffmpeg and intermediate PNG - no AVfoundation yet) */
-(void)exportProRes4444
{
    if (isMlvActive(videoMLV))
    {    
        /* Create open panel */
        NSOpenPanel * panel = [[NSOpenPanel openPanel] retain];

        [panel setCanChooseFiles: NO];
        [panel setCanChooseDirectories: YES];
        [panel setAllowsMultipleSelection: NO];

        [panel beginWithCompletionHandler: ^ (NSInteger result) 
        {
            if (result == NSFileHandlingPanelOKButton)
            {
                for (NSURL * pathURL in [panel URLs])
                {
                    char * pathString = (char *)[pathURL.path UTF8String];
                    char * exportPath = malloc(2048);
                    char * exportDir = malloc(2048);
                    char * commandStr = malloc(2048);

                    /* Hidden directory path */
                    snprintf(exportDir, 2047, "%s/.temp_png", pathString);

                    /* Create hidden directory */
                    snprintf(commandStr, 2047, "mkdir %s", exportDir);
                    system(commandStr);

                    /* So we always get amaze frames for exporting */
                    setMlvAlwaysUseAmaze(videoMLV);

                    /* We will use the same NSBitmapImageRep as for exporting as for preview window */
                    for (int f = 0; f < getMlvFrames(videoMLV); ++f)
                    {
                        /* Generate file name for frame */
                        snprintf(exportPath, 2047, "%s/frame_%.5i.png", exportDir, f);

                        /* Get processed frame */
                        getMlvProcessedFrame8(videoMLV, f, rawImage);

                        /* Export */
                        NSData * imageFile = [rawBitmap representationUsingType: NSPNGFileType properties: nil];
                        [imageFile writeToFile: [NSString stringWithUTF8String:exportPath] atomically: NO];

                        NSLog(@"Exported frame %i to: %s", f, exportPath);
                    }

                    setMlvDontAlwaysUseAmaze(videoMLV);

                    /* Run ffmpeg to create ProRes file */
                    char * ffmpegPath = (char *)[[[NSBundle mainBundle] pathForResource:@"ffmpeg" ofType: nil] UTF8String];
                    snprintf( commandStr, 2047, "\"%s\" -i %s/frame_%s.png -framerate %f -c:v prores_ks -profile:v 4444 %s/%.8s.mov", 
                              ffmpegPath, exportDir, "\%05d", getMlvFramerate(videoMLV), pathString, MLVClipName);
                    system(commandStr);

                    /* Delete hidden directory */
                    snprintf(commandStr, 2047, "rm -rf %s", exportDir);
                    system(commandStr);

                    // snprintf(commandStr, 2047, "rm -rf %s", exportDir);
                    // system(commandStr);

                    free(exportPath);
                    free(exportDir);
                    free(commandStr);
                }
            }
            [panel release];
        } ];
    }
}

@end

/* Slider methods */

@implementation NSSlider (mainMethods)

/* Change frame based on time slider */
-(void)timelineSliderMethod
{
    currentFrameIndex = (int)( [self doubleValue] * (getMlvFrames(videoMLV)-1) );
    frameSliderPosition = [self doubleValue];

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

/* NSImageView stuff */

@implementation NSImageView (mainMethods)

-(void)updatePreviewWindow
{
    [self setImage: nil];
    [self setImage: rawImageObject];
    [self setNeedsDisplay: YES];
}

@end