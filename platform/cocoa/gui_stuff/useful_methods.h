/* Some useful Objective-C methods for GUI */

#ifndef _useful_methods_
#define _useful_methods_

#include "anchor_methods.h"

/* NSTextField methods */
@interface NSTextField (usefulMethods)

/* setLabelStyle is an easy way to put text labels in GUI
 * Allocate an NSTextField first(which is not a label)
 * then make it in to a label:
 * [textfield_name setLabelStyle]; */
-(void)setLabelStyle;

/* Anchor methods macro (yes, what an unhelpful 'interface', 
 * look in anchor_methods.h or how they are used, 
 * I'm sorry for the awful code ) */
ANCHOR_METHODS_INTERFACE

@end

/* NSTextView methods */
@interface NSTextView (usefulMethods)
-(void)setLabelStyle;
ANCHOR_METHODS_INTERFACE
@end

/* NSButton methods */

@interface NSButton (usefulMethods)
ANCHOR_METHODS_INTERFACE
@end

/* NSSlider methods */

@interface NSSlider (usefulMethods)
ANCHOR_METHODS_INTERFACE
@end

/* Drop down menu methods */

@interface NSPopUpButton (usefulMethods)
ANCHOR_METHODS_INTERFACE
@end

#endif