#ifndef _slider_unit_h_
#define _slider_unit_h_

/* A Cocoa slider unit that displays it's value and a name. Layout:
 *
 *     * ----------------------------------------------- *
 *     | Name String                        Slider Value |
 *     |                       *--*                      |
 *     | S--l--i--d--e--r-----|    |-------------------  |
 *     |                       *--*                      |
 *     * ----------------------------------------------- *
 *
 */

@interface SliderUnit : NSView

/* Initialise with frame, recommended size: 200x40 + 8px gap between units */
-(void)initWithFrame: (NSRect)rect;

/* Set slider name */
-(void)setLabelText: (char *)string;

/* Runs on slider change / value change */
-(void)run;

/* Set value range */
-(void)setLinearValueRangeFrom:(double)from to:(double)to;
/* Set value range (non-linear) */
-(void)setNonLinearValueRangeFrom:(double)from middle:(double)middle to:(double)to;

/* Get value */
-(double)doubleValue;
-(float)floatValue;

/* Set method which will be activated on slider change */
-(void)setMethod: (SEL)method;

@end