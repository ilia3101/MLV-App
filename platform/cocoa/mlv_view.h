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

/* Double tap on magic mouse/two-finger double tap on trackpads */
-(void)smartMagnifyWithEvent:(NSEvent *)event;
-(BOOL)acceptsFirstResponder;

/* Set anamorphic stretching */
-(void)setAnamorphic2x;
-(void)setAnamorphic1_75x;
-(void)setAnamorphic1_67x;
-(void)setAnamorphic1_5x;
-(void)setAnamorphic1_33x;
-(void)setAnamorphic1_25x;
-(void)setAnamorphicNone;

/* Properties about the image */
@property int image_width;
@property int image_height;
@property int image_bpp;
@property void * image_data; /* Pointer to supplier of image data */

@property float magnification;
@property float stretch;
@property BOOL one_to_one_zoom;

@property BOOL draw;

@end

#endif