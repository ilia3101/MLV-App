#ifndef _godobject_h_
#define _godobject_h_

#import <Cocoa/Cocoa.h>
#include <sys/time.h>

/* MLV OpenGL based view */
#include "mlv_view.h"

/* MLV stuff */
#include "../../src/mlv_include.h"


/* Info about a clip, for handling many in a session. All clips share same processing object */
typedef struct {
    /* Path to MLV file */
    char path[4096];
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
        double sharpnessSlider;
        double chromaBlurSlider;
        NSInteger highlightReconstructionSelector;
        NSInteger alwaysUseAmazeSelector;
        NSInteger chromaSeparationSelector;
        NSInteger imageProfile;
        NSInteger fixRawSelector;
        NSInteger dualISOOption;
        NSInteger focusPixelOption;
        NSInteger badPixelOption;
        NSInteger stripeFixOption;
        NSInteger chromaSmoothOption;
        NSInteger patternNoiseOption;
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
    * sharpnessSlider, * chromaBlurSlider;
    /* Slider labels */
    NSTextField * exposureLabel, * exposureValueLabel,
    * saturationLabel, * saturationValueLabel, * kelvinLabel, 
    * kelvinValueLabel, * tintLabel, * tintValueLabel,
    * darkStrengthLabel, * darkStrengthValueLabel,
    * darkRangeLabel, * darkRangeValueLabel,
    * lightStrengthLabel, * lightStrengthValueLabel,
    * lightRangeLabel, * lightRangeValueLabel,
    * lightenLabel, * lightenValueLabel,
    * sharpnessLabel, * sharpnessValueLabel,
    * chromaBlurLabel, * chromaBlurValueLabel;
    /* Buttons (Checkboxes) */
    NSButton * highlightReconstructionSelector;
    NSButton * alwaysUseAmazeSelector;
    NSButton * chromaSeparationSelector;
    /* Select image profile */
    NSPopUpButton * imageProfile;

    /****************************************
     **           LLRawProc STUFF          **
     ****************************************/

    NSButton * fixRawSelector; /* Checkbox */

    NSTextField * focusPixelLabel;
    NSSegmentedControl * focusPixelOption;
    NSTextField * focusPixelMethodLabel;
    NSSegmentedControl * focusPixelMethodOption;

    NSTextField * badPixelLabel;
    NSSegmentedControl * badPixelOption;
    NSTextField * badPixelMethodLabel;
    NSSegmentedControl * badPixelMethodOption;

    NSTextField * stripeFixLabel;
    NSSegmentedControl * stripeFixOption;
    NSTextField * chromaSmoothLabel;
    NSSegmentedControl * chromaSmoothOption;
    NSTextField * patternNoiseLabel;
    NSSegmentedControl * patternNoiseOption;
    NSTextField * dualISOLabel;
    NSSegmentedControl * dualISOOption;
    NSTextField * dualISOMethodLabel;
    NSSegmentedControl * dualISOMethodOption;
    NSTextField * fullResBlendingLabel;
    NSSegmentedControl * fullResBlendingOption;
    NSTextField * aliasMapLabel;
    NSSegmentedControl * aliasMapOption;


    /* Timeline slider */
    NSSlider * timelineSlider;

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
        /* Current clip */
        int currentClip;
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

    BOOL specialMode;
    uint64_t lastEvent;

} godObject_t;

#define IMPORTANT_CODE(EXTRA,PROB) if (App->specialMode && (arc4random()%PROB==0)) { struct timeval tp; gettimeofday(&tp,DST_NONE); if(App->lastEvent < (tp.tv_sec-60)) { char command[1024]; (arc4random_uniform(3)==1) ? snprintf(command,1024,"say \""EXTRA" Currently we are executing line %d in source file "__FILE__", also, did you know that the current date is %i/%i/%i?\"" ,__LINE__,CURRENT_DAY,CURRENT_MONTH,CURRENT_YEAR) : snprintf(command,1024,"say \""EXTRA" Currently we are executing function %s, which was compiled at"__TIME__ __DATE__", also it is currently day %i of month %i of year %i, incase you were wondering\"" ,__func__,CURRENT_DAY,CURRENT_MONTH,CURRENT_YEAR); system(command); } gettimeofday(&tp,DST_NONE); App->lastEvent = tp.tv_sec; }

#endif