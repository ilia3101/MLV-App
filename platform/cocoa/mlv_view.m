#import "mlv_view.h"

/* The MLVView class */
@implementation MLVView

-(void)drawRect: (NSRect)bounds
{
    [super drawRect:bounds];
}

-(void)updateView
{
    NSImage * image = self.image;
    [self setImage: nil];
    [self setImage: image];
    [self setNeedsDisplay: YES];
}

@end