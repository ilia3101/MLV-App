#ifndef _main_methods_h_
#define _main_methods_h_

/* Methods that actually do stuff related to MLV on user interface interactions */

/* Initialises app UI */
void initAppWithGod();
void syncGUI(); /* Refreshes all controls, connects all to clip */

/* This is a function as it may be used in more than one place */
int setAppNewMlvClip(char * mlvPath); /* 0=fail 1=ok */

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
/* Enables/disables processing tonemapping */
-(void)toggleTonemapping;
/* Opens a dialog to select MLV file + sets MLV file to that */
-(void)openMlvDialog;
/* Opens a dialog to select export location, then exports a mov clip with prores! */
-(void)exportProRes4444;

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
-(void)saturationSliderMethod;
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

@end

/* Data source for MLV View */
// @interface MLVTableViewDataSource : NSTableViewDataSource

// - (NSInteger)numberOfRowsInTableView:(NSTableView *)aTableView;
// - (id)tableView:(NSTableView *)aTableView objectValueForTableColumn:(NSTableColumn *)aTableColumn row:(NSInteger)rowIndex;

// @end

#endif
