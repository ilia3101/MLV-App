/* Kind of like a makefile within a makefile, Currently used 
 * to put build PC's hostname within app title as a constant...
 * before it would show the users hotname! not builder's. doh. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Host name */
#include <unistd.h>

#include "gui_stuff/style_config.h"


int main(int argc, char * argv[])
{
    /* Generate App title with build info in a temporary .h file */
    char * app_name = malloc(1024);
    char * host_name = malloc(1024);

    gethostname(host_name, 1023);

    snprintf(app_name, 1023, APP_NAME " (" __DATE__ " " __TIME__ " @%s)", host_name);

    printf("Initial app name: %s\n", app_name);

    FILE * app_name_header = fopen("app_window_title.h", "wb");

    /* Here's our beautiful header file */
    fprintf(app_name_header, "#ifndef _app_window_title_h_\n");
    fprintf(app_name_header, "#define _app_window_title_h_\n\n");
    fprintf(app_name_header, "#define APP_WINDOW_TITLE \"%s\"\n\n", app_name);
    fprintf(app_name_header, "%s\n", "#endif");

    fclose(app_name_header);

    free(host_name);
    free(app_name);
}
