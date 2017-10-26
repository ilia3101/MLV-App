#import "mlv_view.h"

#define MARGIN 6

@implementation MLVView

-(id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        self.draw = 0;
    }
    return self;
}

-(void)setSourceImage:(void *)imageData width:(int)width height:(int)height bitDepth:(int)bitsPerComponent
{
    self.draw = 0;

    self.image_width = width;
    self.image_height = height;
    self.image_bpp = bitsPerComponent;
    self.image_data = imageData;

    int size = width * height;
    int bits_per_pixel = bitsPerComponent * 3;
    int bytes_per_pixel = bits_per_pixel / 8;

    // GLuint TextureID = self.TextureID;
    // if (self.TextureID) glDeleteTextures(1, (const GLuint *)&TextureID);	
	// glGenTextures(1, (GLuint *)&TextureID);
	// glBindTexture(GL_TEXTURE_2D, self.TextureID);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, imageData);
	// glBindTexture(GL_TEXTURE_2D, 0);
	// glBindTexture(GL_TEXTURE_2D, self.TextureID);

    // self.TextureID = TextureID;

    self.draw = 1;
}


-(void)drawRect:(NSRect)rect
{ 
    if (self.draw)
    {
        GLuint TextureID = 0;

        glBindTexture(GL_TEXTURE_2D, TextureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, self.image_width, self.image_height, 0, GL_RGB, GL_UNSIGNED_BYTE, self.image_data);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_2D, TextureID);

        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        glTexCoord2i(0, 0); glVertex2f(-1.0,  1.0);
        glTexCoord2i(0, 1); glVertex2f(-1.0, -1.0);
        glTexCoord2i(1, 1); glVertex2f( 1.0, -1.0);
        glTexCoord2i(1, 0); glVertex2f( 1.0,  1.0);
        glEnd();
        glDisable(GL_TEXTURE_2D);
  
        glFlush();
    }
}

-(void)updateView
{
    [self setNeedsDisplay: YES];
}

@end