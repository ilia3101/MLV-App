#ifndef _main_methods_h_
#define _main_methods_h_

/* Methods that actually do stuff related to MLV on user interface interactions */

/* Initialises app UI */
void initAppWithGod();
void syncGUI(); /* Refreshes all controls, connects all to clip */

/* This is a function as it may be used in more than one place */
int setAppNewMlvClip(char * mlvPath); /* 0=fail 1=ok */
/* Sets app to have no open clip */
void setAppCurrentClipNoClip();

/* Button methods */
@interface NSButton (mainMethods)

/* Enable/disable highlight reconstruction */
-(void)toggleHighlightReconstruction;
/* Enables/disable chroma separation in processing (for better sharpening) */
-(void)toggleChromaSeparation;
/* Enables/disables always AMaZE requirement */
-(void)toggleAlwaysAmaze;
/* Enables/disables "raw corrections" */
-(void)toggleLLRawProc;
/* Opens a dialog to select MLV file + sets MLV file to that */
-(void)openMlvDialog;
/* Opens a dialog to select export location, then exports a clip/clips! */
-(void)exportCurrentClip;
-(void)exportAllClips;
/* Save Session */
-(void)saveSessionDialog;
/* Open a MASXML and the clips from it */
-(void)openSessionDialog;

@end

/* NSPopUpButton methods */
@interface NSPopUpButton (mainMethods)

/* Select processing image profile */
-(void)toggleImageProfile;
/* Select video format (currently only ProRes options) */
-(void)toggleVideoFormat;

@end

/* Slider methods */
@interface NSSlider (mainMethods)

/* I think its clear what these do... */
-(void)exposureSliderMethod;
-(void)highlightsSliderMethod;
-(void)shadowsSliderMethod;
-(void)saturationSliderMethod;
#define KELVIN_MAX 10000.0
#define KELVIN_MIN 2000.0
-(void)kelvinSliderMethod;
-(void)tintSliderMethod;
-(void)darkStrengthMethod;
-(void)darkRangeMethod;
-(void)lightStrengthMethod;
-(void)lightRangeMethod;
-(void)lightenMethod;
-(void)sharpnessMethod;
-(void)chromaBlurMethod;

/* For scrubbijng through the clip */
-(void)timelineSliderMethod;

@end

/* NSSegmentedControl methods */
@interface NSSegmentedControl (mainMethods)

-(void)dualISOMethod; /* All 4 dual iso controls call this */
-(void)focusPixelMethod;
-(void)badPixelMethod;
-(void)patternNoiseMethod;
-(void)verticalStripeMethod;
-(void)chromaSmoothMethod;

/* Select tab (Processing, LLRawProc... etc + more in the future) */
-(void)toggleTab;
-(void)toggleTabLeft;

@end

#endif
