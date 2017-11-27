/* Some useful Objective-C methods for GUI */

#ifndef _useful_methods_
#define _useful_methods_

/* NSTextField methods */
@interface NSTextField (usefulMethods)

/* setLabelStyle is an easy way to put text labels in GUI
 * Allocate an NSTextField first(which is not a label)
 * then make it in to a label:
 * [textfield_name setLabelStyle]; */
-(void)setLabelStyle;
-(void)setLabelStyleHighlightedWithColour: (NSColor *)colour;

@end

/* Anchor """"methods"""" */
#define AnchorBottom(OBJECT, CHOICE) { \
    if (CHOICE) OBJECT.autoresizingMask |= NSViewMaxYMargin; \
    else OBJECT.autoresizingMask &= ~NSViewMaxYMargin; \
}
#define AnchorTop(OBJECT, CHOICE) { \
    if (CHOICE) OBJECT.autoresizingMask |= NSViewMinYMargin; \
    else OBJECT.autoresizingMask &= ~NSViewMinYMargin; \
}
#define AnchorLeft(OBJECT, CHOICE) { \
    if (CHOICE) OBJECT.autoresizingMask |= NSViewMaxXMargin; \
    else OBJECT.autoresizingMask &= ~NSViewMaxXMargin; \
}
#define AnchorRight(OBJECT, CHOICE) { \
    if (CHOICE) OBJECT.autoresizingMask |= NSViewMinXMargin; \
    else OBJECT.autoresizingMask &= ~NSViewMinXMargin; \
}

#endif
