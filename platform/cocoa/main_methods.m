/* Methods for user interface interactions 
 * this is where some real code goes */

#include <math.h>
#include <string.h>
#include <unistd.h>

#import <Cocoa/Cocoa.h>

#include "gui_stuff/app_design.h"

#include "main_methods.h"
#include "session_methods.h"
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

/* Exports clip currently in GUI */
void exportCurrentClip(char * folderPath)
{
    NSLog(@"Exporting %s...", App->MLVClipName);
    char exportPath[2048];
    snprintf(exportPath, 2048, "%s/%.8s.mov", folderPath, App->MLVClipName);

    int useAMaZE = (doesMlvAlwaysUseAmaze(App->videoMLV));
    setMlvAlwaysUseAmaze(App->videoMLV);

    double frameRate;
    switch ([App->exportFramerate indexOfSelectedItem])
    {
        case 0:
            frameRate = getMlvFramerate(App->videoMLV);
            if (frameRate < 26.0) goto fps23_976;
            else if (frameRate < 40.0) goto fps29_70;
            else if (frameRate < 55.0) goto fps50;
            else goto fps60;
        case 1:
            fps23_976:
            frameRate = 24000.0/1001.0; break;
        case 2:
            frameRate = 24.0; break;
        case 3:
            fps29_70:
            frameRate = 30000.0/1001.0; break;
        case 4:
            frameRate = 30.0; break;
        case 5:
            fps50:
            frameRate = 50.0; break;
        case 6:
            fps60:
            frameRate = 60.0; break;
        case 7:
            frameRate = getMlvFramerate(App->videoMLV); break;
    }

    int codec;
    switch ([App->exportFormat indexOfSelectedItem])
    {
        case 0:
            codec = AVF_CODEC_PRORES_422; break;
        case 1:
            codec = AVF_CODEC_PRORES_4444; break;
        case 2:
            codec = AVF_CODEC_H264; break;
        case 3:
            codec = AVF_CODEC_HEVC; break;
    }

    AVEncoder_t * encoder = initAVEncoder( getMlvWidth(App->videoMLV),
                                           getMlvHeight(App->videoMLV),
                                           codec,
                                           AVF_COLOURSPACE_SRGB,
                                           frameRate );

    beginWritingVideoFile(encoder, exportPath);

    for (uint64_t f = 0; f < getMlvFrames(App->videoMLV); ++f)
    {
        getMlvProcessedFrame16(App->videoMLV, f, App->rawImage, 1);
        addFrameToVideoFile(encoder, App->rawImage);
    }

    endWritingVideoFile(encoder);
    freeAVEncoder(encoder);

    NSLog(@"Exported %s...", App->MLVClipName);

    if (!useAMaZE) setMlvDontAlwaysUseAmaze(App->videoMLV);
}

/* Also will be called when switching between clips in session */
void syncGUI()
{
    int dd = App->dontDraw;
    App->dontDraw = 0;
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
    [App->sessionTabSwitch toggleTabLeft];
    [App->fixRawSelector toggleLLRawProc];
    [App->dualISOOption dualISOMethod];
    [App->focusPixelOption focusPixelMethod];
    [App->focusPixelMethodOption focusPixelMethod];
    [App->badPixelOption badPixelMethod];
    [App->badPixelMethodOption badPixelMethod];
    [App->stripeFixOption verticalStripeMethod];
    [App->chromaSmoothOption chromaSmoothMethod];
    [App->patternNoiseOption patternNoiseMethod];
    [App->chromaSeparationSelector toggleChromaSeparation];
    [App->imageProfile toggleImageProfile];
    App->dontDraw = dd;
}

/* Sets app to have no open clip currently */
void setAppCurrentClipNoClip()
{
    App->dontDraw = 1;
    memset(App->rawImage, 0, sizeof(uint16_t) * 3 * getMlvWidth(App->videoMLV) * getMlvHeight(App->videoMLV)); // Set dark image
    [App->previewWindow updateView];
    [App->window setTitle: [NSString stringWithFormat: @APP_NAME]];
    freeMlvObject(App->videoMLV);
    App->videoMLV = initMlvObject();
    setMlvProcessing(App->videoMLV, App->processingSettings);
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

    strcpy(App->MLVClipName, mlvFileName);

    /* Set app name to include MLV clip's name */
    [App->window setTitle: [NSString stringWithFormat: @ APP_NAME " | %s", mlvFileName]];

    /* Don't allow drawing frames, incase of adjustments during loading */
    App->dontDraw = 1;

    /* Destroy it just for simplicity... and make a new one */
    freeMlvObject(App->videoMLV);

    /* Create a NEW object with a NEW MLV clip! */
    int err = 0;char
    eRrOrR[6969];
    App->videoMLV = initMlvObjectWithClip( (char *)mlvPath, 0, &err, eRrOrR );

    /* If use has terminal this is useful */
    printMlvInfo(App->videoMLV);

    /* This needs to be joined (or segmentation fault 11 :D) */
    setMlvProcessing(App->videoMLV, App->processingSettings);

    /* Limit frame cache to amount of RAM we decided earlier */
    setMlvRawCacheLimitMegaBytes(App->videoMLV, (uint64_t)(App->cacheSizeMB));
    /* Tell it slightly less cores than we have, so background caching does not slow down UI interaction */
    setMlvCpuCores(App->videoMLV, (MAC_CORES / 2 + 1));

    /* Size may need changing */
    free(App->rawImage);
    App->rawImage = malloc( sizeof(uint16_t) * 3 * getMlvWidth(App->videoMLV) * getMlvHeight(App->videoMLV) );

    [App->previewWindow setSourceImage:App->rawImage width:getMlvWidth(App->videoMLV) height:getMlvHeight(App->videoMLV) bitDepth:16];

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
    setCurrentClipTouched();
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
    setCurrentClipTouched();
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
    setCurrentClipTouched();
}

/* Enable/disable LLRawProc */
-(void)toggleLLRawProc
{
    if ([self state] == NSOnState) 
    {
        llrpSetFixRawMode(App->videoMLV, FR_ON);
    }
    else 
    {
        llrpSetFixRawMode(App->videoMLV, FR_OFF);
    }
    resetMlvCache(App->videoMLV);
    IMPORTANT_CODE("",5);
    App->frameChanged++;
    setCurrentClipTouched();
}

/* Open file dialog + set new MLV clip */
-(void)openMlvDialog
{
    /* Create open panel */
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setCanChooseFiles: YES];
    [panel setCanChooseDirectories: NO];
    [panel setAllowsMultipleSelection: YES];

    /* Can only choose MLV files */
    [panel setAllowedFileTypes: [NSArray arrayWithObject: @"mlv"]];

    [panel beginSheetModalForWindow:App->window completionHandler: ^(NSInteger result)
    {
        if (result == NSFileHandlingPanelOKButton)
        {
            /* What stackoverflow said */
            for (NSURL * fileURL in [panel URLs])
            {
                const char * mlvPathString = [fileURL.path UTF8String];
                sessionAddNewMlvClip((char *)mlvPathString);
                IMPORTANT_CODE("Well done, you opened an MLV file.",2);
            }
            [App->session.clipTable reloadData];
            setAppGUIFromClip(App->session.clipInfo + App->session.clipCount-1);
        }
    } ];
}

/* Open a MASXML and the clips from it */
-(void)openSessionDialog
{
    /* Create open panel */
    NSOpenPanel * panel = [[NSOpenPanel openPanel] retain];

    [panel setCanChooseFiles: YES];
    [panel setCanChooseDirectories: NO];
    [panel setAllowsMultipleSelection: NO];

    /* Can only choose MLV files */
    [panel setAllowedFileTypes: [NSArray arrayWithObject: @"masxml"]];

    [panel beginSheetModalForWindow:App->window completionHandler: ^(NSInteger result)
    {
        if (result == NSFileHandlingPanelOKButton)
        {
            /* What stackoverflow said */
            for (NSURL * fileURL in [panel URLs])
            {
                const char * path = [fileURL.path UTF8String];
                appClearSession();
                appLoadSession((char *)path);
                IMPORTANT_CODE("A session has been opened.",2);
            }
        }
        [panel release];
    } ];
}

/* Save Session */
-(void)saveSessionDialog
{
    /* First update clip info for current clip to make sure it saves */
    saveClipInfo(App->session.clipInfo + App->session.currentClip);

    /* Create save panel */
    NSSavePanel * panel = [NSSavePanel savePanel];

    [panel beginSheetModalForWindow:App->window completionHandler: ^(NSInteger result) {
        if (result == NSFileHandlingPanelOKButton)
        {
            NSURL * fileURL = [panel URL];
            const char * sessionPath = [fileURL.path UTF8String];
            char name[4096];
            strncpy(name,sessionPath,4096);
            strcpy(name + strlen(name), ".masxml");
            appWriteSession((char *)name);
            IMPORTANT_CODE("Saved session.",2);
        }
    }];
}


/* Exports current clip */
-(void)exportCurrentClip
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

        [panel beginSheetModalForWindow:App->window completionHandler: ^(NSInteger result) 
        {
            if (result == NSFileHandlingPanelOKButton)
            {
                for (NSURL * pathURL in [panel URLs])
                {
                    char * directoryPath = (char *)[pathURL.path UTF8String];

                    exportCurrentClip(directoryPath);

                    syncGUI();

                    /* Give notification to user */
                    NSUserNotification * notification = [[[NSUserNotification alloc] init] autorelease];
                    notification.title = @APP_NAME;
                    notification.informativeText = [NSString stringWithFormat:@"Finished exporting."];
                    notification.soundName = NSUserNotificationDefaultSoundName;
                    [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
                }
            }
        } ];
    }
}

/* Exports current clip */
-(void)exportAllClips
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

        [panel beginSheetModalForWindow:App->window completionHandler: ^(NSInteger result) 
        {
            if (result == NSFileHandlingPanelOKButton)
            {
                for (NSURL * pathURL in [panel URLs])
                {
                    char * directoryPath = (char *)[pathURL.path UTF8String];

                    for (int c = 0; c < App->session.clipCount; ++c)
                    {
                        setAppGUIFromClip(App->session.clipInfo + c);
                        exportCurrentClip(directoryPath);
                    }

                    syncGUI();

                    /* Give notification to user */
                    NSUserNotification * notification = [[[NSUserNotification alloc] init] autorelease];
                    notification.title = @APP_NAME;
                    notification.informativeText = [NSString stringWithFormat:@"Finished exporting."];
                    notification.soundName = NSUserNotificationDefaultSoundName;
                    [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
                }
            }
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
    setCurrentClipTouched();
}


/* Select processing image profile */
-(void)toggleVideoFormat
{
    /* Indexes of menu items correspond to defines of processing profiles */
    processingSetImageProfile(App->processingSettings, (int)[self indexOfSelectedItem]);
    App->frameChanged++;
}

/* Choose filter */
-(void)toggleFilter
{
    filterObjectSetFilter(App->processingSettings->filter, (int)[self indexOfSelectedItem]);
    App->frameChanged++;
}

@end


/* Slider methods */
@implementation NSSlider (mainMethods)

-(void)highlightsSliderMethod
{
    /* ±1.0 */
    processingSetHighlights(App->processingSettings, [self doubleValue]*2.0 - 1.0);

    if ((int)(([self doubleValue] - 0.5)*200.0) != 0)
        [App->highlightsValueLabel setStringValue: [NSString stringWithFormat:@"%+6i", (int)(([self doubleValue]-0.5)*200.0) ]];
    else
        [App->highlightsValueLabel setStringValue: [NSString stringWithFormat:@"%6i", 0]];

    App->frameChanged++;
}

-(void)shadowsSliderMethod
{
    processingSetShadows(App->processingSettings, [self doubleValue]*2.0 - 1.0);

    if ((int)(([self doubleValue] - 0.5)*200.0) != 0)
        [App->shadowsValueLabel setStringValue: [NSString stringWithFormat:@"%+6i", (int)(([self doubleValue]-0.5)*200.0) ]];
    else
        [App->shadowsValueLabel setStringValue: [NSString stringWithFormat:@"%6i", 0]];

    App->frameChanged++;
}

-(void)filterStrengthMethod
{
    filterObjectSetFilterStrength(App->processingSettings->filter, [self doubleValue]);
    // [App->filterStrengthLabel setStringValue: [NSString stringWithFormat:@"%+6i", (int)(([self doubleValue])*100.0)]];
    App->frameChanged++;
}

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

    exposureValueStops -= 1.2;

    if (!(exposureValueStops < 0.015 && exposureValueStops > -0.015))
        [App->exposureValueLabel setStringValue: [NSString stringWithFormat:@"%+6.2f", exposureValueStops]];
    else
        [App->exposureValueLabel setStringValue: [NSString stringWithFormat:@"%6.2f", 0.0]];

    App->frameChanged++;
    setCurrentClipTouched();
}

-(void)saturationSliderMethod 
{
    /* Make it so that saturation of 1.0(original) is in the middle 
     * of the slider, and it can go down to 0.0 and up to 3.6 */
    double saturationSliderValue = [self doubleValue] * 2.0;
    double saturationValue = pow( saturationSliderValue, log(3.6)/log(2.0) );

    /* Yea whatever */
    processingSetSaturation(App->processingSettings, saturationValue);

    if (!(saturationSliderValue < 1.015 && saturationSliderValue > 0.985))
        [App->saturationValueLabel setStringValue: [NSString stringWithFormat:@"%+6i", (int)((saturationSliderValue-1.0)*100.0)]];
    else
        [App->saturationValueLabel setStringValue: [NSString stringWithFormat:@"%6i", 0]];

    App->frameChanged++;
    setCurrentClipTouched();
}

-(void)kelvinSliderMethod 
{
    /* Slider has to be in range 2000 - 10000, cos my function is limited to that */
    double kelvinValue = [self doubleValue] * (KELVIN_MAX - KELVIN_MIN) + KELVIN_MIN;

    /* Set processing object white balance */
    processingSetWhiteBalanceKelvin(App->processingSettings, kelvinValue);

    [App->kelvinValueLabel setStringValue: [NSString stringWithFormat:@"%5.ik", (int)kelvinValue]];

    App->frameChanged++;
    setCurrentClipTouched();
}

-(void)tintSliderMethod
{
    /* Slider has to be in range -10 to 10 */
    double tintValue = ([self doubleValue] - 0.5) * 20;

    /* Set processing object white balance */
    processingSetWhiteBalanceTint(App->processingSettings, tintValue);

    if ((int)(([self doubleValue] - 0.5)*200.0) != 0)
        [App->tintValueLabel setStringValue: [NSString stringWithFormat:@"%+6i", (int)(([self doubleValue]-0.5)*200.0) ]];
    else
        [App->tintValueLabel setStringValue: [NSString stringWithFormat:@"%6i", 0]];

    App->frameChanged++;
    setCurrentClipTouched();
}

/* All contrast/curve settings */
-(void)darkStrengthMethod {
    processingSetDCFactor(App->processingSettings, [self doubleValue] * 22.5);
    [App->darkStrengthValueLabel setStringValue: [NSString stringWithFormat:@"%6i", (int)([self doubleValue]*100.0)]];
    App->frameChanged++;
    setCurrentClipTouched();
} -(void)darkRangeMethod {
    processingSetDCRange(App->processingSettings, [self doubleValue]);
    [App->darkRangeValueLabel setStringValue: [NSString stringWithFormat:@"%6i", (int)([self doubleValue]*100.0)]];
    App->frameChanged++;
    setCurrentClipTouched();
} -(void)lightStrengthMethod {
    processingSetLCFactor(App->processingSettings, [self doubleValue] * 11.2);
    [App->lightStrengthValueLabel setStringValue: [NSString stringWithFormat:@"%6i", (int)([self doubleValue]*100.0)]];
    App->frameChanged++;
    setCurrentClipTouched();
} -(void)lightRangeMethod {
    processingSetLCRange(App->processingSettings, [self doubleValue]);
    [App->lightRangeValueLabel setStringValue: [NSString stringWithFormat:@"%6i", (int)([self doubleValue]*100.0)]];
    App->frameChanged++;
    setCurrentClipTouched();
} -(void)lightenMethod {
    processingSetLightening(App->processingSettings, [self doubleValue] * 0.6);
    [App->lightenValueLabel setStringValue: [NSString stringWithFormat:@"%6i", (int)([self doubleValue]*100.0)]];
    App->frameChanged++;
    setCurrentClipTouched();
}

-(void)sharpnessMethod
{
    double sharpnessValue = [self doubleValue];
    processingSetSharpening(App->processingSettings, sharpnessValue);
    [App->sharpnessValueLabel setStringValue: [NSString stringWithFormat:@"%6i", (int)([self doubleValue]*100.0)]];
    App->frameChanged++;
    setCurrentClipTouched();
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
    setCurrentClipTouched();
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
            llrpSetDualIsoMode(App->videoMLV, DISO_OFF);
            break;
        case 1: /* Is Set to high quality 20 bit */
            llrpSetDualIsoMode(App->videoMLV, DISO_20BIT);
            break;
        case 2: /* Is set to quick low quality mode */
            llrpSetDualIsoMode(App->videoMLV, DISO_FAST);
            break;
    }

    /* AMaZE averaging or mean23 */
    llrpSetDualIsoInterpolationMethod(App->videoMLV, [App->dualISOMethodOption selectedSegment] ? DISOI_AMAZE : DISOI_MEAN23);
    /* Alias map */
    llrpSetDualIsoAliasMapMode(App->videoMLV, ![App->aliasMapOption selectedSegment]);
    /* Full res blending option */
    llrpSetDualIsoFullResBlendingMode(App->videoMLV, ![App->fullResBlendingOption selectedSegment]);

    resetMlvCache(App->videoMLV);

    App->frameChanged++;
    setCurrentClipTouched();
}

-(void)patternNoiseMethod
{
    if (!App->videoMLV) return;
    switch ([App->patternNoiseOption selectedSegment])
    {
        case 0:
            llrpSetPatternNoiseMode(App->videoMLV, PN_OFF);
            break;
        case 1:
            llrpSetPatternNoiseMode(App->videoMLV, PN_ON);
            break;
    }
    resetMlvCache(App->videoMLV);
    App->frameChanged++;
    setCurrentClipTouched();
}

-(void)verticalStripeMethod
{
    if (!App->videoMLV) return;
    switch ([App->stripeFixOption selectedSegment])
    {
        case 0:
            llrpSetVerticalStripeMode(App->videoMLV, VS_OFF);
            return;
        case 1:
            llrpSetVerticalStripeMode(App->videoMLV, VS_ON);
            return;
        case 2:
            llrpSetVerticalStripeMode(App->videoMLV, VS_FORCE);
            return;
    }
    llrpComputeStripesOn(App->videoMLV);
    resetMlvCache(App->videoMLV);
    App->frameChanged++;
    setCurrentClipTouched();
}

-(void)focusPixelMethod
{
    if (!App->videoMLV) return;
    switch ([App->focusPixelOption selectedSegment])
    {
        case 0:
            llrpSetFocusPixelMode(App->videoMLV, FP_OFF);
            return;
        case 1:
            llrpSetFocusPixelMode(App->videoMLV, FP_ON);
            return;
        case 2:
            llrpSetFocusPixelMode(App->videoMLV, FP_CROPREC);
            return;
    }
    llrpResetFpmStatus(App->videoMLV);
    llrpResetBpmStatus(App->videoMLV);
    switch ([App->focusPixelMethodOption selectedSegment])
    {
        case 0:
            llrpSetFocusPixelInterpolationMethod(App->videoMLV, FPI_MLVFS);
            return;
        case 1:
            llrpSetFocusPixelInterpolationMethod(App->videoMLV, FPI_RAW2DNG);
            return;
    }
    resetMlvCache(App->videoMLV);
    App->frameChanged++;
    setCurrentClipTouched();
}

-(void)badPixelMethod
{
    if (!App->videoMLV) return;
    switch ([App->badPixelOption selectedSegment])
    {
        case 0:
            llrpSetBadPixelMode(App->videoMLV, BP_OFF);
            break;
        case 1:
            llrpSetBadPixelMode(App->videoMLV, BP_ON);
            break;
        case 2:
            llrpSetBadPixelMode(App->videoMLV, FP_AGGRESSIVE);
            break;
    }
    switch ([App->badPixelMethodOption selectedSegment])
    {
        case 0:
            llrpSetBadPixelInterpolationMethod(App->videoMLV, BPI_MLVFS);
            break;
        case 1:
            llrpSetBadPixelInterpolationMethod(App->videoMLV, BPI_RAW2DNG);
            break;
    }
    llrpResetBpmStatus(App->videoMLV);
    resetMlvCache(App->videoMLV);
    App->frameChanged++;
    setCurrentClipTouched();
}

-(void)chromaSmoothMethod
{
    if (!App->videoMLV) return;
    switch ([App->chromaSmoothOption selectedSegment])
    {
        case 0: /* Is off */
            llrpSetChromaSmoothMode(App->videoMLV, CS_OFF);
            break;
        case 1: /* 2x2 */
            llrpSetChromaSmoothMode(App->videoMLV, CS_2x2);
            break;
        case 2: /* 3x3 */
            llrpSetChromaSmoothMode(App->videoMLV, CS_3x3);
            break;
        case 3: /* 5x5 */
            llrpSetChromaSmoothMode(App->videoMLV, CS_5x5);
            break;
    }
    resetMlvCache(App->videoMLV);
    App->frameChanged++;
    setCurrentClipTouched();
}

-(void)toggleTabLeft
{
    BOOL showClips;
    BOOL showExport;

    switch ([self selectedSegment])
    {
        case 0: /* Clip Tab */
            showClips = NO;
            showExport = YES;
            break;
        case 1: /* Export Tab */
            showClips = YES;
            showExport = NO;
            break;
    }

    [App->openMLVButton setHidden: showClips];
    [App->openSessionButton setHidden: showClips];
    [App->saveSessionButton setHidden: showClips];
    [App->session.tableContainer setHidden: showClips];

    [App->exportCurrentClipButton setHidden: showExport];
    [App->exportFormatLabel setHidden: showExport];
    [App->exportFormat setHidden: showExport];
    [App->exportFramerateLabel setHidden: showExport];
    [App->exportFramerate setHidden: showExport];
}

/* Select tab (Processing, LLRawProc... etc + more in the future) */
-(void)toggleTab
{
    BOOL hideLLRawProc;
    BOOL hideProcessing;
    BOOL hideFilter;

    switch ([self selectedSegment])
    {
        case 0: /* LLRawProc Tab */
            hideLLRawProc = NO;
            hideProcessing = YES;
            hideFilter = YES;
            break;
        case 1: /* Processing Tab */
            hideLLRawProc = YES;
            hideProcessing = NO;
            hideFilter = YES;
            break;
        case 2: /* Filter Tab */
            hideLLRawProc = YES;
            hideProcessing = YES;
            hideFilter = NO;
            break;
    }

    /* 
     * Processing Tab
     */

    /* Now show/hide things, yes this is ugly. Cocoa GUI code has become a mess */
    [App->exposureSlider setHidden: hideProcessing];
    [App->saturationSlider setHidden: hideProcessing]; [App->kelvinSlider setHidden: hideProcessing];
    [App->tintSlider setHidden: hideProcessing]; [App->darkStrengthSlider setHidden: hideProcessing];
    [App->darkRangeSlider setHidden: hideProcessing]; [App->lightStrengthSlider setHidden: hideProcessing];
    [App->lightRangeSlider setHidden: hideProcessing]; [App->lightenSlider setHidden: hideProcessing];
    [App->sharpnessSlider setHidden: hideProcessing]; [App->chromaBlurSlider setHidden: hideProcessing];
    /* Slider labels */
    [App->exposureLabel setHidden: hideProcessing]; [App->exposureValueLabel setHidden: hideProcessing];
    [App->saturationLabel setHidden: hideProcessing]; [App->saturationValueLabel setHidden: hideProcessing]; [App->kelvinLabel setHidden: hideProcessing];
    [App->kelvinValueLabel setHidden: hideProcessing]; [App->tintLabel setHidden: hideProcessing]; [App->tintValueLabel setHidden: hideProcessing];
    [App->darkStrengthLabel setHidden: hideProcessing]; [App->darkStrengthValueLabel setHidden: hideProcessing];
    [App->darkRangeLabel setHidden: hideProcessing]; [App->darkRangeValueLabel setHidden: hideProcessing];
    [App->lightStrengthLabel setHidden: hideProcessing]; [App->lightStrengthValueLabel setHidden: hideProcessing];
    [App->lightRangeLabel setHidden: hideProcessing]; [App->lightRangeValueLabel setHidden: hideProcessing];
    [App->lightenLabel setHidden: hideProcessing]; [App->lightenValueLabel setHidden: hideProcessing];
    [App->sharpnessLabel setHidden: hideProcessing]; [App->sharpnessValueLabel setHidden: hideProcessing];
    [App->chromaBlurLabel setHidden: hideProcessing]; [App->chromaBlurValueLabel setHidden: hideProcessing];
    /* Checkboxes and processing profile selector */
    [App->highlightReconstructionSelector setHidden: hideProcessing];
    [App->alwaysUseAmazeSelector setHidden: hideProcessing];
    [App->chromaSeparationSelector setHidden: hideProcessing];
    /* Select image profile */
    [App->imageProfile setHidden: hideProcessing];

    /* 
     * LLRawProc Tab
     */

    [App->fixRawSelector setHidden: hideLLRawProc];
    [App->focusPixelLabel setHidden: hideLLRawProc];
    [App->focusPixelOption setHidden: hideLLRawProc];
    [App->focusPixelMethodLabel setHidden: hideLLRawProc];
    [App->focusPixelMethodOption setHidden: hideLLRawProc];
    [App->stripeFixLabel setHidden: hideLLRawProc];
    [App->stripeFixOption setHidden: hideLLRawProc];
    [App->chromaSmoothLabel setHidden: hideLLRawProc];
    [App->chromaSmoothOption setHidden: hideLLRawProc];
    [App->patternNoiseLabel setHidden: hideLLRawProc];
    [App->patternNoiseOption setHidden: hideLLRawProc];
    [App->badPixelLabel setHidden: hideLLRawProc];
    [App->badPixelOption setHidden: hideLLRawProc];
    [App->badPixelMethodLabel setHidden: hideLLRawProc];
    [App->badPixelMethodOption setHidden: hideLLRawProc];
    [App->dualISOLabel setHidden: hideLLRawProc];
    [App->dualISOOption setHidden: hideLLRawProc];
    [App->dualISOMethodLabel setHidden: hideLLRawProc];
    [App->dualISOMethodOption setHidden: hideLLRawProc];
    [App->fullResBlendingLabel setHidden: hideLLRawProc];
    [App->fullResBlendingOption setHidden: hideLLRawProc];
    [App->aliasMapLabel setHidden: hideLLRawProc];
    [App->aliasMapOption setHidden: hideLLRawProc];

    /*
     * Filter tab
     */

    [App->filterLabel setHidden: hideFilter];
    [App->filterOptions setHidden: hideFilter];
    [App->filterStrengthSlider setHidden: hideFilter];
    [App->filterStrengthLabel setHidden: hideFilter];
}

@end
