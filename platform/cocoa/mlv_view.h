#ifndef _mlv_view_h_
#define _mlv_view_h_

#import <Cocoa/Cocoa.h>

@interface MLVView : NSImageView

-(void)drawRect: (NSRect)bounds;
/* A superior update method that works even 
 * if the window is bigger than 640x480 :D */
-(void)updateView;

@end

#endif