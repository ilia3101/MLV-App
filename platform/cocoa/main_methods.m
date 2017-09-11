/* Methods for user interface interactions 
 * this is where some real code goes */

#include <math.h>
#include <string.h>
#include <unistd.h>

#import <Cocoa/Cocoa.h>

#include "gui_stuff/app_design.h"

#include "main_methods.h"
#include "../../src/mlv_include.h"

#include "mac_info.h"

#include "background_thread.h"

/* God object used to share globals (type) */
#include "godobject.h"
/* The godobject itsself */
extern godObject_t * App;


/* Initialises value labels with correct slider values */
void initAppWithGod()
{
    /* I really need a slider struct/object ugh :[ */
    [App->exposureSlider exposureSliderMethod];
    [App->saturationSlider saturationSliderMethod];
    [App->kelvinSlider kelvinSliderMethod];
    [App->tintSlider tintSliderMethod];
    [App->darkStrengthSlider darkStrengthMethod];
    [App->darkRangeSlider darkRangeMethod];
    [App->lightStrengthSlider lightStrengthMethod];
    [App->lightRangeSlider lightRangeMethod];
    [App->lightenSlider lightenMethod];
    App->frameChanged = 0;
}


/* This is now a function so that it can be accessedd from any part of the app, not just a button press */
int setAppNewMlvClip(char * mlvPath)
{
    /* Setting app ttle to MLV file name... */
    char * mlvFileName = mlvPath;
    char * extension = mlvPath + strlen(mlvPath) - 3;
    int clipNameStart = strlen(mlvPath) - 1;

    /* Point to just name */
    while (mlvPath[clipNameStart] != '/')
    {
        mlvFileName = mlvPath + clipNameStart;
        clipNameStart--;
    }

    /* Only allow if file has MLV extension */
    if ( !( (extension[0] == 'm' && extension[1] == 'l' && extension[2] == 'v') ||
            (extension[0] == 'M' && extension[1] == 'L' && extension[2] == 'V')  ) )
    {
        return 1;
    }

    free(App->MLVClipName);
    App->MLVClipName = malloc( strlen(mlvFileName) );
    memcpy(App->MLVClipName, mlvFileName, strlen(mlvFileName));

    /* Set app name to include MLV clip's name */
    [App->window setTitle: [NSString stringWithFormat: @ APP_NAME " | %s", mlvFileName]];

    /* Don't allow drawing frames, incase of adjustments during loading */
    App->dontDraw = 1;

    /* Destroy it just for simplicity... and make a new one */
    freeMlvObject(App->videoMLV);

    /* Create a NEW object with a NEW MLV clip! */
    App->videoMLV = initMlvObjectWithClip( (char *)mlvPath );

    /* If use has terminal this is useful */
    printMlvInfo(App->videoMLV);

    /* This needs to be joined (or segmentation fault 11 :D) */
    setMlvProcessing(App->videoMLV, App->processingSettings);

    /* Limit frame cache to amount of RAM we decided earlier */
    setMlvRawCacheLimitMegaBytes(App->videoMLV, (uint64_t)(App->cacheSizeMB));
    /* Tell it slightly less cores than we have, so background caching does not slow down UI interaction */
    setMlvCpuCores(App->videoMLV, (MAC_CORES / 2 + 1));

    /* Adjust image size(probably) */
    [App->previewWindow setImage: nil];

    /* I think this is like free() */
    [App->rawImageObject release];
    [App->rawBitmap release];

    /* Size may need changing */
    free(App->rawImage);
    App->rawImage = malloc( sizeof(uint8_t) * 3 * getMlvWidth(App->videoMLV) * getMlvHeight(App->videoMLV) );

    /* Now reallocate and set up all imagey-objecty things */
    App->rawBitmap = [ [NSBitmapImageRep alloc]
                       initWithBitmapDataPlanes: (unsigned char * _Nullable * _Nullable)&App->rawImage 
                       /* initWithBitmapDataPlanes: NULL */
                       pixelsWide: getMlvWidth(App->videoMLV)
                       pixelsHigh: getMlvHeight(App->videoMLV)
                       bitsPerSample: 8
                       samplesPerPixel: 3
                       hasAlpha: NO 
                       isPlanar: NO
                       colorSpaceName: @"NSDeviceRGBColorSpace"
                       bitmapFormat: 0
                       bytesPerRow: getMlvWidth(App->videoMLV) * 3
                       bitsPerPixel: 24 ];

    App->rawImageObject = [[NSImage alloc] initWithSize: NSMakeSize(getMlvWidth(App->videoMLV),getMlvHeight(App->videoMLV))];
    [App->rawImageObject addRepresentation: App->rawBitmap];
    [App->rawImageObject setCacheMode: NSImageCacheNever];
    [App->previewWindow setImage: App->rawImageObject];

    /* If ALways AMaZE was set, set it again on new clip */
    if ([App->alwaysUseAmazeSelector state] == NSOnState)
    {
        setMlvAlwaysUseAmaze(App->videoMLV);
    }

    /* Allow drawing frames */
    App->dontDraw = 0;
    /* Set current frame where slider is at */
    App->currentFrameIndex = (int)( App->frameSliderPosition * (getMlvFrames(App->videoMLV)-1) );
    /* So view updates and shows new clip */
    App->frameChanged++;

    /* End of MLV opening stuff */

    /* Audio test - seems to crash when in an app bundle :[ */
    //writeMlvAudioToWave(App->videoMLV, "test.wav");

    return 0;
}

/* Button methods */

@implementation NSButton (mainMethods)

/* Enables/disables always AMaZE requirement */
-(void)toggleAlwaysAmaze
{
    if ([self state] == NSOnState) 
    {
        setMlvAlwaysUseAmaze(App->videoMLV);
        App->frameChanged++;
    }
    else 
    {
        setMlvDontAlwaysUseAmaze(App->videoMLV);
        App->frameChanged++;
    }
}

/* Enables/disables highlight reconstruction */
-(void)toggleHighlightReconstruction
{
    if ([self state] == NSOnState) 
    {
        processingEnableHighlightReconstruction(App->processingSettings);
        App->frameChanged++;
    }
    else 
    {
        processingDisableHighlightReconstruction(App->processingSettings);
        App->frameChanged++;
    }
}

/* Enable/disable tonemapping */
-(void)toggleTonemapping
{
    if ([self state] == NSOnState) 
    {
        processingEnableTonemapping(App->processingSettings);
        App->frameChanged++;
    }
    else 
    {
        processingDisableTonemapping(App->processingSettings);
        App->frameChanged++;
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
                setAppNewMlvClip((char *)mlvPathString);
            }
        }
        [panel release];
    } ];
}


/* Exports to Prores 4444 (currently with ffmpeg and intermediate PNG - no AVfoundation yet) */
-(void)exportProRes4444
{
    if (isMlvActive(App->videoMLV))
    {    
        /* Create open panel */
        NSOpenPanel * panel = [NSOpenPanel openPanel];

        [panel setPrompt:[NSString stringWithFormat:@"Export Here"]];

        [panel setCanChooseFiles: NO];
        [panel setCanChooseDirectories: YES];
        [panel setAllowsMultipleSelection: NO];
        [panel setCanCreateDirectories: YES];

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
                    setMlvAlwaysUseAmaze(App->videoMLV);

                    /* Progress window */

                    /* We will use the same NSBitmapImageRep as for exporting as for preview window */
                    for (int f = 0; f < getMlvFrames(App->videoMLV); ++f)
                    {
                        /* Generate file name for frame */
                        snprintf(exportPath, 2047, "%s/%s_frame_%.5i.png", exportDir, App->MLVClipName, f);

                        /* Get processed frame */
                        getMlvProcessedFrame8(App->videoMLV, f, App->rawImage);

                        /* Export */
                        NSData * imageFile = [[App->rawBitmap representationUsingType: NSPNGFileType properties: nil] autorelease];
                        [imageFile writeToFile: [NSString stringWithUTF8String:exportPath] atomically: NO];

                        NSLog(@"Exported frame %i to: %s", f, exportPath);
                    }

                    setMlvDontAlwaysUseAmaze(App->videoMLV);

                    /* Run ffmpeg to create ProRes file */
                    char * ffmpegPath = (char *)[[[NSBundle mainBundle] pathForResource:@"ffmpeg" ofType: nil] UTF8String];
                    snprintf( commandStr, 2047, "\"%s\" -r %f -i %s/%s_frame_%s.png -c:v prores_ks -profile:v 4444 %s/%.8s.mov", 
                              ffmpegPath, getMlvFramerate(App->videoMLV), exportDir, App->MLVClipName, "\%05d", pathString, App->MLVClipName);
                    system(commandStr);

                    /* Delete hidden directory */
                    snprintf(commandStr, 2047, "rm -rf %s", exportDir);
                    system(commandStr);

                    // snprintf(commandStr, 2047, "rm -rf %s", exportDir);
                    // system(commandStr);

                    free(exportPath);
                    free(exportDir);
                    free(commandStr);

                    /* Give notification to user */
                    NSUserNotification * notification = [[[NSUserNotification alloc] init] autorelease];
                    notification.title = @APP_NAME;
                    notification.informativeText = [NSString stringWithFormat:@"Finished exporting."];
                    notification.soundName = NSUserNotificationDefaultSoundName;
                    [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];

                    NSLog(@"\n\n\n\n\n\n STILL ALIVE \n\n\n\n\n\n\n");
                }
            }
            [panel release];
        } ];
    }
}

@end


/* NSPopUpButton methods */
@implementation NSPopUpButton (mainMethods)

/* Select processing image profile */
-(void)toggleImageProfile
{
    /* Indexes of menu items correspond to defines of processing profiles */
    processingSetImageProfile(App->processingSettings, (int)[self indexOfSelectedItem]);
    App->frameChanged++;
}


/* Select processing image profile */
-(void)toggleVideoFormat
{
    /* Indexes of menu items correspond to defines of processing profiles */
    processingSetImageProfile(App->processingSettings, (int)[self indexOfSelectedItem]);
    App->frameChanged++;
}

@end


/* Slider methods */
@implementation NSSlider (mainMethods)

/* Change frame based on time slider */
-(void)timelineSliderMethod
{
    App->currentFrameIndex = (int)( [self doubleValue] * (getMlvFrames(App->videoMLV)-1) );
    App->frameSliderPosition = [self doubleValue];

    App->frameChanged++;
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
    processingSetExposureStops(App->processingSettings, exposureValueStops);

    [App->exposureValueLabel setStringValue: [NSString stringWithFormat:@"%6.3f", exposureValueStops - 1.2]];

    App->frameChanged++;
}

-(void)saturationSliderMethod 
{
    /* Make it so that saturation of 1.0(original) is in the middle 
     * of the slider, and it can go down to 0.0 and up to 3.6 */
    double saturationSliderValue = [self doubleValue] * 2.0;
    double saturationValue = pow( saturationSliderValue, log(3.6)/log(2.0) );

    /* Yea whatever */
    processingSetSaturation(App->processingSettings, saturationValue);

    [App->saturationValueLabel setStringValue: [NSString stringWithFormat:@"%6.3f", saturationValue]];

    App->frameChanged++;
}

-(void)kelvinSliderMethod 
{
    /* Slider has to be in range 2500 - 10000, cos my function is limited to that */
    double kelvinValue = [self doubleValue] * 7500.0 + 2500.0;

    /* Set processing object white balance */
    processingSetWhiteBalanceKelvin(App->processingSettings, kelvinValue);

    [App->kelvinValueLabel setStringValue: [NSString stringWithFormat:@"%5.dk", (int)kelvinValue]];

    App->frameChanged++;
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
    processingSetWhiteBalanceTint(App->processingSettings, tintValue);

    [App->tintValueLabel setStringValue: [NSString stringWithFormat:@"%6.3f", ([self doubleValue] - 0.5) * 2 ]];

    App->frameChanged++;
}

/* All contrast/curve settings */
-(void)darkStrengthMethod {
    processingSetDCFactor(App->processingSettings, [self doubleValue] * 22.5);
    [App->darkStrengthValueLabel setStringValue: [NSString stringWithFormat:@"%6.3f", [self doubleValue]]];
    App->frameChanged++;
} -(void)darkRangeMethod {
    processingSetDCRange(App->processingSettings, [self doubleValue]);
    [App->darkRangeValueLabel setStringValue: [NSString stringWithFormat:@"%6.3f", [self doubleValue]]];
    App->frameChanged++;
} -(void)lightStrengthMethod {
    processingSetLCFactor(App->processingSettings, [self doubleValue] * 11.2);
    [App->lightStrengthValueLabel setStringValue: [NSString stringWithFormat:@"%6.3f", [self doubleValue]]];
    App->frameChanged++;
} -(void)lightRangeMethod {
    processingSetLCRange(App->processingSettings, [self doubleValue]);
    [App->lightRangeValueLabel setStringValue: [NSString stringWithFormat:@"%6.3f", [self doubleValue]]];
    App->frameChanged++;
} -(void)lightenMethod {
    processingSetLightening(App->processingSettings, [self doubleValue] * 0.6);
    [App->lightenValueLabel setStringValue: [NSString stringWithFormat:@"%6.3f", [self doubleValue]]];
    App->frameChanged++;
}

@end