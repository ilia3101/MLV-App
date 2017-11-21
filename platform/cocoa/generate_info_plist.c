/* Generates an info.plist file for the app */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host name */
#include <unistd.h>

#define TAB "    "
#define NL "\n"

#define XML_TAG_A(TYPE, EXTRAS) "<" TYPE EXTRAS ">"
#define XML_TAG_B(TYPE) "</" TYPE ">"
#define XML_TAG_C(TYPE) "<" TYPE "/>"

/* Leave extras blank probably */
#define XML_TAG(TYPE, INFO, EXTRAS) \
    XML_TAG_A(TYPE, EXTRAS) INFO XML_TAG_B(TYPE)

#define INFO_PLIST_START \
    XML_TAG_A("?xml", " version=\"1.0\" encoding=\"UTF-8\"?") NL \
	XML_TAG_A("!DOCTYPE", " plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"") NL \
	XML_TAG_A("plist", " version=\"1.0\"") NL \
    XML_TAG_A("dict", "") NL

#define INFO_PLIST_END \
    XML_TAG_B("dict") NL \
	XML_TAG_B("plist") NL

#define INFO_PLIST_PROPERTY(KEY, STRING) \
    TAB XML_TAG("key", KEY, "") NL \
    TAB XML_TAG("string", STRING, "") NL


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
    fprintf(info_plist, INFO_PLIST_PROPERTY("CFBundleIconFile", "MLV App.icns"));
    fprintf(info_plist, TAB XML_TAG("key", "LSUIElement", "") NL TAB XML_TAG_C("false") NL);
    fprintf(info_plist, INFO_PLIST_PROPERTY("NSHumanReadableCopyright", "© 2017 Creators Of MLV App"));
    fprintf(info_plist, INFO_PLIST_PROPERTY("CFBundleIconFile", "MLV App.icns"));
    /* MLV files r supported */
    fprintf(info_plist, TAB XML_TAG("key", "CFBundleDocumentTypes", "") NL);
    fprintf(info_plist, TAB XML_TAG_A("array","") NL);
    fprintf(info_plist, TAB TAB XML_TAG_A("dict","") NL);
    fprintf(info_plist, TAB TAB TAB XML_TAG("key", "CFBundleTypeExtensions", "") NL);
    fprintf(info_plist, TAB TAB TAB XML_TAG_A("array", "") NL);
    fprintf(info_plist, TAB TAB TAB TAB XML_TAG("string", "MLV", "") NL);
    fprintf(info_plist, TAB TAB TAB XML_TAG_B("array") NL);
    fprintf(info_plist, TAB TAB TAB XML_TAG("key", "LSHandlerRank", "") NL);
    fprintf(info_plist, TAB TAB TAB XML_TAG("string", "Default", "") NL);
    fprintf(info_plist, TAB TAB TAB XML_TAG("key", "CFBundleTypeRole", "") NL);
    fprintf(info_plist, TAB TAB TAB XML_TAG("string", "Viewer", "") NL);
    fprintf(info_plist, TAB TAB XML_TAG_B("dict") NL);
    fprintf(info_plist, TAB XML_TAG_B("array") NL);
    fprintf(info_plist, INFO_PLIST_END);

    fclose(info_plist);

    free(host_name);
}
