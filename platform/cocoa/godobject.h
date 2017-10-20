#ifndef _godobject_h_
#define _godobject_h_

#import <Cocoa/Cocoa.h>

/* MLV OpenGL based view */
#include "mlv_view.h"

/* MLV stuff */
#include "../../src/mlv_include.h"


/* Info about a clip, for handling many in a session. All clips share same processing object */
typedef struct {

    /* MLV object */
    mlvObject_t * video;

    /* Easier to set slider positions than using actual processing values */
    struct {
        double exposureSlider;
        double saturationSlider;
        double kelvinSlider;
        double tintSlider;
        double darkStrengthSlider;
        double darkRangeSlider;
        double lightStrengthSlider;
        double lightRangeSlider;
        double lightenSlider;
        int highlightReconstructionSelector;
        int alwaysUseAmazeSelector;
        int tonemappingSelector;
    } settings;

} clipInfo_t;

/* Just a god object for the Cocoa GUI, because I literally can't be asked to learn the proper way of doing this stuff */
typedef struct {

    /* THE application window */
    NSWindow * window;
    /* The actual view that will display it */
    MLVView * previewWindow;
    /* Holds a (THE) processed frame that is displayed */
    NSBitmapImageRep * rawBitmap;
    /* Yes, displayed image will be 8 bit, as most monitors are */
    uint8_t * rawImage;


    /****************************************
     **          PROCESSING STUFF          **
     ****************************************/

    /* Sliders */
    NSSlider * exposureSlider, /* All adjustment sliders for processing */
    * saturationSlider, * kelvinSlider,
    * tintSlider, * darkStrengthSlider,
    * darkRangeSlider, * lightStrengthSlider,
    * lightRangeSlider, * lightenSlider,
    * sharpnessSlider;
    /* Slider labels */
    NSTextField * exposureLabel, * exposureValueLabel,
    * saturationLabel, * saturationValueLabel, * kelvinLabel, 
    * kelvinValueLabel, * tintLabel, * tintValueLabel,
    * darkStrengthLabel, * darkStrengthValueLabel,
    * darkRangeLabel, * darkRangeValueLabel,
    * lightStrengthLabel, * lightStrengthValueLabel,
    * lightRangeLabel, * lightRangeValueLabel,
    * lightenLabel, * lightenValueLabel,
    * sharpnessLabel, * sharpnessValueLabel;
    /* Buttons (Checkboxes) */
    NSButton * highlightReconstructionSelector;
    NSButton * alwaysUseAmazeSelector;
    /* Select image profile */
    NSPopUpButton * imageProfile;

    /****************************************
     **           LLRawProc STUFF          **
     ****************************************/

    NSButton * fixRawSelector; /* Checkbox */

    NSTextField * focusPixelLabel;
    NSSegmentedControl * focusPixelOption;

    NSTextField * stripeFixLabel;
    NSSegmentedControl * stripeFixOption;

    NSTextField * chromaSmoothLabel;
    NSSegmentedControl * chromaSmoothOption;

    NSTextField * patternNoiseLabel;
    NSSegmentedControl * patternNoiseOption;

    NSTextField * badPixelLabel;
    NSSegmentedControl * badPixelOption;




    NSSlider * timelineSlider; /* Timeline slider */

    /* Buttons */
    NSButton * openMLVButton;
    NSButton * exportProRes4444Button;

    /* Select video export format */
    NSPopUpButton * videoFormat;

    /* Select between LLRawProc and Processing tab */
    NSSegmentedControl * processingTabSwitch;

    /****************************************
     **           SESSION STUFF            **
     ****************************************/

    struct {

        /* Number of clips */
        int clipCount;
        /* List of clips loaded (in session) */
        NSTableView * clipTable;
        /* Info about each one (array as long as clipcount) */
        clipInfo_t * clipInfo;

    } session;

    /****************************************
     **        END OF SESSION STUFF        **
     ****************************************/

    /* The main video object that the app will use for
     * handling MLV videos and processing them */
    mlvObject_t * videoMLV;
    processingObject_t * processingSettings;
    char * MLVClipName;
    
    double frameSliderPosition;

    /* String for exporting using ffmpeg command - TEMPORARY unil avfoundation */
    char * ffmpegFormatString;

    /* ++ this on adjustments to redraw or on playback to go draw next frame */
    int frameChanged;
    /* What frame we r on */
    int currentFrameIndex;
    /* To pause frame drawing, above 0 = paused */
    int dontDraw;

    /* How much cache */
    int cacheSizeMB;

} godObject_t;

#endif