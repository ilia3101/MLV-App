#include "delegate.h"
#include "../../src/mlv_include.h"
#include "main_methods.h"
#include "godobject.h"
extern godObject_t * App;

@implementation MLVAppDelegate

- (BOOL)application:(NSApplication *)sender openFile:(NSString *)filename
{
    if (setAppNewMlvClip([filename UTF8String]))
    {
        return YES;
    }
    else
    {
        return NO;
    }
}

@end