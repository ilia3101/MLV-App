#ifndef _mlv_view_h_
#define _mlv_view_h_

#import <Cocoa/Cocoa.h>
#include <OpenGL/gl.h>

@interface MLVView : NSOpenGLView

-(id)initWithFrame:(NSRect)frame;

/* Set source of image */
-(void)setSourceImage:(void *)imageData width:(int)width height:(int)height bitDepth:(int)bitsPerComponent;

/* A superior update method that works even 
 * if the window is bigger than 640x480 :D */
-(void)updateView;

/* The main drawing fucntion (no need to call) */
-(void)drawRect: (NSRect)bounds;

/* Properties about the image */
@property int image_width;
@property int image_height;
@property int image_bpp;

@property void * image_data; /* Pointer to supplier of image data */

@property int draw; /* Flag */

@end

#endif