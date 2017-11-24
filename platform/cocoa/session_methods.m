/* Methods for user interface interactions 
 * this is where some real code goes */

#include <math.h>
#include <string.h>
#include <unistd.h>

#import "Cocoa/Cocoa.h"

#include "session_methods.h"

#include "gui_stuff/app_design.h"
#include "gui_stuff/useful_methods.h"

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
void sessionAddNewMlvClip(char * mlvPathString)
{
    /* Save current clip settings */
    saveClipInfo(App->session.clipInfo + App->session.currentClip);
    App->session.currentClip = App->session.clipCount++;
    App->session.clipInfo = realloc(App->session.clipInfo, sizeof(clipInfo_t) * App->session.clipCount);
    setDefaultsClip(App->session.clipInfo + App->session.clipCount-1);
    strncpy(App->session.clipInfo[App->session.clipCount-1].path, mlvPathString, 4096);
    setAppGUIFromClip(App->session.clipInfo + App->session.clipCount-1);
}

/* Copy current settings to a clipinfo_t struct */
void saveClipInfo(clipInfo_t * clip)
{
    clip->settings.exposureSlider = [App->exposureSlider doubleValue];
    clip->settings.saturationSlider = [App->saturationSlider doubleValue];
    clip->settings.kelvinSlider = [App->kelvinSlider doubleValue];
    clip->settings.tintSlider = [App->tintSlider doubleValue];
    clip->settings.darkStrengthSlider = [App->darkStrengthSlider doubleValue];
    clip->settings.darkRangeSlider = [App->darkRangeSlider doubleValue];
    clip->settings.lightStrengthSlider = [App->lightStrengthSlider doubleValue];
    clip->settings.lightRangeSlider = [App->lightRangeSlider doubleValue];
    clip->settings.lightenSlider = [App->lightenSlider doubleValue];
    clip->settings.sharpnessSlider = [App->sharpnessSlider doubleValue];
    clip->settings.chromaBlurSlider = [App->chromaBlurSlider doubleValue];
    clip->settings.highlightReconstructionSelector = [App->highlightReconstructionSelector state];
    clip->settings.alwaysUseAmazeSelector = [App->alwaysUseAmazeSelector state];
    clip->settings.chromaSeparationSelector = [App->chromaSeparationSelector state];
    clip->settings.imageProfile = [App->imageProfile indexOfSelectedItem];
    clip->settings.fixRawSelector = [App->fixRawSelector state];
    clip->settings.dualISOOption = [App->dualISOOption indexOfSelectedItem];
    clip->settings.focusPixelOption = [App->focusPixelOption indexOfSelectedItem];
    clip->settings.badPixelOption = [App->badPixelOption indexOfSelectedItem];
    clip->settings.stripeFixOption = [App->stripeFixOption indexOfSelectedItem];
    clip->settings.chromaSmoothOption = [App->chromaSmoothOption indexOfSelectedItem];
    clip->settings.patternNoiseOption = [App->patternNoiseOption indexOfSelectedItem];
}

/* Sets a clipinfo_t object to default settings */
void setDefaultsClip(clipInfo_t * clip)
{
    clip->settings.exposureSlider = 0.5;
    clip->settings.saturationSlider = 0.5;
    clip->settings.kelvinSlider = 0.5;
    clip->settings.tintSlider = 0.5;
    clip->settings.darkStrengthSlider = 0.23;
    clip->settings.darkRangeSlider = 0.73;
    clip->settings.lightStrengthSlider = 0.0;
    clip->settings.lightRangeSlider = 0.5;
    clip->settings.lightenSlider = 0.0;
    clip->settings.sharpnessSlider = 0.0;
    clip->settings.chromaBlurSlider = 0.0;
    clip->settings.highlightReconstructionSelector = NSOffState;
    clip->settings.alwaysUseAmazeSelector = NSOffState;
    clip->settings.chromaSeparationSelector = NSOffState;
    clip->settings.imageProfile = 0;
    clip->settings.fixRawSelector = NSOffState;
    clip->settings.dualISOOption = 0;
    clip->settings.focusPixelOption = 0;
    clip->settings.badPixelOption = 0;
    clip->settings.stripeFixOption = 0;
    clip->settings.chromaSmoothOption = 0;
    clip->settings.patternNoiseOption = 0;
}

/* Set app GUI According to clipinfo struct */
void setAppGUIFromClip(clipInfo_t * clip)
{
    setAppNewMlvClip(clip->path);
    App->exposureSlider.doubleValue = clip->settings.exposureSlider;
    App->saturationSlider.doubleValue = clip->settings.saturationSlider;
    App->kelvinSlider.doubleValue = clip->settings.kelvinSlider;
    App->tintSlider.doubleValue = clip->settings.tintSlider;
    App->darkStrengthSlider.doubleValue = clip->settings.darkStrengthSlider;
    App->darkRangeSlider.doubleValue = clip->settings.darkRangeSlider;
    App->lightStrengthSlider.doubleValue = clip->settings.lightStrengthSlider;
    App->lightRangeSlider.doubleValue = clip->settings.lightRangeSlider;
    App->lightenSlider.doubleValue = clip->settings.lightenSlider;
    App->sharpnessSlider.doubleValue = clip->settings.sharpnessSlider;
    App->chromaBlurSlider.doubleValue = clip->settings.chromaBlurSlider;
    App->highlightReconstructionSelector.state = clip->settings.highlightReconstructionSelector;
    App->alwaysUseAmazeSelector.state = clip->settings.alwaysUseAmazeSelector;
    App->chromaSeparationSelector.state = clip->settings.chromaSeparationSelector;
    [App->imageProfile selectItemAtIndex: clip->settings.imageProfile];
    App->fixRawSelector.state = clip->settings.fixRawSelector;
    App->dualISOOption.selectedSegment = clip->settings.dualISOOption;
    App->focusPixelOption.selectedSegment = clip->settings.focusPixelOption ;
    App->badPixelOption.selectedSegment = clip->settings.badPixelOption;
    App->stripeFixOption.selectedSegment = clip->settings.stripeFixOption;
    App->chromaSmoothOption.selectedSegment = clip->settings.chromaSmoothOption;
    App->patternNoiseOption.selectedSegment = clip->settings.patternNoiseOption;
    [App->session.clipTable reloadData];
    syncGUI();
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
    if (masxml_size > (1 << 23))
    {
        NSLog(@"MASXML is over 8MB.");
        return;
    }

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
    App->session.clipCount = 0;
    return;
}

@implementation MLVListDelegate

/* Get label or whatevr */
- (NSView *)tableView:(NSTableView *)tableView
   viewForTableColumn:(NSTableColumn *)tableColumn
                  row:(NSInteger)row
{
    // Get an existing cell with the MyView identifier if it exists
    NSTextField * result = [tableView makeViewWithIdentifier:@"MLV" owner:tableView];
    // There is no existing cell to reuse so create a new one
    if (result == nil) {
        /* Create new cell */
        result = [[NSTextField alloc] initWithFrame:NSMakeRect(0,0,0,0)];
        /* Look nice */
        [result setLabelStyle];
        /* This identifier allows the cell to be reused. */
        result.identifier = @"MLV";
    }
    /* Set string value */
    char mlvFileName[128] = {0};
    char * mlvNameStart = App->session.clipInfo[row].path + strlen(App->session.clipInfo[row].path)-1;
    while (mlvNameStart[-1] != '/') mlvNameStart--;
    memcpy(mlvFileName,mlvNameStart,strlen(mlvNameStart)-4);
    result.stringValue = [NSString stringWithFormat:@"%s", mlvFileName];
    /* Highlight current clip in bold */
    if (row == App->session.currentClip) {
        [result setFont: [NSFont boldSystemFontOfSize:12.0f]];
    } else {
        [result setFont: [NSFont systemFontOfSize:12.0f]];
    }
    return result;
}

@end

@implementation MLVListDataSource
/* How many MLV'z r open */
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
    return App->session.clipCount;
}
@end


@implementation NSTableView (sessionMethods)
/* When a clip is double clicked in the table view */
-(void)doubleClickSetClip
{
    /* Save current clip's settings */
    saveClipInfo(App->session.clipInfo + App->session.currentClip);
    /* Open next clip */
    App->session.currentClip = [self clickedRow];
    setAppGUIFromClip(App->session.clipInfo + App->session.currentClip);
}
@end