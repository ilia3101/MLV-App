#ifndef _session_methods_h_
#define _session_methods_h_

/* Methods/functions for handling sessions */


/* This is a function as it may be used in more than one place */
void sessionAddNewMlvClip(char * mlvPathString, char * mlvFileName);
/* Called from -(void)openSessionDialog - currently only loads first clip */
void appLoadSession(char * sessionPath);
/* Frees/deletes all mlv objects */
void appClearSession();


/* Button methods */
@interface NSButton (sessionMethods)

/* Opens a dialog to select MLV file + sets MLV file to that */
-(void)openSessionDialog;

@end


/* Slider methods */
@interface NSSlider (sessionMethods)

@end

#endif
