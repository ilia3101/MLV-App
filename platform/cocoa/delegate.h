#ifndef _delegate_h_
#define _delegate_h_

#import <Cocoa/Cocoa.h>

@interface MLVAppDelegate : NSObject <NSApplicationDelegate>

/* What happens when app loads (update checks etc) */
- (void)applicationDidFinishLaunching: (NSApplication *)sender;
- (void)applicationWillFinishLaunching: (NSNotification *)notification;

/* To do "Open With" */
- (BOOL)application: (NSApplication *)sender openFile: (NSString *)filename;

/* Close when window closes */
- (BOOL)applicationShouldTerminateAfterLastWindowClosed: (NSApplication *)sender;

@end


#endif