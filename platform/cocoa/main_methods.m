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

/* My custom AVFoundation lib innit */
#include "avf_lib/avf_lib.h"


/* Initialises value labels with correct slider values */
void initAppWithGod()
{
    syncGUI();
}

/* Also will be called when switching between clips in session */
void syncGUI()
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
    [App->sharpnessSlider sharpnessMethod];
    [App->chromaBlurSlider chromaBlurMethod];
    [App->processingTabSwitch toggleTab];
    [App->fixRawSelector toggleLLRawProc];
    [App->dualISOOption dualISOMethod];
    [App->focusPixelOption focusPixelMethod];
    [App->badPixelOption badPixelMethod];
    [App->stripeFixOption verticalStripeMethod];
    [App->chromaSmoothOption chromaSmoothMethod];
    [App->patternNoiseOption patternNoiseMethod];
    [App->chromaSeparationSelector toggleChromaSeparation];
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

    /* Default: don't enable llrawproc */
    App->videoMLV->llrawproc->fix_raw = 0;

    /* Size may need changing */
    free(App->rawImage);
    App->rawImage = malloc( sizeof(uint8_t) * 3 * getMlvWidth(App->videoMLV) * getMlvHeight(App->videoMLV) );

    /* The bitmap-rep thing is only used for exporting */
    [App->rawBitmap release];
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

    [App->previewWindow setSourceImage:App->rawImage width:getMlvWidth(App->videoMLV) height:getMlvHeight(App->videoMLV) bitDepth:8];

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

    syncGUI();

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
    }
    else 
    {
        setMlvDontAlwaysUseAmaze(App->videoMLV);
    }
    App->frameChanged++;
}

/* Enables/disables chroma separation */
-(void)toggleChromaSeparation
{
    if ([self state] == NSOnState) 
    {
        processingEnableChromaSeparation(App->processingSettings);
    }
    else 
    {
        processingDisableChromaSeparation(App->processingSettings);
        /* Set chroma blur to zero */
        [App->chromaBlurSlider setDoubleValue:0.0];
        [App->chromaBlurSlider chromaBlurMethod]; /* to refresh the label */
    }
    App->frameChanged++;
}

/* Enables/disables highlight reconstruction */
-(void)toggleHighlightReconstruction
{
    if ([self state] == NSOnState) 
    {
        processingEnableHighlightReconstruction(App->processingSettings);
    }
    else 
    {
        processingDisableHighlightReconstruction(App->processingSettings);
    }
    App->frameChanged++;
}

/* Enable/disable tonemapping */
-(void)toggleTonemapping
{
    if ([self state] == NSOnState) 
    {
        processingEnableTonemapping(App->processingSettings);
    }
    else 
    {
        processingDisableTonemapping(App->processingSettings);
    }
    App->frameChanged++;
}

/* Enable/disable tonemapping */
-(void)toggleLLRawProc
{
    if ([self state] == NSOnState) 
    {
        App->videoMLV->llrawproc->fix_raw = 1;
    }
    else 
    {
        App->videoMLV->llrawproc->fix_raw = 0;
    }
    mark_mlv_uncached(App->videoMLV);  if (!isMlvObjectCaching(App->videoMLV)) enableMlvCaching(App->videoMLV);// TEMPORARY
    App->frameChanged++;
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
        NSOpenPanel * panel = [[NSOpenPanel openPanel] retain];

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

                    snprintf(exportPath, 2048, "%s/%.8s.mov", pathString, App->MLVClipName);

                    AVEncoder_t * encoder = initAVEncoder( getMlvWidth(App->videoMLV),
                                                           getMlvHeight(App->videoMLV),
                                                           AVF_CODEC_PRORES_422,
                                                           AVF_COLOURSPACE_SRGB,
                                                           getMlvFramerate(App->videoMLV) );

                    beginWritingVideoFile(encoder, exportPath, NULL);

                    freeAVEncoder(encoder);

                    free(exportPath);

                    /* Give notification to user */
                    NSUserNotification * notification = [[[NSUserNotification alloc] init] autorelease];
                    notification.title = @APP_NAME;
                    notification.informativeText = [NSString stringWithFormat:@"Finished exporting."];
                    notification.soundName = NSUserNotificationDefaultSoundName;
                    [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
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
    #define KELVIN_MAX 10000.0
    #define KELVIN_MIN 2000.0
    /* Slider has to be in range 2000 - 10000, cos my function is limited to that */
    double kelvinValue = [self doubleValue] * (KELVIN_MAX - KELVIN_MIN) + KELVIN_MIN;
    #undef KELVIN_MAX
    #undef KELVIN_MIN

    /* Set processing object white balance */
    processingSetWhiteBalanceKelvin(App->processingSettings, kelvinValue);

    [App->kelvinValueLabel setStringValue: [NSString stringWithFormat:@"%5.dk", (int)kelvinValue]];

    App->frameChanged++;
}

-(void)tintSliderMethod
{
    /* Slider has to be in range -10 to 10, as that sounds about right */
    double tintSliderValue = ([self doubleValue] - 0.5) * 2.0;
    /* Control should be more fine when its closer to zero... */
    double tintValue = tintSliderValue;
    /* Invert for power if negative */
    if (tintValue < 0) tintValue = -tintValue;
    /* Power */
    tintValue = pow(tintValue, 1.7) * 10.0;
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

-(void)sharpnessMethod
{
    double sharpnessValue = [self doubleValue];
    processingSetSharpening(App->processingSettings, sharpnessValue);
    [App->sharpnessValueLabel setStringValue: [NSString stringWithFormat:@"%6.3f", sharpnessValue]];
    App->frameChanged++;
}

-(void)chromaBlurMethod
{
    /* This is the radius actually, I've limited it to 12 */
    int chromaBlurValue = (int)([self doubleValue] * 12.0 + 0.5);
    processingSetChromaBlurRadius(App->processingSettings, chromaBlurValue);
    /* If chroma blur on, and chroma separation is off, enable chroma separation */
    if (chromaBlurValue > 0)
    {
        App->chromaSeparationSelector.state = NSOnState;
        processingEnableChromaSeparation(App->processingSettings);
        [App->chromaBlurValueLabel setStringValue: [NSString stringWithFormat:@"%6.i", chromaBlurValue]];
    }
    else
    {
        [App->chromaBlurValueLabel setStringValue: [NSString stringWithFormat:@"   Off"]];
    }
    App->frameChanged++;
}

@end

/* NSSegmentedControl methods */
@implementation NSSegmentedControl (mainMethods)

/* This method is called by *all* dual ISO selectors */
-(void)dualISOMethod
{
    if (!App->videoMLV) return;
    switch ([App->dualISOOption selectedSegment])
    {
        case 0: /* Is off */
            App->videoMLV->llrawproc->dual_iso = 0;
            break;
        case 1: /* Is Set to high quality 20 bit */
            App->videoMLV->llrawproc->dual_iso = 2;
            break;
        case 2: /* Is set to quick low quality mode */
            App->videoMLV->llrawproc->dual_iso = 1;
            break;
    }

    /* AMaZE averaging or mean23 */
    App->videoMLV->llrawproc->diso_averaging = [App->dualISOMethodOption selectedSegment];
    /* Alias map */
    App->videoMLV->llrawproc->diso_alias_map = ![App->aliasMapOption selectedSegment];
    /* Full res blending option */
    App->videoMLV->llrawproc->diso_frblending = ![App->fullResBlendingOption selectedSegment];

    mark_mlv_uncached(App->videoMLV);  if (!isMlvObjectCaching(App->videoMLV)) enableMlvCaching(App->videoMLV);// TEMPORARY

    App->frameChanged++;
}

-(void)patternNoiseMethod
{
    if (!App->videoMLV) return;
    App->videoMLV->llrawproc->pattern_noise = [App->patternNoiseOption selectedSegment];
    mark_mlv_uncached(App->videoMLV);  if (!isMlvObjectCaching(App->videoMLV)) enableMlvCaching(App->videoMLV);// TEMPORARY
    App->frameChanged++;
}

-(void)verticalStripeMethod
{
    if (!App->videoMLV) return;
    App->videoMLV->llrawproc->vertical_stripes = [App->stripeFixOption selectedSegment];
    App->videoMLV->llrawproc->compute_stripes = [App->stripeFixOption selectedSegment] ? 1 : 0;
    mark_mlv_uncached(App->videoMLV);  if (!isMlvObjectCaching(App->videoMLV)) enableMlvCaching(App->videoMLV);// TEMPORARY
    App->frameChanged++;
}

-(void)focusPixelMethod
{
    if (!App->videoMLV) return;
    App->videoMLV->llrawproc->focus_pixels = [App->focusPixelOption selectedSegment];
    App->videoMLV->llrawproc->fpi_method = [App->focusPixelMethodOption selectedSegment];
    mark_mlv_uncached(App->videoMLV);  if (!isMlvObjectCaching(App->videoMLV)) enableMlvCaching(App->videoMLV);// TEMPORARY
    App->frameChanged++;
}

-(void)badPixelMethod
{
    if (!App->videoMLV) return;
    App->videoMLV->llrawproc->bad_pixels = [App->badPixelOption selectedSegment];
    App->videoMLV->llrawproc->bpi_method = [App->badPixelMethodOption selectedSegment];
    mark_mlv_uncached(App->videoMLV);  if (!isMlvObjectCaching(App->videoMLV)) enableMlvCaching(App->videoMLV);// TEMPORARY
    App->frameChanged++;
}

-(void)chromaSmoothMethod
{
    if (!App->videoMLV) return;
    switch ([App->chromaSmoothOption selectedSegment])
    {
        case 0: /* Is off */
            App->videoMLV->llrawproc->chroma_smooth = 0;
            break;
        case 1: /* 2x2 */
            App->videoMLV->llrawproc->chroma_smooth = 2;
            break;
        case 2: /* 3x3 */
            App->videoMLV->llrawproc->chroma_smooth = 3;
            break;
        case 3: /* 5x5 */
            App->videoMLV->llrawproc->chroma_smooth = 5;
            break;
    }
    mark_mlv_uncached(App->videoMLV);  if (!isMlvObjectCaching(App->videoMLV)) enableMlvCaching(App->videoMLV);// TEMPORARY
    App->frameChanged++;
}

/* Select tab (Processing, LLRawProc... etc + more in the future) */
-(void)toggleTab
{
    BOOL showLLRawProc;
    BOOL showProcessing;

    switch ([self selectedSegment])
    {
        case 0: /* LLRawProc Tab */
            showLLRawProc = NO;
            showProcessing = YES;
            break;
        case 1: /* Processing Tab */
            showLLRawProc = YES;
            showProcessing = NO;
            break;
    }

    /* 
     * Processing Tab
     */

    /* Now show/hide things, yes this is ugly. Cocoa GUI code has become a mess */
    [App->exposureSlider setHidden: showProcessing];
    [App->saturationSlider setHidden: showProcessing]; [App->kelvinSlider setHidden: showProcessing];
    [App->tintSlider setHidden: showProcessing]; [App->darkStrengthSlider setHidden: showProcessing];
    [App->darkRangeSlider setHidden: showProcessing]; [App->lightStrengthSlider setHidden: showProcessing];
    [App->lightRangeSlider setHidden: showProcessing]; [App->lightenSlider setHidden: showProcessing];
    [App->sharpnessSlider setHidden: showProcessing]; [App->chromaBlurSlider setHidden: showProcessing];
    /* Slider labels */
    [App->exposureLabel setHidden: showProcessing]; [App->exposureValueLabel setHidden: showProcessing];
    [App->saturationLabel setHidden: showProcessing]; [App->saturationValueLabel setHidden: showProcessing]; [App->kelvinLabel setHidden: showProcessing];
    [App->kelvinValueLabel setHidden: showProcessing]; [App->tintLabel setHidden: showProcessing]; [App->tintValueLabel setHidden: showProcessing];
    [App->darkStrengthLabel setHidden: showProcessing]; [App->darkStrengthValueLabel setHidden: showProcessing];
    [App->darkRangeLabel setHidden: showProcessing]; [App->darkRangeValueLabel setHidden: showProcessing];
    [App->lightStrengthLabel setHidden: showProcessing]; [App->lightStrengthValueLabel setHidden: showProcessing];
    [App->lightRangeLabel setHidden: showProcessing]; [App->lightRangeValueLabel setHidden: showProcessing];
    [App->lightenLabel setHidden: showProcessing]; [App->lightenValueLabel setHidden: showProcessing];
    [App->sharpnessLabel setHidden: showProcessing]; [App->sharpnessValueLabel setHidden: showProcessing];
    [App->chromaBlurLabel setHidden: showProcessing]; [App->chromaBlurValueLabel setHidden: showProcessing];
    /* Checkboxes and processing profile selector */
    [App->highlightReconstructionSelector setHidden: showProcessing];
    [App->alwaysUseAmazeSelector setHidden: showProcessing];
    [App->chromaSeparationSelector setHidden: showProcessing];
    /* Select image profile */
    [App->imageProfile setHidden: showProcessing];

    /* 
     * LLRawProc Tab
     */

    [App->fixRawSelector setHidden: showLLRawProc];
    [App->focusPixelLabel setHidden: showLLRawProc];
    [App->focusPixelOption setHidden: showLLRawProc];
    [App->focusPixelMethodLabel setHidden: showLLRawProc];
    [App->focusPixelMethodOption setHidden: showLLRawProc];
    [App->stripeFixLabel setHidden: showLLRawProc];
    [App->stripeFixOption setHidden: showLLRawProc];
    [App->chromaSmoothLabel setHidden: showLLRawProc];
    [App->chromaSmoothOption setHidden: showLLRawProc];
    [App->patternNoiseLabel setHidden: showLLRawProc];
    [App->patternNoiseOption setHidden: showLLRawProc];
    [App->badPixelLabel setHidden: showLLRawProc];
    [App->badPixelOption setHidden: showLLRawProc];
    [App->badPixelMethodLabel setHidden: showLLRawProc];
    [App->badPixelMethodOption setHidden: showLLRawProc];
    [App->dualISOLabel setHidden: showLLRawProc];
    [App->dualISOOption setHidden: showLLRawProc];
    [App->dualISOMethodLabel setHidden: showLLRawProc];
    [App->dualISOMethodOption setHidden: showLLRawProc];
    [App->fullResBlendingLabel setHidden: showLLRawProc];
    [App->fullResBlendingOption setHidden: showLLRawProc];
    [App->aliasMapLabel setHidden: showLLRawProc];
    [App->aliasMapOption setHidden: showLLRawProc];
}

@end
