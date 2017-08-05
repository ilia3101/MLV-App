/* Defining a few Methods for anchoring ui elements as macros so they don't 
 * have to be rewritten for every class of object they are added to 
 * maybe not the proper way to do this but it works
 * just add 'ANCHOR_METHODS_INTERFACE' to the inteface of a class
 * and 'ANCHOR_METHODS_IMPLEMENTATION' to its implementation */

#ifndef _anchor_methods_
#define _anchor_methods_


#define ANCHOR_METHOD_INTERFACE(METHOD_NAME) \
-(void)METHOD_NAME:(bool)anchor;

/* This method takes a bool - YES or NO to set anchoring */
#define ANCHOR_METHOD_IMPLEMENTATION(METHOD_NAME, MASK_NAME) \
-(void)METHOD_NAME:(bool)anchor { \
    /* Adding mask */ \
    if (anchor) self.autoresizingMask |= MASK_NAME; \
    /* Removing that mask */ \
    else self.autoresizingMask &= ~MASK_NAME; \
}

#define ANCHOR_BOTTOM_INTERFACE ANCHOR_METHOD_INTERFACE(anchorBottom)
#define ANCHOR_BOTTOM_IMPLEMENTATION ANCHOR_METHOD_IMPLEMENTATION(anchorBottom, NSViewMaxYMargin)

#define ANCHOR_TOP_INTERFACE ANCHOR_METHOD_INTERFACE(anchorTop)
#define ANCHOR_TOP_IMPLEMENTATION ANCHOR_METHOD_IMPLEMENTATION(anchorTop, NSViewMinYMargin)

#define ANCHOR_LEFT_INTERFACE ANCHOR_METHOD_INTERFACE(anchorLeft)
#define ANCHOR_LEFT_IMPLEMENTATION ANCHOR_METHOD_IMPLEMENTATION(anchorLeft, NSViewMaxXMargin)

#define ANCHOR_RIGHT_INTERFACE ANCHOR_METHOD_INTERFACE(anchorRight)
#define ANCHOR_RIGHT_IMPLEMENTATION ANCHOR_METHOD_IMPLEMENTATION(anchorRight, NSViewMinXMargin)

#define ANCHOR_METHODS_INTERFACE \
ANCHOR_BOTTOM_INTERFACE \
ANCHOR_TOP_INTERFACE \
ANCHOR_LEFT_INTERFACE \
ANCHOR_RIGHT_INTERFACE

#define ANCHOR_METHODS_IMPLEMENTATION \
ANCHOR_BOTTOM_IMPLEMENTATION \
ANCHOR_TOP_IMPLEMENTATION \
ANCHOR_LEFT_IMPLEMENTATION \
ANCHOR_RIGHT_IMPLEMENTATION


#endif