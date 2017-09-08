#include <math.h>
#include <string.h>
#include <unistd.h>

#import "Cocoa/Cocoa.h"

#import "slider_unit.h"

@implementation SliderUnit : NSView

/* Initialise with frame, recommended size: 200x40 + 8px gap between units */
-(void)initWithFrame: (NSRect)rect
{
    /* Initialise */
    self = [super initWithFrame: rect];
    /* Now do custom stuff */
    if (self) 
    {
        /* Make the slider */
        self.slider = [ [SliderUnitSlider alloc]
                        initWithFrame: NSMakeRect( [self NSMinX], /* X coord */
                                                   [self NSMinY], /* Y coord */
                                                   /* Width and height */ 
                                                   [self NSMaxX] - [self NSMinX], 32) ];
        [self.slider setParent:self];
    }
    return self;
}

/* Set slider name */
-(void)setLabelText: (char *)string;

/* Runs on slider change / value change */
-(void)run
{
    [self performSelector(self.method)];
}

/* Set value range */
-(void)setLinearValueRangeFrom:(double)from to:(double)to;
/* Set value range (non-linear) */
-(void)setNonLinearValueRangeFrom:(double)from middle:(double)middle to:(double)to;

/* Get value */
-(double)doubleValue;
-(float)floatValue;

/* Set method which will be activated on slider change */
-(void)setMethod: (SEL)method
{
    return;
}


/* Consists of 3 properties */
@property NSTextField * nameLabel;
@property NSTextField * valueLabel;
@property SliderUnitSlider * slider; /* Subclass of NSSlider/Cell */

/* Pointer ish thing to method */
@property SEL method;

/* Range */
@property double rangeFrom;
@property double rangeTo;
@property double rangeMiddle; /* Only if not linear */
/* Is it linear? */
@property int isLinear;

@end




/* Subclasses used in SliderUnit follow */

/* The slider inside of SliderUnit will do this */
@implementation SliderUnitSlider : NSSlider

-(void)initWithFrame: (NSRect)rect
{
    self = [super initWithFrame: rect];
}

/* WHich slider unit it will relate to */
-(void)setSliderUnit: (SliderUnit *);
{
    self.unit = unit;
}

@property SliderUnit * unit;

@end