#ifndef _delegate_h_
#define _delegate_h_

#import <Cocoa/Cocoa.h>

@interface MLVAppDelegate : NSObject <NSApplicationDelegate>

/* To do "Open With" */
- (BOOL)application: (NSApplication *)sender openFile: (NSString *)filename;

@end


#endif