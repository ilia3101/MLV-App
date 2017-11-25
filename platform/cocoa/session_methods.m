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
    clip->settings.chromaSeparationSelector = [App->chromaSeparationSelector state];
    clip->settings.imageProfile = [App->imageProfile indexOfSelectedItem];
    clip->settings.fixRawSelector = [App->fixRawSelector state];
    clip->settings.dualISOOption = [App->dualISOOption indexOfSelectedItem];
    clip->settings.focusPixelOption = [App->focusPixelOption indexOfSelectedItem];
    clip->settings.focusPixelMethodOption = [App->focusPixelMethodOption indexOfSelectedItem];
    clip->settings.badPixelOption = [App->badPixelOption indexOfSelectedItem];
    clip->settings.badPixelMethodOption = [App->badPixelMethodOption indexOfSelectedItem];
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
    clip->settings.chromaSeparationSelector = NSOffState;
    clip->settings.imageProfile = PROFILE_TONEMAPPED;
    clip->settings.fixRawSelector = NSOffState;
    clip->settings.dualISOOption = 0;
    clip->settings.focusPixelOption = 0;
    clip->settings.focusPixelMethodOption = 0;
    clip->settings.badPixelOption = 0;
    clip->settings.badPixelMethodOption = 0;
    clip->settings.stripeFixOption = 0;
    clip->settings.chromaSmoothOption = 0;
    clip->settings.patternNoiseOption = 0;
}

/* Set app GUI According to clipinfo struct */
void setAppGUIFromClip(clipInfo_t * clip)
{
    setAppNewMlvClip(clip->path);
    [App->exposureSlider setDoubleValue: clip->settings.exposureSlider];
    [App->saturationSlider setDoubleValue: clip->settings.saturationSlider];
    [App->kelvinSlider setDoubleValue: clip->settings.kelvinSlider];
    [App->tintSlider setDoubleValue: clip->settings.tintSlider];
    [App->darkStrengthSlider setDoubleValue: clip->settings.darkStrengthSlider];
    [App->darkRangeSlider setDoubleValue: clip->settings.darkRangeSlider];
    [App->lightStrengthSlider setDoubleValue: clip->settings.lightStrengthSlider];
    [App->lightRangeSlider setDoubleValue: clip->settings.lightRangeSlider];
    [App->lightenSlider setDoubleValue: clip->settings.lightenSlider];
    [App->sharpnessSlider setDoubleValue: clip->settings.sharpnessSlider];
    [App->chromaBlurSlider setDoubleValue: clip->settings.chromaBlurSlider];
    App->highlightReconstructionSelector.state = clip->settings.highlightReconstructionSelector;
    App->chromaSeparationSelector.state = clip->settings.chromaSeparationSelector;
    [App->imageProfile selectItemAtIndex: clip->settings.imageProfile];
    App->fixRawSelector.state = clip->settings.fixRawSelector;
    App->dualISOOption.selectedSegment = clip->settings.dualISOOption;
    App->focusPixelOption.selectedSegment = clip->settings.focusPixelOption;
    App->focusPixelMethodOption.selectedSegment = clip->settings.focusPixelMethodOption;
    App->badPixelOption.selectedSegment = clip->settings.badPixelOption;
    App->badPixelMethodOption.selectedSegment = clip->settings.badPixelMethodOption;
    App->stripeFixOption.selectedSegment = clip->settings.stripeFixOption;
    App->chromaSmoothOption.selectedSegment = clip->settings.chromaSmoothOption;
    App->patternNoiseOption.selectedSegment = clip->settings.patternNoiseOption;
    [App->session.clipTable reloadData];
    syncGUI();
}

/* Called from -(void)openSessionDialog - currently only loads first clip */
void appWriteSession(char * sessionPath)
{
    /* Open the MASXML file 4 reading */
    FILE * session_file = fopen(sessionPath, "wb");

    #define TAB "    "
    #define TAB2 TAB TAB
    fprintf(session_file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(session_file, "<mlv_files>\n");
    for (int i = 0; i < App->session.clipCount; i++)
    {
        clipInfo_t * clip = App->session.clipInfo + i;
        fprintf(session_file, TAB "<clip path=\"%s\">\n", clip->path);

        #define MASXML_TYPE double
        #define MASXML_TYPE_ESC "%.3f"
        fprintf(session_file, TAB2 "<exposure>"MASXML_TYPE_ESC"</exposure>\n", (MASXML_TYPE)(clip->settings.exposureSlider * 100.0));
        fprintf(session_file, TAB2 "<temperature>"MASXML_TYPE_ESC"</temperature>\n", (MASXML_TYPE)(clip->settings.kelvinSlider * (KELVIN_MAX - KELVIN_MIN) + KELVIN_MIN));
        fprintf(session_file, TAB2 "<tint>"MASXML_TYPE_ESC"</tint>\n", (MASXML_TYPE)(clip->settings.tintSlider * 10.0));
        fprintf(session_file, TAB2 "<saturation>"MASXML_TYPE_ESC"</saturation>\n", (MASXML_TYPE)(clip->settings.saturationSlider * 100.0));
        fprintf(session_file, TAB2 "<ds>"MASXML_TYPE_ESC"</ds>\n", (MASXML_TYPE)(clip->settings.darkStrengthSlider * 100.0));
        fprintf(session_file, TAB2 "<dr>"MASXML_TYPE_ESC"</dr>\n", (MASXML_TYPE)(clip->settings.darkRangeSlider * 100.0));
        fprintf(session_file, TAB2 "<ls>"MASXML_TYPE_ESC"</ls>\n", (MASXML_TYPE)(clip->settings.lightStrengthSlider * 100.0));
        fprintf(session_file, TAB2 "<lr>"MASXML_TYPE_ESC"</lr>\n", (MASXML_TYPE)(clip->settings.lightRangeSlider * 100.0));
        fprintf(session_file, TAB2 "<lightening>"MASXML_TYPE_ESC"</lightening>\n", (MASXML_TYPE)(clip->settings.lightenSlider * 100.0));
        fprintf(session_file, TAB2 "<sharpen>"MASXML_TYPE_ESC"</sharpen>\n", (MASXML_TYPE)(clip->settings.sharpnessSlider * 100.0));
        fprintf(session_file, TAB2 "<chromaBlur>"MASXML_TYPE_ESC"</chromaBlur>\n", (MASXML_TYPE)(clip->settings.chromaBlurSlider * 100.0));
        fprintf(session_file, TAB2 "<highlightReconstruction>%i</highlightReconstruction>\n", (int)clip->settings.highlightReconstructionSelector);
        fprintf(session_file, TAB2 "<chromaSeparation>%i</chromaSeparation>\n", (int)clip->settings.chromaSeparationSelector);
        fprintf(session_file, TAB2 "<profile>%i</profile>\n", (int)clip->settings.imageProfile);
        fprintf(session_file, TAB2 "<rawFixesEnabled>%i</rawFixesEnabled>\n", (int)clip->settings.chromaSeparationSelector);
        fprintf(session_file, TAB2 "<verticalStripes>%i</verticalStripes>\n", (int)clip->settings.stripeFixOption);
        fprintf(session_file, TAB2 "<focusPixels>%i</focusPixels>\n", (int)clip->settings.focusPixelOption);
        fprintf(session_file, TAB2 "<fpiMethod>%i</fpiMethod>\n", (int)clip->settings.focusPixelMethodOption);
        fprintf(session_file, TAB2 "<badPixels>%i</badPixels>\n", (int)clip->settings.badPixelOption);
        fprintf(session_file, TAB2 "<bpiMethod>%i</bpiMethod>\n", (int)clip->settings.badPixelMethodOption);
        int CSOption; switch ((int)clip->settings.chromaSmoothOption) {
            case 0: CSOption = 0; break;
            case 1: CSOption = 2; break;
            case 2: CSOption = 3; break;
            case 3: CSOption = 5; break;
        } fprintf(session_file, TAB2 "<chromaSmooth>%i</chromaSmooth>\n", (int)CSOption);
        fprintf(session_file, TAB2 "<patternNoise>%i</patternNoise>\n", (int)clip->settings.patternNoiseOption);
        // TODO : dual iso session saving 
        // fprintf(session_file, TAB2 "<dualIso>%i</dualIso>\n", clip->settings.patternNoiseOption);

        fprintf(session_file, TAB "</clip>\n");
    }
    fprintf(session_file, "</mlv_files>");
    #undef TAB
    #undef TAB2

    free(session_file);
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