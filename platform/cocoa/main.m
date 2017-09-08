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

/* God object used to share globals (type) */
#include "godobject.h"
/* The godobject itsself */
godObject_t * App;

/* App delegate */
#include "delegate.h"


/* Default image profile when app is loaded */
#define DEFAULT_IMAGE_PROFILE_APP PROFILE_TONEMAPPED


int main(int argc, char * argv[])
{
    /* Apple documentation says this is right way to do things */
    return NSApplicationMain(argc, (const char **) argv);
}


int NSApplicationMain(int argc, const char * argv[])
{
    /* http://www.gnustep.it/nicola/Tutorials/FirstGUIApplication/node3.html */
    [NSApplication sharedApplication];
    [NSApp setDelegate: [MLVAppDelegate new]];

    /* We make the god opbject */
    App = calloc(1,sizeof(godObject_t));

    /* Just for easyness */
    App->MLVClipName = malloc(1);

    /* Don't draw as there's no clip loaded */
    App->dontDraw = 1;

    /* Some stuff we may use */
    NSLog(@"Screen width: %.0f, height: %.0f", SCREEN_WIDTH, SCREEN_HEIGHT);
    NSLog(@"Physical RAM: %i MB", MAC_RAM);
    NSLog(@"CPU  threads: %i", MAC_CORES);

    /* Some style properties for the window... */
    NSUInteger windowStyle = NSTitledWindowMask | NSClosableWindowMask | NSResizableWindowMask 
        | NSMiniaturizableWindowMask | NSFullSizeContentViewWindowMask;

    /* Make the window */
    App->window = [ [NSWindow alloc]
                    initWithContentRect:
                    /* Load window in center of screen */
                    NSMakeRect( (SCREEN_WIDTH  -  WINDOW_WIDTH) / 2, 
                                (SCREEN_HEIGHT - WINDOW_HEIGHT) / 2,
                                (WINDOW_WIDTH), (WINDOW_HEIGHT) )
                    styleMask: windowStyle
                    backing: NSBackingStoreBuffered
                    defer: NO ];

    /* Make minimum size */
    [App->window setMinSize: NSMakeSize(WINDOW_WIDTH_MINIMUM, WINDOW_HEIGHT_MINIMUM)];


    /* App title with build info - a generated macro during compilation */
    [App->window setTitle: @APP_WINDOW_TITLE];


    /* If DARK_STYLE is true set window to dark theme 
     * Settings are in app_design.h */
    #if DARK_STYLE == 1
    App->window.appearance = [NSAppearance appearanceNamed: NSAppearanceNameVibrantDark];
    #elif DARK_STYLE == 0 
    // window.appearance = [NSAppearance appearanceNamed: NSAppearanceNameVibrantLight];
    #endif

    /* Remove titlebar */
    App->window.titlebarAppearsTransparent = true;

    /* Use of Objecive-C is minimised through massive macros */

    /*
     *******************************************************************************
     * RIGHT SIDEBAR STUFF
     *******************************************************************************
     */

    /* Processing style selector */
    App->imageProfile = [ [NSPopUpButton alloc]
                          initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(0,24,19) ) ];
    [App->imageProfile anchorRight: YES];
    [App->imageProfile anchorTop: YES];
    [App->imageProfile insertItemWithTitle: @"Standard" atIndex: PROFILE_STANDARD];
    [App->imageProfile insertItemWithTitle: @"Tonemapped" atIndex: PROFILE_TONEMAPPED];
    // [App->imageProfile insertItemWithTitle: @"Canon C-Log" atIndex: PROFILE_CANON_LOG];
    [App->imageProfile insertItemWithTitle: @"Alexa Log-C" atIndex: PROFILE_ALEXA_LOG];
    [App->imageProfile insertItemWithTitle: @"Cineon Log" atIndex: PROFILE_CINEON_LOG];
    [App->imageProfile insertItemWithTitle: @"Sony S-Log3" atIndex: PROFILE_SONY_LOG_3];
    [App->imageProfile insertItemWithTitle: @"Linear" atIndex: PROFILE_LINEAR];
    [App->imageProfile setTarget: App->imageProfile];
    [App->imageProfile setAction: @selector(toggleImageProfile)];
    [App->imageProfile selectItemAtIndex: DEFAULT_IMAGE_PROFILE_APP];
    [[App->window contentView] addSubview: App->imageProfile];

    /* First block of sliders */
    CREATE_SLIDER_RIGHT( App->exposureSlider, App->exposureLabel, App->exposureValueLabel, @"Exposure", 1, exposureSliderMethod, 0, 0.5 );
    CREATE_SLIDER_RIGHT( App->saturationSlider, App->saturationLabel, App->saturationValueLabel, @"Saturation", 2, saturationSliderMethod, 0, 0.5 );
    CREATE_SLIDER_RIGHT( App->kelvinSlider, App->kelvinLabel, App->kelvinValueLabel, @"Temperature", 3, kelvinSliderMethod, 0, 0.5 );
    CREATE_SLIDER_RIGHT( App->tintSlider, App->tintLabel, App->tintValueLabel, @"Tint", 4, tintSliderMethod, 0, 0.5 );

    /* Second block of sliders */
    CREATE_SLIDER_RIGHT( App->darkStrengthSlider, App->darkStrengthLabel, App->darkStrengthValueLabel, @"Dark Strength", 5, darkStrengthMethod, BLOCK_OFFSET, 0.23 );
    CREATE_SLIDER_RIGHT( App->darkRangeSlider, App->darkRangeLabel, App->darkRangeValueLabel, @"Dark Range", 6, darkRangeMethod, BLOCK_OFFSET, 0.73 );
    CREATE_SLIDER_RIGHT( App->lightStrengthSlider, App->lightStrengthLabel, App->lightStrengthValueLabel, @"Light Strength", 7, lightStrengthMethod, BLOCK_OFFSET, 0.0 );
    CREATE_SLIDER_RIGHT( App->lightRangeSlider, App->lightRangeLabel, App->lightRangeValueLabel, @"Light Range", 8, lightRangeMethod, BLOCK_OFFSET, 0.5 );
    CREATE_SLIDER_RIGHT( App->lightenSlider, App->lightenLabel, App->lightenValueLabel, @"Lighten", 9, lightenMethod, BLOCK_OFFSET, 0.0 );

    /* Third block */
    //CREATE_SLIDER_RIGHT( App->sharpnessSlider, sharpnessLabel, sharpnessValueLabel, @"Sharpen", 10, sharpnessMethod, BLOCK_OFFSET * 2, 0.0 );
    /* Maybe we won't have sharpness */

    /* Enable/disable highlight reconstruction */
    App->highlightReconstructionSelector = [ [NSButton alloc]
                                             initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(11, ELEMENT_HEIGHT, 16) )];
    [App->highlightReconstructionSelector setButtonType: NSSwitchButton];
    [App->highlightReconstructionSelector setTitle: @"Highlight Reconstruction"];
    [App->highlightReconstructionSelector anchorRight: YES];
    [App->highlightReconstructionSelector anchorTop: YES];
    [App->highlightReconstructionSelector setTarget: App->highlightReconstructionSelector];
    [App->highlightReconstructionSelector setAction: @selector(toggleHighlightReconstruction)];
    [[App->window contentView] addSubview: App->highlightReconstructionSelector];

    /* To set always use AMaZE on/off */
    App->alwaysUseAmazeSelector = [ [NSButton alloc]
                                    initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(12, ELEMENT_HEIGHT, 30) )];
    [App->alwaysUseAmazeSelector setButtonType: NSSwitchButton];
    [App->alwaysUseAmazeSelector setTitle: @"Always use AMaZE"];
    [App->alwaysUseAmazeSelector anchorRight: YES];
    [App->alwaysUseAmazeSelector anchorTop: YES];
    [App->alwaysUseAmazeSelector setTarget: App->alwaysUseAmazeSelector];
    [App->alwaysUseAmazeSelector setAction: @selector(toggleAlwaysAmaze)];
    [[App->window contentView] addSubview: App->alwaysUseAmazeSelector];

    /*
     *******************************************************************************
     * LEFT SIDEBAR STUFF
     *******************************************************************************
     */

    /* Open MLV file button */
    CREATE_BUTTON_LEFT_TOP( App->openMLVButton, 0, openMlvDialog, 0, @"Open MLV File" );
    // CREATE_BUTTON_LEFT_TOP( App->openMLVButton, 1, openMlvDialog, 6, @"Open Session" ); /* Commented out as not working yet */
    CREATE_BUTTON_LEFT_BOTTOM( App->exportProRes4444Button, 0, exportProRes4444, 1, @"Export ProRes 4444" );

    /* Export format selector */
    // App->videoFormat = [ [NSPopUpButton alloc]
    //                      initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(0,24,19) ) ];
    // [App->imageProfile anchorRight: YES];
    // [App->imageProfile anchorTop: YES];
    // [App->imageProfile insertItemWithTitle: @"ProRes 422 Proxy" atIndex: 0];
    // [App->imageProfile insertItemWithTitle: @"ProRes 422 LT" atIndex: 1];
    // [App->imageProfile insertItemWithTitle: @"ProRes 422 Normal" atIndex: 1];
    // [App->imageProfile insertItemWithTitle: @"ProRes 422 HQ" atIndex: 1];
    // [App->imageProfile setTarget: App->imageProfile];
    // [App->imageProfile setAction: @selector(toggleProResFormat)];
    // [App->imageProfile selectItemAtIndex: DEFAULT_IMAGE_PROFILE_APP];
    // [[App->window contentView] addSubview: App->imageProfile];

    /* NSTableView - List of all clips currently open (session) */
    // NSScrollView * tableContainer = [[NSScrollView alloc] initWithFrame:NSMakeRect(10, 10, 380, 200)];

    // NSTableColumn * column = [[NSTableColumn alloc] initWithIdentifier:@"id"];
    // App->clipTable = [ [NSTableView alloc] 
    //                    initWithFrame: NSMakeRect(10,200,100,100)];
    // [App->clipTable addTableColumn:column];
    // [[App->window contentView] addSubview: App->tonemappingSelector];

    /*
     *******************************************************************************
     * MLV AND PROCESSING STUFF
     *******************************************************************************
     */

    /* Initialise the MLV object so it is actually useful */
    App->videoMLV = initMlvObject();
    /* Intialise the processing settings object */
    App->processingSettings = initProcessingObject();
    /* Allow highlight reconstruction */
    processingDisableHighlightReconstruction(App->processingSettings);
    /* Set exposure to + 1.2 stops instead of correct 0.0, this is to give the impression 
     * (to those that believe) that highlights are recoverable (shhh don't tell) */
    processingSetExposureStops(App->processingSettings, 1.2);
    /* TEST */
    processingSetImageProfile(App->processingSettings, DEFAULT_IMAGE_PROFILE_APP);
    /* Link video with processing settings */
    setMlvProcessing(App->videoMLV, App->processingSettings);
    /* Limit frame cache to suitable amount of RAM (~33% at 8GB and below, ~50% at 16GB, then up and up) */
    App->cacheSizeMB = (int)(0.66666 * (double)(MAC_RAM - 4000));
    if (MAC_RAM < 7500) App->cacheSizeMB = MAC_RAM * 0.33;
    NSLog(@"Cache size = %iMB, or %i percent of RAM", App->cacheSizeMB, (int)((double)App->cacheSizeMB / (double)MAC_RAM * 100));
    setMlvRawCacheLimitMegaBytes(App->videoMLV, App->cacheSizeMB);

    /*
     *******************************************************************************
     * SETTING UP IMAGE VIEW AND STUFF (with many layers of cocoa)
     *******************************************************************************
     */

    /* ...lets start at 5D2 resolution because that's my camera */

    App->rawImage = malloc( 1880 * 1056 * 3 * sizeof(uint8_t) ); 

    /* NSBitmapImageRep lets you display bitmap data n stuff in CrApple things like NSImageView */
    App->rawBitmap = [ [NSBitmapImageRep alloc] 
                       initWithBitmapDataPlanes: (unsigned char * _Nullable * _Nullable)&App->rawImage 
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
    App->previewWindow = [ [NSImageView alloc]
                           initWithFrame: NSMakeRect(PREVIEW_WINDOW_LOCATION) ];

    /* Bezel alternatives: NSImageFrameGrayBezel NSImageFrameNone */
    [App->previewWindow setImageFrameStyle: NSImageFrameGrayBezel];
    [App->previewWindow setImageAlignment: NSImageAlignCenter];
    /* Scaling alternatives: NSScaleToFit - NSImageScaleProportionallyDown - NSScaleNone */
    [App->previewWindow setImageScaling: NSImageScaleProportionallyDown];
    /* NSImageView doesn't need to be anchored for some reason, just works anyway */
    [App->previewWindow setAutoresizingMask: (NSViewHeightSizable | NSViewWidthSizable) ];
    // [previewWindow setTarget:previewWindow];

    App->rawImageObject = [[NSImage alloc] initWithSize: NSMakeSize(1880,1056) ];
    [App->rawImageObject addRepresentation:App->rawBitmap];

    [App->previewWindow setImage: App->rawImageObject];
    [[App->window contentView] addSubview: App->previewWindow];

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
    [[App->window contentView] addSubview: timelineSlider];


    /* If commandline arguments were used load clip... */
    if (argc > 1)
    {
        setAppNewMlvClip((char *)argv[1]);
    }


    /* Init UI */
    initAppWithGod();


    /* Start the FPS timer on background thread */
    beginFrameDrawing();


    /* Show the window or something */
    [App->window orderFrontRegardless];
    [NSApp run];

    return 0;
}
