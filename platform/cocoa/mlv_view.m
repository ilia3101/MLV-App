#import "mlv_view.h"
#include <pthread.h>

void change_scale(MLVView * self)
{
    self.changing_scale = YES;

    int steps = 100;

    for (int i = 0; i < steps; ++i)
    {
        self.real_stretch = (self.stretch*i + self.real_stretch*(steps-i)) / (float)steps;
        usleep(1000000/70);
        [self performSelectorOnMainThread:@selector(updateView) withObject:nil waitUntilDone:NO];
    }

    self.real_stretch = self.stretch;

    self.changing_scale = NO;
}

@implementation MLVView

- (void)changeScale
{
    pthread_t thread;
    if (!self.changing_scale)
    {
        pthread_create(&thread, NULL, (void * _Nullable)&change_scale, self);
    }
}

- (void)rightMouseDown:(NSEvent *)theEvent
{
    NSMenu *theMenu = [[NSMenu alloc] initWithTitle:@"MLVView Settings"];
    int index = 0;
    [theMenu insertItemWithTitle:@"1.0x (source)" action:@selector(setAnamorphicNone) keyEquivalent:@"" atIndex:index++];
    [theMenu addItem:[NSMenuItem separatorItem]]; index++;
    [theMenu insertItemWithTitle:@"1.67x Vertical" action:@selector(setStretch1_67x) keyEquivalent:@"" atIndex:index++];
    [theMenu addItem:[NSMenuItem separatorItem]]; index++;
    [theMenu insertItemWithTitle:@"1.25x" action:@selector(setAnamorphic1_25x) keyEquivalent:@"" atIndex:index++];
    [theMenu insertItemWithTitle:@"1.33x" action:@selector(setAnamorphic1_33x) keyEquivalent:@"" atIndex:index++];
    [theMenu insertItemWithTitle:@"1.5x" action:@selector(setAnamorphic1_5x) keyEquivalent:@"" atIndex:index++];
    [theMenu insertItemWithTitle:@"1.75x" action:@selector(setAnamorphic1_75x) keyEquivalent:@"" atIndex:index++];
    [theMenu insertItemWithTitle:@"2.0x" action:@selector(setAnamorphic2x) keyEquivalent:@"" atIndex:index++];
    [theMenu addItem:[NSMenuItem separatorItem]]; index++;
    [theMenu insertItemWithTitle:@"2.35:1" action:@selector(setAspect2_35) keyEquivalent:@"" atIndex:index++];
    [theMenu insertItemWithTitle:@"2.50:1" action:@selector(setAspect2_50) keyEquivalent:@"" atIndex:index++];
    [theMenu insertItemWithTitle:@"2.67:1" action:@selector(setAspect2_67) keyEquivalent:@"" atIndex:index++];
    [NSMenu popUpContextMenu:theMenu withEvent:theEvent forView:self];
}

-(void)setStretch1_67x {
    self.stretch = 1.0/1.666667;
    [self changeScale];
}
-(void)setAnamorphic2x {
    self.stretch = 2.0f;
    [self changeScale];
}
-(void)setAnamorphic1_75x {
    self.stretch = 1.75f;
    [self changeScale];
}
-(void)setAnamorphic1_5x {
    self.stretch = 1.5f;
    [self changeScale];
}
-(void)setAnamorphic1_33x {
    self.stretch = 1.33f;
    [self changeScale];
}
-(void)setAnamorphic1_25x {
    self.stretch = 1.25f;
    [self changeScale];
}
-(void)setAnamorphicNone {
    self.stretch = 1.0f;
    [self changeScale];
}
-(void)setAspect2_35 {
    self.stretch = 2.35 / [self aspect];
    [self changeScale];
}
-(void)setAspect2_50 {
    self.stretch = 2.50 / [self aspect];
    [self changeScale];
}
-(void)setAspect2_67 {
    self.stretch = 2.666667 / [self aspect];
    [self changeScale];
}

-(double)aspect {
    return (double)self.image_width / (double)self.image_height;
}

-(id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        self.draw = NO;
        self.magnification = 1.0;
        self.stretch = 1.0;
        self.real_stretch = 1.0;
        self.changing_scale = NO;
    }
    return self;
}

-(void)setSourceImage:(void *)imageData width:(int)width height:(int)height bitDepth:(int)bitsPerComponent
{
    self.draw = NO;

    self.image_width = width;
    self.image_height = height;
    self.image_bpp = bitsPerComponent;
    self.image_data = imageData;

    self.draw = YES;

    [self setAnamorphicNone];
}

-(void)drawRect:(NSRect)rect
{
    glClearColor(0.0353f, 0.0353f, 0.0353f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (self.draw)
    {
        /* Where to draw for correct aspect ratio */
        float viewAspect = (float)NSWidth(rect) / (float)NSHeight(rect);
        float imageAspect = (float)self.image_width / (float)self.image_height * self.real_stretch;

        /* Magnification */
        float scaleFactor;
        GLuint Scaling = GL_LINEAR;
        
        if (self.one_to_one_zoom) {
            if (imageAspect > viewAspect)
                scaleFactor = (float)self.image_width / NSWidth(rect);
            else
                scaleFactor = (float)self.image_height / NSHeight(rect);
            Scaling = GL_NEAREST;
        } else {
            scaleFactor = self.magnification;
        }

        float pointTL[2] = {-scaleFactor,  scaleFactor};
        float pointBL[2] = {-scaleFactor, -scaleFactor};
        float pointBR[2] = { scaleFactor, -scaleFactor};
        float pointTR[2] = { scaleFactor,  scaleFactor};

        /* Fit to aspect ratio */
        int divideElement = (imageAspect > viewAspect) ? 1 : 0;
        float divisor = (imageAspect > viewAspect) ? (imageAspect/viewAspect) : (viewAspect/imageAspect);
        pointBL[divideElement] /= divisor;
        pointBR[divideElement] /= divisor;
        pointTL[divideElement] /= divisor;
        pointTR[divideElement] /= divisor;

        /* Create texture */
        GLuint TextureID = 0;
        glBindTexture(GL_TEXTURE_2D, TextureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Scaling);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Scaling);
        if (self.image_bpp == 8)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, self.image_width, self.image_height, 0, GL_RGB, GL_UNSIGNED_BYTE, self.image_data);
        else if (self.image_bpp == 16)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16, self.image_width, self.image_height, 0, GL_RGB, GL_UNSIGNED_SHORT, self.image_data);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_2D, TextureID);

        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(pointTL[0], pointTL[1]); /* Top left */
        glTexCoord2f(0.0f, 1.0f); glVertex2f(pointBL[0], pointBL[1]); /* Bottom left */
        glTexCoord2f(1.0f, 1.0f); glVertex2f(pointBR[0], pointBR[1]); /* Bottom right */
        glTexCoord2f(1.0f, 0.0f); glVertex2f(pointTR[0], pointTR[1]); /* Top right */
        glEnd();
        glDisable(GL_TEXTURE_2D);

        glDeleteTextures(1, &TextureID);
    }

    glFlush();
}

-(void)updateView
{
    [self setNeedsDisplay: YES];
}

/* Double tap on magic mouse/two-finger double tap on trackpads */
- (void)smartMagnifyWithEvent:(NSEvent *)event
{
    if (!self.one_to_one_zoom) {
        self.one_to_one_zoom = YES;
    } else {
        self.one_to_one_zoom = NO;
    } [self updateView];
}

-(BOOL)acceptsFirstResponder {return YES;}


@end