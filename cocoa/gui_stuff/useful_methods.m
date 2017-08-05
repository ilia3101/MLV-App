/* Useful mostly UI related and boring methods */
#import "Cocoa/Cocoa.h"
#include "useful_methods.h"
#include "anchor_methods.h"


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

ANCHOR_METHODS_IMPLEMENTATION

@end

/* NSTextView methods */

@implementation NSTextView (usefulMethods)

-(void)setLabelStyle {
    [self setDrawsBackground:NO];
    [self setEditable:NO];
    [self setSelectable:NO];
}

ANCHOR_METHODS_IMPLEMENTATION

@end

/* NSButton methods */
@implementation NSButton (usefulMethods)
ANCHOR_METHODS_IMPLEMENTATION
@end

/* NSSlider methods */
@implementation NSSlider (usefulMethods)
ANCHOR_METHODS_IMPLEMENTATION
@end

/* Drop down menu methods */
@implementation NSPopUpButton (usefulMethods)
ANCHOR_METHODS_IMPLEMENTATION
@end