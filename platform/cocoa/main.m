/* Main file for mac
 * Objective C gui. */

#import "Cocoa/Cocoa.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
/* Host name */
#include <unistd.h>

#include "main_methods.h"

/* UI related crap */
#include "gui_stuff/useful_methods.h"
#include "gui_stuff/app_design.h"
#include "gui_stuff/gui_macros.h"

/* PC info */
#include "mac_info.h"

/* MLV stuff */
#include "../../src/mlv_include.h"

/* Important stuff */
#include "background_thread.h"

/* This file is generated temorarily during compile time */
#include "app_window_title.h"


/* Here comes some very global variables */

/* The main video object that the app will use for
 * handling MLV videos and processing them */
mlvObject_t * videoMLV;
processingObject_t * processingSettings;

char * MLVClipName;

/* Holds rawBitmap inside it or something */
NSImage * rawImageObject;
/* Holds a (THE) processed frame that is displayed */
NSBitmapImageRep * rawBitmap;
/* Yes, displayed image will be 8 bit, as most monitors are */
uint8_t * rawImage;
/* The actual view that will display it */
NSImageView * previewWindow;
/* THE application window */
NSWindow * window;

/* ++ this on adjustments to redraw or on playback to go draw next frame */
int frameChanged;
/* What frame we r on */
int currentFrameIndex;
double frameSliderPosition;
/* To pause frame drawing, above 0 = paused */
int dontDraw;

/* How much cache */
int cacheSizeMB;


int main(int argc, char * argv[])
{
    /* Apple documentation says this is right way to do things */
    return NSApplicationMain(argc, (const char **) argv);
}


int NSApplicationMain(int argc, const char * argv[])
{
    /* Just for easyness */
    MLVClipName = malloc(1);

    /* Don't draw as there's no clip loaded */
    dontDraw = 1;

    /* Don't know the purpose of this :[ */
    [NSApplication sharedApplication];

    /* Some stuff we may use */
    NSLog(@"Screen width: %.0f, height: %.0f", SCREEN_WIDTH, SCREEN_HEIGHT);
    NSLog(@"Physical RAM: %i MB", MAC_RAM);
    NSLog(@"CPU  threads: %i", MAC_CORES);

    /* Some style properties for the window... */
    NSUInteger windowStyle = NSTitledWindowMask | NSClosableWindowMask | NSResizableWindowMask 
        | NSMiniaturizableWindowMask | NSFullSizeContentViewWindowMask;

    /* Make the window */
    window = [ [NSWindow alloc]
               initWithContentRect:
               /* Load window in center of screen */
               NSMakeRect( (SCREEN_WIDTH  -  WINDOW_WIDTH) / 2, 
                           (SCREEN_HEIGHT - WINDOW_HEIGHT) / 2,
                           (WINDOW_WIDTH), (WINDOW_HEIGHT) )
               styleMask: windowStyle
               backing: NSBackingStoreBuffered
               defer: NO ];

    /* Make minimum size */
    [window setMinSize: NSMakeSize(WINDOW_WIDTH_MINIMUM, WINDOW_HEIGHT_MINIMUM)];


    /* App title with build info - a generated macro during compilation */
    [window setTitle: @APP_WINDOW_TITLE];


    /* If DARK_STYLE is true set window to dark theme 
     * Settings are in app_design.h */
    #if DARK_STYLE == 1
    window.appearance = [NSAppearance appearanceNamed: NSAppearanceNameVibrantDark];
    #elif DARK_STYLE == 0 
    // window.appearance = [NSAppearance appearanceNamed: NSAppearanceNameVibrantLight];
    #endif

    /* Remove titlebar */
    window.titlebarAppearsTransparent = true;

    /* Processing style selector */
    NSPopUpButton * processingStyle = [
        [NSPopUpButton alloc]
        initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(0,24,19) )
    ];
    [processingStyle anchorRight: YES];
    [processingStyle anchorTop: YES];
    [processingStyle addItemWithTitle: @"Faithful"];
    // [processingStyle addItemWithTitle: @"Milo"];
    [[window contentView] addSubview: processingStyle];

    /* Yes, macros -  Az u can tell by the capietals.
     * I don't want to add hundreds of lines of Objective C 
     * to the world, just to make some buttons for some app */

    /*
     *******************************************************************************
     * RIGHT SIDEBAR STUFF
     *******************************************************************************
     */

    /* First block of sliders */
    CREATE_SLIDER_RIGHT( exposureSlider, exposureLabel, exposureValueLabel, @"Exposure", 1, exposureSliderMethod, 0, 0.5 );
    CREATE_SLIDER_RIGHT( saturationSlider, saturationLabel, saturationValueLabel, @"Saturation", 2, saturationSliderMethod, 0, 0.5 );
    CREATE_SLIDER_RIGHT( kelvinSlider, kelvinLabel, kelvinValueLabel, @"Temperature", 3, kelvinSliderMethod, 0, 0.5 );
    CREATE_SLIDER_RIGHT( tintSlider, tintLabel, tintValueLabel, @"Tint", 4, tintSliderMethod, 0, 0.5 );

    /* Second block of sliders */
    CREATE_SLIDER_RIGHT( darkStrengthSlider, darkStrengthLabel, darkStrengthValueLabel, @"Dark Strength", 5, darkStrengthMethod, BLOCK_OFFSET, 0.23 );
    CREATE_SLIDER_RIGHT( darkRangeSlider, darkRangeLabel, darkRangeValueLabel, @"Dark Range", 6, darkRangeMethod, BLOCK_OFFSET, 0.73 );
    CREATE_SLIDER_RIGHT( lightStrengthSlider, lightStrengthLabel, lightStrengthValueLabel, @"Light Strength", 7, lightStrengthMethod, BLOCK_OFFSET, 0.0 );
    CREATE_SLIDER_RIGHT( lightRangeSlider, lightRangeLabel, lightRangeValueLabel, @"Light Range", 8, lightRangeMethod, BLOCK_OFFSET, 0.5 );
    CREATE_SLIDER_RIGHT( lightenSlider, lightenLabel, lightenValueLabel, @"Lighten", 9, lightenMethod, BLOCK_OFFSET, 0.0 );

    /* Third block */
    //CREATE_SLIDER_RIGHT( sharpnessSlider, sharpnessLabel, sharpnessValueLabel, @"Sharpen", 10, sharpnessMethod, BLOCK_OFFSET * 2, 0.0 );
    /* Maybe we won't have sharpness */

    /* Enable/disable highlight reconstruction */
    NSButton * highlightReconstructionSelector = [ [NSButton alloc] 
                                                   initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(11, ELEMENT_HEIGHT, 16) )];
    [highlightReconstructionSelector setButtonType: NSSwitchButton];
    [highlightReconstructionSelector setTitle: @"Highlight Reconstruction"];
    [highlightReconstructionSelector anchorRight: YES];
    [highlightReconstructionSelector anchorTop: YES];
    [highlightReconstructionSelector setTarget: highlightReconstructionSelector];
    [highlightReconstructionSelector setAction: @selector(toggleHighlightReconstruction)];
    [[window contentView] addSubview: highlightReconstructionSelector];

    /* To set always use AMaZE on/off */
    NSButton * alwaysUseAmazeSelector = [ [NSButton alloc] 
                                          initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(12, ELEMENT_HEIGHT, 30) )];
    [alwaysUseAmazeSelector setButtonType: NSSwitchButton];
    [alwaysUseAmazeSelector setTitle: @"Always use AMaZE"];
    [alwaysUseAmazeSelector anchorRight: YES];
    [alwaysUseAmazeSelector anchorTop: YES];
    [alwaysUseAmazeSelector setTarget: alwaysUseAmazeSelector];
    [alwaysUseAmazeSelector setAction: @selector(toggleAlwaysAmaze)];
    [[window contentView] addSubview: alwaysUseAmazeSelector];

    /*
     *******************************************************************************
     * LEFT SIDEBAR STUFF
     *******************************************************************************
     */

    /* Open MLV file button */
    CREATE_BUTTON_LEFT_TOP( openMLVButton, 0, openMlvDialog, 0, @"Open MLV File" );
    /* Export an image sequence (temporary) - these buttons look awkward and awful :[ */
    // CREATE_BUTTON_LEFT_BOTTOM( exportJpegSequenceButton, 1, exportJpegSequence, 1, @"Export JPEG Sequence" );
    // CREATE_BUTTON_LEFT_BOTTOM( exportPngSequenceButton, 0, exportPngSequence, 1, @"Export PNG Sequence" );
    CREATE_BUTTON_LEFT_BOTTOM( exportProRes4444Button, 0, exportProRes4444, 1, @"Export ProRes 4444" );
    /* Black level user input/adjustment */
    // CREATE_INPUT_WITH_LABEL_LEFT( blackLevelEntry, 1, blackLevelSet, 0, @"Black Level:" );

    /*
     *******************************************************************************
     * MLV AND PROCESSING STUFF
     *******************************************************************************
     */

    /* Initialise the MLV object so it is actually useful */
    videoMLV = initMlvObject();
    /* Intialise the processing settings object */
    processingSettings = initProcessingObject();
    /* Allow highlight reconstruction */
    processingDisableHighlightReconstruction(processingSettings);
    /* Set exposure to + 1.2 stops instead of correct 0.0, this is to give the impression 
     * (to those that believe) that highlights are recoverable (shhh don't tell) */
    processingSetExposureStops(processingSettings, 1.2);
    /* Link video with processing settings */
    setMlvProcessing(videoMLV, processingSettings);
    /* Limit frame cache to suitable amount of RAM (~33% at 8GB and below, ~50% at 16GB, then up and up) */
    cacheSizeMB = (int)(0.66666f * (float)(MAC_RAM - 4000));
    if (MAC_RAM < 7500) cacheSizeMB = MAC_RAM * 0.33;
    NSLog(@"Cache size = %iMB, or %i percent of RAM", cacheSizeMB, (int)((float)cacheSizeMB / (float)MAC_RAM * 100));
    setMlvRawCacheLimitMegaBytes(videoMLV, cacheSizeMB);

    /*
     *******************************************************************************
     * SETTING UP IMAGE VIEW AND STUFF (with many layers of cocoa)
     *******************************************************************************
     */

    /* ...lets start at 5D2 resolution because that's my camera */

    rawImage = malloc( 1880 * 1056 * 3 * sizeof(uint8_t) ); 

    /* NSBitmapImageRep lets you display bitmap data n stuff in CrApple things like NSImageView */
    rawBitmap = [ [NSBitmapImageRep alloc] 
                  initWithBitmapDataPlanes: (unsigned char * _Nullable * _Nullable)&rawImage 
                  /* initWithBitmapDataPlanes: NULL */
                  pixelsWide: 1880
                  pixelsHigh: 1056
                  bitsPerSample: 8
                  samplesPerPixel: 3
                  hasAlpha: NO 
                  isPlanar: NO 
                  /* Colour spaces: NSDeviceRGBColorSpace NSCalibratedRGBColorSpace */
                  colorSpaceName: @"NSDeviceRGBColorSpace"
                  bitmapFormat: 0
                  /* every pixel = 1 byte * 3 channels */
                  bytesPerRow: 1880 * 3
                  bitsPerPixel: 24 ];


    /* Will display our video */
    previewWindow = [ [NSImageView alloc]
                      initWithFrame: NSMakeRect(PREVIEW_WINDOW_LOCATION) ];

    /* Bezel alternatives: NSImageFrameGrayBezel NSImageFrameNone */
    [previewWindow setImageFrameStyle: NSImageFrameGrayBezel];
    [previewWindow setImageAlignment: NSImageAlignCenter];
    /* Scaling alternatives: NSScaleToFit - NSImageScaleProportionallyDown - NSScaleNone */
    [previewWindow setImageScaling: NSImageScaleProportionallyDown];
    /* NSImageView doesn't need to be anchored for some reason, just works anyway */
    [previewWindow setAutoresizingMask: (NSViewHeightSizable | NSViewWidthSizable) ];
    // [previewWindow setTarget:previewWindow];

    rawImageObject = [[NSImage alloc] initWithSize: NSMakeSize(1880,1056) ];
    [rawImageObject addRepresentation:rawBitmap];

    [previewWindow setImage: rawImageObject];
    [[window contentView] addSubview: previewWindow];

    /* Slider for moving thourhg the clip */
    NSSlider * timelineSlider = [
        [NSSlider alloc]
        initWithFrame: NSMakeRect( TIMELINE_SLIDER_LOCATION )
    ];
    [timelineSlider setTarget: timelineSlider];
    [timelineSlider setAction: @selector(timelineSliderMethod)];
    [timelineSlider setDoubleValue: 0.0];
    [timelineSlider anchorRight: YES];
    [timelineSlider anchorLeft: YES];
    [timelineSlider setAutoresizingMask: NSViewWidthSizable ];
    [[window contentView] addSubview: timelineSlider];


    /* If commandline arguments were used load clip... */
    if (argc > 1)
    {
        /* Hardcoded for now as 1st argument is clip */
        char * mlvPath = (char *)argv[1];
        char * mlvName = mlvPath;
        char * extension = mlvName + strlen(mlvPath) - 3;
        int clipNameStart = strlen(mlvPath) - 1;

        /* Point to just name */
        while (mlvPath[clipNameStart] != '/')
        {
            mlvName = mlvPath + clipNameStart;
            clipNameStart--;
        }

        /* Only allow if file has MLV extension */
        if ( (extension[0] == 'm' && extension[1] == 'l' && extension[2] == 'v') ||
             (extension[0] == 'M' && extension[1] == 'L' && extension[2] == 'V')  )
        {
            setAppNewMlvClip(mlvPath, mlvName);
        }
    }


    /* Start the FPS timer on background thread */
    beginFrameDrawing();


    /* Show the window or something */
    [window orderFrontRegardless];
    [NSApp run];

    return 0;
}
