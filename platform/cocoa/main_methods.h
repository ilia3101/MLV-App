#ifndef _main_methods_
#define _main_methods_

/* Methods that actually do stuff related to MLV on user interface interactions */


/* Button methods */

@interface NSButton (mainMethods)

/* Opens a dialog to select MLV file + sets MLV file to that */
-(void)openMlvDialog;
/* Opens a dialog to select export location, then exports BMPs */
-(void)exportBmpSequence;

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


#endif