/* Useful mostly UI related and boring methods */
#import "Cocoa/Cocoa.h"
#include "useful_methods.h"


/* NSTextField methods */
@implementation NSTextField (usefulMethods)

/* setLabelStyle is an easy way to put text labels in GUI
 * Allocate an NSTextField first(which is not a label)
 * then make it in to a label:
 * [textfield_name setLabelStyle]; */
-(void)setLabelStyle {
    [self setBezeled:NO];
    [self setDrawsBackground:NO];
    [self setEditable:NO];
    [self setSelectable:NO];
}
-(void)setLabelStyleHighlightedWithColour: (NSColor *)colour {
    [self setBezeled:NO];
    [self setDrawsBackground:YES];
    [self setBackgroundColor: colour];
    [self setEditable:NO];
    [self setSelectable:NO];
}

@end