#import "mlv_view.h"

#define MARGIN 6

@implementation MLVView

-(id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) { 
        self.colorSpace = nil;
        self.provider = nil;
        self.the_image = nil;
        self.draw = 0;
    }
    return self;
}

-(void)setSourceImage:(void *)imageData width:(int)width height:(int)height bitDepth:(int)bitsPerComponent
{
    self.draw = 0;

    self.image_width = width;
    self.image_height = height;
    self.image_bpp = bitsPerComponent;
    self.image_data = imageData;

    int size = width * height;
    int bits_per_pixel = bitsPerComponent * 3;
    int bytes_per_pixel = bits_per_pixel / 8;

    if (self.colorSpace) CGColorSpaceRelease(self.colorSpace);
    self.colorSpace = CGColorSpaceCreateDeviceRGB();

    if (self.provider) CGDataProviderRelease(self.provider);
    self.provider = CGDataProviderCreateWithData(nil, imageData, size * bytes_per_pixel, nil);

    self.draw = 1;
}


-(void)drawRect:(NSRect)rect
{
    if (self.draw)
    {
        /* This gives us the nice grey bordered rectangle */
        [super drawRect:rect];

        CGContextRef context = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];

        /* Set medium interpolation quality | https://developer.apple.com/documentation/coregraphics/cginterpolationquality?language=objc */
        CGContextSetInterpolationQuality(context, kCGInterpolationHigh);

        /* Custom drawing bounds to keep aspect ratio */

        /* Cropped to margin */
        NSRect draw_frame = NSMakeRect( NSMinX(self.bounds)+MARGIN, NSMinY(self.bounds)+MARGIN,
                                        NSWidth(self.bounds)-2*MARGIN, NSHeight(self.bounds)-2*MARGIN );

        double view_aspect = (double)NSWidth(draw_frame) / (double)NSHeight(draw_frame);
        double image_aspect = (double)self.image_width / (double)self.image_height;

        /* Make sure aspect ratio is correct and center the image rectangle */
        if (image_aspect > view_aspect)
        {
            draw_frame = NSMakeRect( NSMinX(draw_frame), NSMinY(draw_frame) + (NSHeight(draw_frame) - NSWidth(draw_frame)/image_aspect)/2,
                                     NSWidth(draw_frame), NSWidth(draw_frame) / image_aspect );
        }
        else
        {
            image_aspect = 1.0 / image_aspect;
            draw_frame = NSMakeRect( NSMinX(draw_frame) + (NSWidth(draw_frame) - NSHeight(draw_frame)/image_aspect)/2, NSMinY(draw_frame), 
                                     NSHeight(draw_frame) / image_aspect, NSHeight(draw_frame) );
        }

        /* CGImage to display */
        CGImageRef image = CGImageCreate( self.image_width, self.image_height,
                                          self.image_bpp, self.image_bpp * 3,
                                          (self.image_bpp/8 * 3) * self.image_width, 
                                          self.colorSpace,
                                          kCGBitmapByteOrderDefault,
                                          self.provider, nil, YES,
                                          kCGRenderingIntentDefault );

        /* Draw */
        CGContextDrawImage(context, draw_frame, image);

        CGImageRelease(image);
    }

}

-(void)updateView
{
    [self setNeedsDisplay: YES];
}

@end