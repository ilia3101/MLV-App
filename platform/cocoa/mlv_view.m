#import "mlv_view.h"

@implementation MLVView

-(id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        self.draw = NO;
        self.magnification = 1.0;
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
}


-(void)drawRect:(NSRect)rect
{
    glClearColor(0.0353f, 0.0353f, 0.0353f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (self.draw)
    {
        /* Where to draw for correct aspect ratio */
        float viewAspect = (float)NSWidth(rect) / (float)NSHeight(rect);
        float imageAspect = (float)self.image_width / (float)self.image_height;

        /* Magnification */
        float scaleFactor;
        
        if (self.one_to_one_zoom) {
            if (imageAspect > viewAspect)
                scaleFactor = (float)self.image_width / NSWidth(rect);
            else
                scaleFactor = (float)self.image_height / NSHeight(rect);
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
        GLuint Scaling = GL_LINEAR;
        glBindTexture(GL_TEXTURE_2D, TextureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Scaling);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Scaling);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, self.image_width, self.image_height, 0, GL_RGB, GL_UNSIGNED_BYTE, self.image_data);
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