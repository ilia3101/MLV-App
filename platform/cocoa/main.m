/* Main file for mac
 * Objective C gui. */

#import <Cocoa/Cocoa.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "main_methods.h"

/* UI related crap */
#include "gui_stuff/useful_methods.h"
#include "gui_stuff/app_design.h"
#include "gui_stuff/gui_macros.h"

/* MacOS version */
#include "AvailabilityMacros.h"

/* PC info */
#include "mac_info.h"

/* MLV stuff */
#include "../../src/mlv_include.h"

/* Important stuff */
#include "background_thread.h"

/* This file is generated temorarily during compile time */
#include "app_defines.h"

/* God object used to share globals (type) */
#include "godobject.h"
/* The godobject itsself */
godObject_t * App;

/* App delegate */
#include "delegate.h"

/* MLV OpenGL based view */
#include "mlv_view.h"

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

    /* Set directory fo9r LLRawProc to get it's pixel maps */
    chdir((char *)[[[NSBundle mainBundle] pathForResource:@"pixelmaps" ofType: nil] UTF8String]);

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
    /* Dark style is only supported in OSX 10.10 and up */
    if (floor(kCFCoreFoundationVersionNumber) > kCFCoreFoundationVersionNumber10_9)
    {
        App->window.appearance = [NSAppearance appearanceNamed: NSAppearanceNameVibrantDark];
    }
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
    CREATE_SLIDER_RIGHT( App->sharpnessSlider, App->sharpnessLabel, App->sharpnessValueLabel, @"Sharpen", 10, sharpnessMethod, BLOCK_OFFSET * 1.62, 0.0 );
    CREATE_SLIDER_RIGHT( App->chromaBlurSlider, App->chromaBlurLabel, App->chromaBlurValueLabel, @"Chroma Blur Radius", 11, chromaBlurMethod, BLOCK_OFFSET * 1.62, 0.0 );

    /* Enable/disable highlight reconstruction */
    App->highlightReconstructionSelector = [ [NSButton alloc]
                                             initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(12, ELEMENT_HEIGHT, 16 + BLOCK_OFFSET*1.67) )];
    [App->highlightReconstructionSelector setButtonType: NSSwitchButton];
    [App->highlightReconstructionSelector setTitle: @"Highlight Reconstruction"];
    AnchorRight(App->highlightReconstructionSelector, YES);
    AnchorTop(App->highlightReconstructionSelector, YES);
    [App->highlightReconstructionSelector setTarget: App->highlightReconstructionSelector];
    [App->highlightReconstructionSelector setAction: @selector(toggleHighlightReconstruction)];
    [[App->window contentView] addSubview: App->highlightReconstructionSelector];

    /* To set always use AMaZE on/off */
    App->alwaysUseAmazeSelector = [ [NSButton alloc]
                                    initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(13, ELEMENT_HEIGHT, 30 + BLOCK_OFFSET*1.67) )];
    [App->alwaysUseAmazeSelector setButtonType: NSSwitchButton];
    [App->alwaysUseAmazeSelector setTitle: @"Always use AMaZE"];
    AnchorRight(App->alwaysUseAmazeSelector, YES);
    AnchorTop(App->alwaysUseAmazeSelector, YES);
    [App->alwaysUseAmazeSelector setTarget: App->alwaysUseAmazeSelector];
    [App->alwaysUseAmazeSelector setAction: @selector(toggleAlwaysAmaze)];
    [[App->window contentView] addSubview: App->alwaysUseAmazeSelector];

    /* To enable/disable chroma separation */
    App->chromaSeparationSelector = [ [NSButton alloc]
                                      initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(14, ELEMENT_HEIGHT, 44 + BLOCK_OFFSET*1.67) )];
    [App->chromaSeparationSelector setButtonType: NSSwitchButton];
    [App->chromaSeparationSelector setTitle: @"Chroma Separation (YCbCr)"];
    AnchorRight(App->chromaSeparationSelector, YES);
    AnchorTop(App->chromaSeparationSelector, YES);
    [App->chromaSeparationSelector setTarget: App->chromaSeparationSelector];
    [App->chromaSeparationSelector setAction: @selector(toggleChromaSeparation)];
    [[App->window contentView] addSubview: App->chromaSeparationSelector];


    /* Processing style selector */
    App->imageProfile = [ [NSPopUpButton alloc]
                          initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(14,24,-28) ) ];
    AnchorRight(App->imageProfile, YES);
    AnchorTop(App->imageProfile, YES);
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
    /* Default profiel */
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
    App->previewWindow = [ [MLVView alloc]
                           initWithFrame: NSMakeRect(PREVIEW_WINDOW_LOCATION) ];

    /* Bezel alternatives: NSImageFrameGrayBezel NSImageFrameNone */
    [App->previewWindow setImageFrameStyle: NSImageFrameGrayBezel];
    [App->previewWindow setImageAlignment: NSImageAlignCenter];
    /* Scaling alternatives: NSScaleToFit - NSImageScaleProportionallyDown - NSScaleNone */
    [App->previewWindow setImageScaling: NSImageScaleProportionallyDown];
    /* NSImageView doesn't need to be anchored for some reason, just works anyway */
    [App->previewWindow setAutoresizingMask: (NSViewHeightSizable | NSViewWidthSizable) ];
    // [previewWindow setTarget:previewWindow];
    [App->previewWindow setSourceImage:App->rawImage width:1880 height:1056 bitDepth:8];

    [[App->window contentView] addSubview: App->previewWindow];

    /* Slider for moving thourhg the clip */
    App->timelineSlider = [
        [NSSlider alloc]
        initWithFrame: NSMakeRect( TIMELINE_SLIDER_LOCATION )
    ];
    [App->timelineSlider setTarget: App->timelineSlider];
    [App->timelineSlider setAction: @selector(timelineSliderMethod)];
    [App->timelineSlider setDoubleValue: 0.0];
    AnchorRight(App->timelineSlider, YES);
    AnchorLeft(App->timelineSlider, YES);
    [App->timelineSlider setAutoresizingMask: NSViewWidthSizable];
    [[App->window contentView] addSubview: App->timelineSlider];



    /* Session tableview */
    // create a table view and a scroll view
    NSScrollView * tableContainer = [[NSScrollView alloc] initWithFrame:NSMakeRect(SIDE_GAP_X_L, 64, LEFT_SIDEBAR_ELEMENT_WIDTH, WINDOW_HEIGHT-128)];
    App->session.clipTable = [[NSTableView alloc] init];
    App->session.clipTable.backgroundColor = [NSColor colorWithRed:0.235 green:0.235 blue:0.235 alpha:0.8];
    [App->session.clipTable setFocusRingType: NSFocusRingTypeNone]; /* Hide ugly 'active' border */
    // create columns for our table
    NSTableColumn * clipColumn = [[NSTableColumn alloc] initWithIdentifier:@"Col1"];
    [clipColumn.headerCell setStringValue:@"Clip Name"];
    [clipColumn setWidth: LEFT_SIDEBAR_ELEMENT_WIDTH];
    // generally you want to add at least one column to the table view.
    [App->session.clipTable addTableColumn:clipColumn];
    // [App->session.clipTable setDelegate: [clipTableDelegate new]];
    // [App->session.clipTable setDataSource: [clipTableDelegate new]];
    [App->session.clipTable reloadData];
    // embed the table view in the scroll view, and add the scroll view
    // to our window.
    [tableContainer setDocumentView:App->session.clipTable];
    [tableContainer setHasVerticalScroller:NO];
    [tableContainer setAutoresizingMask: NSViewHeightSizable];
    [[App->window contentView] addSubview:tableContainer];


    /* Control for swapping between processing view and LLRawProc control 'tabs'
     * https://developer.apple.com/library/content/documentation/Cocoa/Conceptual/SegmentedControl/Articles/SegmentedControlCode.html */

    App->processingTabSwitch = [[NSSegmentedControl alloc] initWithFrame: NSMakeRect(RIGHT_SIDEBAR_SLIDER(0, ELEMENT_HEIGHT, 17))];
    [App->processingTabSwitch setSegmentCount:2];
    [App->processingTabSwitch setLabel:@"Correct" forSegment:0];
    [App->processingTabSwitch setLabel:@"Process" forSegment:1];
    AnchorRight(App->processingTabSwitch, YES);
    AnchorTop(App->processingTabSwitch, YES);
    [App->processingTabSwitch setTarget: App->processingTabSwitch];
    [App->processingTabSwitch setAction: @selector(toggleTab)];
    App->processingTabSwitch.selectedSegment = 1; /* Processing is default tab */
    [[App->window contentView] addSubview: App->processingTabSwitch];

    /*
     *******************************************************************************
     * Create LLRawProc tab
     *******************************************************************************
     */


    App->fixRawSelector = [[NSButton alloc] initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(1, ELEMENT_HEIGHT, 21) )];
    [App->fixRawSelector setButtonType: NSSwitchButton];
    [App->fixRawSelector setTitle: @"Apply RAW Corrections"];
    AnchorRight(App->fixRawSelector, YES);
    AnchorTop(App->fixRawSelector, YES);
    [App->fixRawSelector setTarget: App->fixRawSelector];
    [App->fixRawSelector setAction: @selector(toggleLLRawProc)];
    [[App->window contentView] addSubview: App->fixRawSelector];


    App->focusPixelLabel = [ [NSTextField alloc]
                             initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(2,ELEMENT_HEIGHT,5) )];
    [App->focusPixelLabel setLabelStyle];
    AnchorTop(App->focusPixelLabel, YES);
    AnchorRight(App->focusPixelLabel, YES);
    [App->focusPixelLabel setStringValue: @"Fix Focus Pixels"];
    [[App->window contentView] addSubview: App->focusPixelLabel];
    App->focusPixelOption = [ [NSSegmentedControl alloc]
                              initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(2,ELEMENT_HEIGHT,5) ) ];
    AnchorRight(App->focusPixelOption, YES);
    AnchorTop(App->focusPixelOption, YES);
    [App->focusPixelOption setSegmentCount:3];
    [App->focusPixelOption setLabel:@"Off" forSegment:0];
    [App->focusPixelOption setLabel:@"On" forSegment:1];
    [App->focusPixelOption setLabel:@"CropRec" forSegment:2];
    [App->focusPixelOption setTarget: App->focusPixelOption];
    [App->focusPixelOption setAction: @selector(focusPixelMethod)];
    App->focusPixelOption.selectedSegment = 0;
    [[App->window contentView] addSubview: App->focusPixelOption];


    App->focusPixelMethodLabel = [ [NSTextField alloc]
                                   initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(3,ELEMENT_HEIGHT,-5) )];
    [App->focusPixelMethodLabel setLabelStyle];
    AnchorTop(App->focusPixelMethodLabel, YES);
    AnchorRight(App->focusPixelMethodLabel, YES);
    [App->focusPixelMethodLabel setStringValue: @"Focus Pixel Interpolation"];
    [[App->window contentView] addSubview: App->focusPixelMethodLabel];
    App->focusPixelMethodOption = [ [NSSegmentedControl alloc]
                                    initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(3,ELEMENT_HEIGHT,-5) ) ];
    AnchorRight(App->focusPixelMethodOption, YES);
    AnchorTop(App->focusPixelMethodOption, YES);
    [App->focusPixelMethodOption setSegmentCount:2];
    [App->focusPixelMethodOption setLabel:@"mlvfs" forSegment:0];
    [App->focusPixelMethodOption setLabel:@"raw2dng" forSegment:1];
    [App->focusPixelMethodOption setTarget: App->focusPixelMethodOption];
    [App->focusPixelMethodOption setAction: @selector(focusPixelMethod)];
    App->focusPixelMethodOption.selectedSegment = 0;
    [[App->window contentView] addSubview: App->focusPixelMethodOption];


    App->badPixelLabel = [ [NSTextField alloc]
                           initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(4,ELEMENT_HEIGHT,-15) )];
    [App->badPixelLabel setLabelStyle];
    AnchorTop(App->badPixelLabel, YES);
    AnchorRight(App->badPixelLabel, YES);
    [App->badPixelLabel setStringValue: @"Fix Bad Pixels"];
    [[App->window contentView] addSubview: App->badPixelLabel];
    App->badPixelOption = [ [NSSegmentedControl alloc]
                            initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(4,ELEMENT_HEIGHT,-15) ) ];
    AnchorRight(App->badPixelOption, YES);
    AnchorTop(App->badPixelOption, YES);
    [App->badPixelOption setSegmentCount:3];
    [App->badPixelOption setLabel:@"Off" forSegment:0];
    [App->badPixelOption setLabel:@"On" forSegment:1];
    [App->badPixelOption setLabel:@"Aggressive" forSegment:2];
    [App->badPixelOption setTarget: App->badPixelOption];
    [App->badPixelOption setAction: @selector(badPixelMethod)];
    App->badPixelOption.selectedSegment = 0;
    [[App->window contentView] addSubview: App->badPixelOption];


    App->badPixelMethodLabel = [ [NSTextField alloc]
                                 initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(5,ELEMENT_HEIGHT,-25) )];
    [App->badPixelMethodLabel setLabelStyle];
    AnchorTop(App->badPixelMethodLabel, YES);
    AnchorRight(App->badPixelMethodLabel, YES);
    [App->badPixelMethodLabel setStringValue: @"Bad Pixel Interpolation"];
    [[App->window contentView] addSubview: App->badPixelMethodLabel];
    App->badPixelMethodOption = [ [NSSegmentedControl alloc]
                                  initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(5,ELEMENT_HEIGHT,-25) ) ];
    AnchorRight(App->badPixelMethodOption, YES);
    AnchorTop(App->badPixelMethodOption, YES);
    [App->badPixelMethodOption setSegmentCount:2];
    [App->badPixelMethodOption setLabel:@"mlvfs" forSegment:0];
    [App->badPixelMethodOption setLabel:@"raw2dng" forSegment:1];
    [App->badPixelMethodOption setTarget: App->badPixelMethodOption];
    [App->badPixelMethodOption setAction: @selector(badPixelMethod)];
    App->badPixelMethodOption.selectedSegment = 0;
    [[App->window contentView] addSubview: App->badPixelMethodOption];


    App->chromaSmoothLabel = [ [NSTextField alloc]
                               initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(6,ELEMENT_HEIGHT,-35) )];
    [App->chromaSmoothLabel setLabelStyle];
    AnchorTop(App->chromaSmoothLabel, YES);
    AnchorRight(App->chromaSmoothLabel, YES);
    [App->chromaSmoothLabel setStringValue: @"Chroma Smooth"];
    [[App->window contentView] addSubview: App->chromaSmoothLabel];
    App->chromaSmoothOption = [ [NSSegmentedControl alloc]
                                initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(6,ELEMENT_HEIGHT,-35) ) ];
    AnchorRight(App->chromaSmoothOption, YES);
    AnchorTop(App->chromaSmoothOption, YES);
    [App->chromaSmoothOption setSegmentCount:4];
    [App->chromaSmoothOption setLabel:@"Off" forSegment:0];
    [App->chromaSmoothOption setLabel:@"2x2" forSegment:1];
    [App->chromaSmoothOption setLabel:@"3x3" forSegment:2];
    [App->chromaSmoothOption setLabel:@"5x5" forSegment:3];
    [App->chromaSmoothOption setTarget: App->chromaSmoothOption];
    [App->chromaSmoothOption setAction: @selector(chromaSmoothMethod)];
    App->chromaSmoothOption.selectedSegment = 0;
    [[App->window contentView] addSubview: App->chromaSmoothOption];


    App->stripeFixLabel = [ [NSTextField alloc]
                            initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(7,ELEMENT_HEIGHT, -45) )];
    [App->stripeFixLabel setLabelStyle];
    AnchorTop(App->stripeFixLabel, YES);
    AnchorRight(App->stripeFixLabel, YES);
    [App->stripeFixLabel setStringValue: @"Vertical Stripe Fix"];
    [[App->window contentView] addSubview: App->stripeFixLabel];
    App->stripeFixOption = [ [NSSegmentedControl alloc]
                             initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(7,ELEMENT_HEIGHT,-45) ) ];
    AnchorRight(App->stripeFixOption, YES);
    AnchorTop(App->stripeFixOption, YES);
    [App->stripeFixOption setSegmentCount:3];
    [App->stripeFixOption setLabel:@"Off" forSegment:0];
    [App->stripeFixOption setLabel:@"Normal" forSegment:1];
    [App->stripeFixOption setLabel:@"Force" forSegment:2];
    [App->stripeFixOption setTarget: App->stripeFixOption];
    [App->stripeFixOption setAction: @selector(verticalStripeMethod)];
    App->stripeFixOption.selectedSegment = 0;
    [[App->window contentView] addSubview: App->stripeFixOption];


    App->patternNoiseLabel = [ [NSTextField alloc]
                               initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(8,ELEMENT_HEIGHT,-55) )];
    [App->patternNoiseLabel setLabelStyle];
    AnchorTop(App->patternNoiseLabel, YES);
    AnchorRight(App->patternNoiseLabel, YES);
    [App->patternNoiseLabel setStringValue: @"Pattern Noise Fix"];
    [[App->window contentView] addSubview: App->patternNoiseLabel];
    App->patternNoiseOption = [ [NSSegmentedControl alloc]
                                initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(8,ELEMENT_HEIGHT,-55) ) ];
    AnchorRight(App->patternNoiseOption, YES);
    AnchorTop(App->patternNoiseOption, YES);
    [App->patternNoiseOption setSegmentCount:2];
    [App->patternNoiseOption setLabel:@"Off" forSegment:0];
    [App->patternNoiseOption setLabel:@"On" forSegment:1];
    [App->patternNoiseOption setTarget: App->patternNoiseOption];
    [App->patternNoiseOption setAction: @selector(patternNoiseMethod)];
    App->patternNoiseOption.selectedSegment = 0;
    [[App->window contentView] addSubview: App->patternNoiseOption];


    App->dualISOLabel = [ [NSTextField alloc]
                          initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(9,ELEMENT_HEIGHT,-65) )];
    [App->dualISOLabel setLabelStyle];
    AnchorTop(App->dualISOLabel, YES);
    AnchorRight(App->dualISOLabel, YES);
    [App->dualISOLabel setStringValue: @"Dual ISO"];
    [[App->window contentView] addSubview: App->dualISOLabel];
    App->dualISOOption = [ [NSSegmentedControl alloc]
                           initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(9,ELEMENT_HEIGHT,-65) ) ];
    AnchorRight(App->dualISOOption, YES);
    AnchorTop(App->dualISOOption, YES);
    [App->dualISOOption setSegmentCount:3];
    [App->dualISOOption setLabel:@"Off" forSegment:0];
    [App->dualISOOption setLabel:@"Full 20bit" forSegment:1];
    [App->dualISOOption setLabel:@"Fast" forSegment:2];
    
    [App->dualISOOption setTarget: App->dualISOOption];
    [App->dualISOOption setAction: @selector(dualISOMethod)];
    App->dualISOOption.selectedSegment = 0;
    [[App->window contentView] addSubview: App->dualISOOption];

    App->dualISOMethodLabel = [ [NSTextField alloc]
                                initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(10,ELEMENT_HEIGHT,-75) )];
    [App->dualISOMethodLabel setLabelStyle];
    AnchorTop(App->dualISOMethodLabel, YES);
    AnchorRight(App->dualISOMethodLabel, YES);
    [App->dualISOMethodLabel setStringValue: @"Interpolation Method"];
    [[App->window contentView] addSubview: App->dualISOMethodLabel];
    App->dualISOMethodOption = [ [NSSegmentedControl alloc]
                                 initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(10,ELEMENT_HEIGHT,-75) ) ];
    AnchorRight(App->dualISOMethodOption, YES);
    AnchorTop(App->dualISOMethodOption, YES);
    [App->dualISOMethodOption setSegmentCount:2];
    [App->dualISOMethodOption setLabel:@"AMaZE" forSegment:0];
    [App->dualISOMethodOption setLabel:@"mean23" forSegment:1];
    [App->dualISOMethodOption setTarget: App->dualISOMethodOption];
    [App->dualISOMethodOption setAction: @selector(dualISOMethod)];
    App->dualISOMethodOption.selectedSegment = 0;
    [[App->window contentView] addSubview: App->dualISOMethodOption];

    App->fullResBlendingLabel = [ [NSTextField alloc]
                                  initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(11,ELEMENT_HEIGHT,-85) )];
    [App->fullResBlendingLabel setLabelStyle];
    AnchorTop(App->fullResBlendingLabel, YES);
    AnchorRight(App->fullResBlendingLabel, YES);
    [App->fullResBlendingLabel setStringValue: @"Full-Res Blending"];
    [[App->window contentView] addSubview: App->fullResBlendingLabel];
    App->fullResBlendingOption = [ [NSSegmentedControl alloc]
                                   initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(11,ELEMENT_HEIGHT,-85) ) ];
    AnchorRight(App->fullResBlendingOption, YES);
    AnchorTop(App->fullResBlendingOption, YES);
    [App->fullResBlendingOption setSegmentCount:2];
    [App->fullResBlendingOption setLabel:@"On" forSegment:0];
    [App->fullResBlendingOption setLabel:@"Off" forSegment:1];
    [App->fullResBlendingOption setTarget: App->fullResBlendingOption];
    [App->fullResBlendingOption setAction: @selector(dualISOMethod)];
    App->fullResBlendingOption.selectedSegment = 0;
    [[App->window contentView] addSubview: App->fullResBlendingOption];

    App->aliasMapLabel = [ [NSTextField alloc]
                           initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(12,ELEMENT_HEIGHT,-95) )];
    [App->aliasMapLabel setLabelStyle];
    AnchorTop(App->aliasMapLabel, YES);
    AnchorRight(App->aliasMapLabel, YES);
    [App->aliasMapLabel setStringValue: @"Alias Map"];
    [[App->window contentView] addSubview: App->aliasMapLabel];
    App->aliasMapOption = [ [NSSegmentedControl alloc]
                            initWithFrame: NSMakeRect( RIGHT_SIDEBAR_SLIDER(12,ELEMENT_HEIGHT,-95) ) ];
    AnchorRight(App->aliasMapOption, YES);
    AnchorTop(App->aliasMapOption, YES);
    [App->aliasMapOption setSegmentCount:2];
    [App->aliasMapOption setLabel:@"On" forSegment:0];
    [App->aliasMapOption setLabel:@"Off" forSegment:1];
    [App->aliasMapOption setTarget: App->aliasMapOption];
    [App->aliasMapOption setAction: @selector(dualISOMethod)];
    App->aliasMapOption.selectedSegment = 0;
    [[App->window contentView] addSubview: App->aliasMapOption];




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

    // NSRect frame = NSMakeRect(0, 0, 566, 566);
    // NSWindow * window  = [[[NSWindow alloc] initWithContentRect:frame
    //                       styleMask:NSTitledWindowMask | NSClosableWindowMask
    //                       backing:NSBackingStoreBuffered
    //                       defer:NO] autorelease];
    // [window setTitle:@"MLV App Update"];
    // window.appearance = [NSAppearance appearanceNamed: NSAppearanceNameVibrantDark];
    // [window makeKeyAndOrderFront:NSApp];

    [NSApp run];

    return 0;
}
