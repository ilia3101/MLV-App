/* Kind of like a makefile within a makefile, Currently used 
 * to put build PC's hostname within app title as a constant
 * and many other things. */

#import "Cocoa/Cocoa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Host name */
#include <unistd.h>

#include "gui_stuff/style_config.h"


int main(int argc, char * argv[])
{
    /* Generate App title with ultimate debugging build info in a temporary .h file */
    char app_name[1024];
    char host_name[1024];
    char commit[1024] = {0};

    gethostname(host_name, 1024);

    /* Get latest commit id to put in app name */
    system("git rev-parse HEAD >> commithash");
    FILE * commithash = fopen("commithash", "rb");
    fseek(commithash, 0, SEEK_END);
    int size = ftell(commithash);
    fseek(commithash, 0, SEEK_SET);
    fread(commit, size, sizeof(char), commithash);
    fclose(commithash);
    system("rm commithash");
    commit[strlen(commit)-1] = 0;

    /* Get macOS version */
    const char * _Nullable macOS_version = [[[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"] objectForKey:@"ProductVersion"] UTF8String];

    snprintf(app_name, 1024, APP_NAME " (" __DATE__ " " __TIME__ " @%s [%s] %s)", host_name, macOS_version, commit);

    printf("Initial app name: %s\n", app_name);

    FILE * app_name_header = fopen("app_defines.h", "wb");

    /* Here's our beautiful header file */
    fprintf(app_name_header, "#ifndef _app_window_title_h_\n");
    fprintf(app_name_header, "#define _app_window_title_h_\n\n");
    fprintf(app_name_header, "#define APP_WINDOW_TITLE \"%s\"\n\n", app_name);

    /* Also date (numerical) */
    NSDateComponents * date = [[NSCalendar currentCalendar] components:NSCalendarUnitDay | NSCalendarUnitMonth | NSCalendarUnitYear fromDate:[NSDate date]];
    fprintf(app_name_header, "#define APP_BUILD_YEAR %i\n", (int)[date year]);
    fprintf(app_name_header, "#define APP_BUILD_MONTH %i\n", (int)[date month]);
    fprintf(app_name_header, "#define APP_BUILD_DAY %i\n\n", (int)[date day]);

    fprintf(app_name_header, "#endif\n");

    fclose(app_name_header);
}