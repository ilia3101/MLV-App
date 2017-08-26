/* Methods for user interface interactions 
 * this is where some real code goes */

#include <math.h>
#include <string.h>
#include <unistd.h>

#import "Cocoa/Cocoa.h"

#include "gui_stuff/app_design.h"

#include "main_methods.h"
#include "../../src/mlv_include.h"

#include "mac_info.h"

#include "background_thread.h"

/* God object used to share globals (type) */
#include "godobject.h"
/* The godobject itsself */
extern godObject_t * App;


/* Methods/functions for handling sessions */

/* This is a function as it may be used in more than one place */
void sessionAddNewMlvClip(char * mlvPathString, char * mlvFileName)
{
    return;
    /* Do clip adding stuff here... */
    App->session.clipCount++;
}

/* Called from -(void)openSessionDialog - currently only loads first clip */
void appLoadSession(char * sessionPath)
{
    /* Open the MASXML file 4 reading */
    FILE * session_file = fopen(sessionPath, "rb");

    /* Get size of file */
    fseek(session_file, 0, SEEK_END);
    uint64_t masxml_size = ftell(session_file);

    /* Don't allow files over over 8MB */
    if (masxml_size > (1 << 23)) return;

    /* Read whole session in to memory */
    char * session_xml = calloc(masxml_size, sizeof(char));
    fread(session_xml, sizeof(char), masxml_size, session_file);
    fclose(session_file);

    /* Parse the XML... */

    /* This will be boring to write :( */

    free(session_xml);
}

/* Frees/deletes all mlv objects */
void appClearSession()
{
    /* Not done as you can see */
    App->session.clipCount = 0;
    return;
}


/* Button methods */
@implementation NSButton (sessionMethods)

/* Opens a dialog to select MLV file + sets MLV file to that */
-(void)openSessionDialog
{
    return;
}

@end


/* Slider methods */
@implementation NSSlider (sessionMethods)

@end