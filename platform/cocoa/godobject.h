#ifndef _godobject_h_
#define _godobject_h_

#import "Cocoa/Cocoa.h"

/* Just a god object for the Cocoa GUI, because I literally can't be asked to learn the proper way of doing this stuff */

typedef struct {

    /* THE application window */
    NSWindow * window;
    /* The actual view that will display it */
    NSImageView * previewWindow;
    /* Holds rawBitmap inside it or something */
    NSImage * rawImageObject;
    /* Holds a (THE) processed frame that is displayed */
    NSBitmapImageRep * rawBitmap;
    /* Yes, displayed image will be 8 bit, as most monitors are */
    uint8_t * rawImage;

    /* Sliders */
    NSSlider * exposureSlider,
    * saturationSlider, * kelvinSlider,
    * tintSlider, * darkStrengthSlider,
    * darkRangeSlider, * lightStrengthSlider,
    * lightRangeSlider, * lightenSlider;
    /* Slider labels */
    NSTextField * exposureLabel, * exposureValueLabel,
    * saturationLabel, * saturationValueLabel, * kelvinLabel, 
    * kelvinValueLabel, * tintLabel, * tintValueLabel,
    * darkStrengthLabel, * darkStrengthValueLabel,
    * darkRangeLabel, * darkRangeValueLabel,
    * lightStrengthLabel, * lightStrengthValueLabel,
    * lightRangeLabel, * lightRangeValueLabel,
    * lightenLabel, * lightenValueLabel;

    /* The main video object that the app will use for
     * handling MLV videos and processing them */
    mlvObject_t * videoMLV;
    processingObject_t * processingSettings;
    char * MLVClipName;
    
    double frameSliderPosition;

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