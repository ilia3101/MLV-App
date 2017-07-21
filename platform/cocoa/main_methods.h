#ifndef _main_methods_
#define _main_methods_

/* Methods that actually do stuff related to MLV on user interface interactions */

/* This is a function as it may be used in more than one place */
void setAppNewMlvClip(char * mlvPathString, char * mlvFileName);

/* Button methods */
@interface NSButton (mainMethods)

/* Opens a dialog to select MLV file + sets MLV file to that */
-(void)openMlvDialog;
/* Opens a dialog to select export location, then exports images */
-(void)exportJpegSequence;
-(void)exportPngSequence;

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

/* For scrubbijng through the clip */
-(void)timelineSliderMethod;

@end

/* NSImageView stuff */
@interface NSImageView (mainMethods)

-(void)updatePreviewWindow;

@end

#endif