/* Generates an info.plist file for the app */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host name */
#include <unistd.h>

#define TAB "    "

#define INFO_PLIST_START \
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" \
	"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">" \
	"<plist version=\"1.0\">\n" \
	"<dict>\n"

#define INFO_PLIST_END \
	"</dict>\n" \
	"</plist>\n" \

#define INFO_PLIST_PROPERTY(KEY, STRING) \
	TAB "<key>" KEY "</key>\n" \
	TAB "<string>" STRING "</string>\n"


/* App name should be single commandline argument */
int main(int argc, char * argv[])
{
    char * host_name = malloc(1024);
    gethostname(host_name, 1023);

    FILE * info_plist = fopen("info.plist", "wb");

    /* What an amazing plist */
    fprintf(info_plist, INFO_PLIST_START);
    fprintf(info_plist, INFO_PLIST_PROPERTY("CFBundleDisplayName", "%s"), argv[1]);
    fprintf(info_plist, INFO_PLIST_PROPERTY("CFBundleExecutable", "%s"), argv[1]);
    fprintf(info_plist, INFO_PLIST_PROPERTY("CFBundleIdentifier", "fm.ilia.%s"), argv[1]);
    fprintf(info_plist, INFO_PLIST_PROPERTY("CFBundleInfoDictionaryVersion", "6.0"));
    fprintf(info_plist, INFO_PLIST_PROPERTY("CFBundleName", "%s"), argv[1]);
    fprintf(info_plist, INFO_PLIST_PROPERTY("CFBundlePackageType", "APPL"));
    fprintf(info_plist, INFO_PLIST_PROPERTY("CFBundleVersion", "(%s @%s)"), __DATE__, host_name);
    fprintf(info_plist, INFO_PLIST_PROPERTY("LSMinimumSystemVersion", "10.6.0"));
    fprintf(info_plist, TAB "<key>LSUIElement</key>\n" TAB "<false/>\n");
    fprintf(info_plist, INFO_PLIST_PROPERTY("NSHumanReadableCopyright", "© 2017 Ilia Sibiryakov"));
    fprintf(info_plist, INFO_PLIST_END);

    fclose(info_plist);

    free(host_name);
}
