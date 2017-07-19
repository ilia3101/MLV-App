/* Code for reading and writing bitmaps of all kinds.
 * Fun fact: 24bpp bitmaps have colour as B-G-R instead of R-G-B */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// #include "../../debug.h"
#include "../structs/imagestruct.h"
#include "bitmap.h"
#include "../imageio.h"

/* BMP headers... */

#pragma pack(push,1) /* All data packed to 1 byte(to avoid gaps of zeros in file) */

/*  First part of all bitmap files is a 14 byte header, this one is used in BMP files version 2 and up  */
typedef struct
{
    uint16_t    magicnumber;    /* Magic number - must be 0x4D42                    */
    uint32_t    filesize;       /* Size of file, header + image                     */
    uint16_t    nothing1;       /* Empty - but could be used for hidden info        */
    uint16_t    nothing2;       /* Another empty                                    */
    uint32_t    offset;         /* Where image starts, might be 54                  */
}  bitmapfileheader;

/*  First header is followed by a second header, here a few versions... */

/*  Bitmap version 2 header structure */
typedef struct
{
    uint32_t    headersize;     /* Size of this header in bytes - 12                                */
    int16_t     imagewidth;     /* Width of actual image in pixels(not including 4 Byte padding)    */
    int16_t     imageheight;    /* Height of image in pixels                                        */
    uint16_t    planes;         /* Should be 1                                                      */
    uint16_t    bpp;            /* Probably 24                                                      */
}  bitmap2header;

/*  Bitmap version 3 header structure */
typedef struct
{
    uint32_t    headersize;         /* Size of this header in bytes - 40                                */
    uint32_t    imagewidth;         /* Width of actual image in pixels(not including 4 Byte padding)    */
    uint32_t    imageheight;        /* Height of image in pixels                                        */
    uint16_t    planes;             /* Should be 1                                                      */
    uint16_t    bpp;                /* Probably 24                                                      */
    uint32_t    compressiontype;    /* Should be 0 - 3 for bitfields (custom bpp)                       */
    uint32_t    sizeofimage;        /* Size of image data in bytes                                      */
    uint32_t    horizontalppm;      /* Horizontal pixels per meter - useless - set to 2835 == 72 DPI    */
    uint32_t    verticalppm;        /* Same - just set to 2835                                          */
    uint32_t    coloursinimage;     /* Colours in image - also pointless - set to 0                     */
    uint32_t    importantcolours;   /* 'important 'colours in image - 0 means all colors are important  */
}  bitmap3header;

/*  Various bits can be added afterwards... */

/*  Bitmap bitfields structure - specify how many bits each channel uses - used in 16 / 32 bit files    */
typedef struct
{   /*  Note: Windows 95 only supports 5:6:5 and 5:5:5 in 16 bit images and 8:8:8 in 32 bit images      */
    uint32_t    redmask;    /* Red   - for 5:6:5 = 1111 1000 0000 0000 0000 0000 0000 0000 = 0xF8000000 */
    uint32_t    greenmask;  /* Green - for 5:6:5 = 0000 0111 1110 0000 0000 0000 0000 0000 = 0x07D00000 */
    uint32_t    bluemask;   /* Blue  - for 5:6:5 = 0000 0000 0001 1111 0000 0000 0000 0000 = 0x001F0000 */
}  bitmap3bitfieldsmask;

#pragma pack(pop) /* Reset packing to normal */

/* Bitmasks for 16 bit bitmaps */
// static bitmap3bitfieldsmask mask565 = { 0xF8000000, 0x07E00000, 0x001F0000 };
// static bitmap3bitfieldsmask mask555 = { 0x7C000000, 0x03E00000, 0x001F0000 };

/* Bitmasks for 32 bit bitmaps */
// static bitmap3bitfieldsmask mask101010 = { 0xFFC00000, 0x003FF000, 0x00000FFC };
// static bitmap3bitfieldsmask mask888    = { 0xFF000000, 0x00FF0000, 0x0000FF00 };

/* Function for padding (rounding up) */
static uint32_t pad(uint32_t notpadded, uint32_t bytes)
{
    uint32_t padded = notpadded;
    padded += (bytes - (padded % bytes)) % bytes;
    return padded;
}

/* Converts 3 8-bit channels to a 16 bit 5:6:5 bitmap pixel */
static uint16_t pixel24to565(uint8_t channel1, uint8_t channel2, uint8_t channel3)
{
    uint16_t newpixel = 0; /* Bits shifted and masked to fit in to 16 bits */
    newpixel |= (channel1 << 8) & 0xF800;  /* 1111 1000 0000 0000 - 0xF800 */
    newpixel |= (channel2 << 3) & 0x07E0;  /* 0000 0111 1110 0000 - 0x07E0 */
    newpixel |= (channel3 >> 3) & 0x001F;  /* 0000 0000 0001 1111 - 0x001F */
    return newpixel;
}

/* Converts 3 8-bit channels to a 16 bit 5:5:5 bitmap pixel */
static uint16_t pixel24to555(uint8_t channel1, uint8_t channel2, uint8_t channel3)
{
    uint16_t newpixel = 0; /* Bits shifted and masked to fit in to 16 bits */
    newpixel |= (channel1 << 7) & 0x7C00;  /* 0111 1100 0000 0000 - 0xF800 */
    newpixel |= (channel2 << 2) & 0x03E0;  /* 0000 0011 1110 0000 - 0x07C0 */
    newpixel |= (channel3 >> 3) & 0x001F;  /* 0000 0000 0001 1111 - 0x003E */
    return newpixel;
}

/* RGB 24 bit BMP version 3 write function, 8 bit/channel */
void write_bmp3_24(imagestruct * image, char * imagename)
{
    uint32_t paddedwidth = pad(image->width * 3, 4);
    uint32_t imagesize = paddedwidth * image->height;
    /* Open/create file */
    FILE * file = fopen(imagename,"wb");
    /* Create header and write it to file */
    bitmapfileheader header1 = { 0x4D42, (54 + imagesize), 0, 0, 54 };
    fwrite(&header1, 1, sizeof(bitmapfileheader), file);
    bitmap3header header2 = { 40, image->width, image->height, 1, 24, 0, imagesize, 2835, 2835, 0, 0 };
    fwrite(&header2, 1, sizeof(bitmap3header), file);
    /* Go down the rows and write pixels - with padding */
    if (image->flip_image)
    {
        int y;
        for (y = image->height - 1; y >= 0; --y)
        {   /* Write row by row with 4 byte padding */
            fwrite(&image->imagedata[y * image->width * 3], paddedwidth, sizeof(uint8_t), file);
        }
    }
    else
    {
        int y;
        for (y = 0; y < image->height; ++y)
        {   /* Write row by row with 4 byte padding */
            fwrite(&image->imagedata[y * image->width * 3], paddedwidth, sizeof(uint8_t), file);
        }
    }
    fclose(file);
}

/* RGB 24 bit BMP version 2 write function - smaller header */
void write_bmp2_24(imagestruct * image, char * imagename)
{
    uint32_t paddedwidth = pad(image->width * 3, 4);
    uint32_t imagesize = paddedwidth * image->height;
    /* Open/create file */
    FILE * file = fopen(imagename,"wb");
    /* Create header and write it to file */
    bitmapfileheader header1 = { 0x4D42, (54 + imagesize), 0, 0, 26 };
    fwrite(&header1, 1, sizeof(bitmapfileheader), file);
    bitmap2header header2 = { 12, image->width, image->height, 1, 24 };
    fwrite(&header2, 1, sizeof(bitmap2header), file);
    /* Go down the rows and write pixels - with padding */
    if (image->flip_image)
    {
        int y;
        for (y = image->height - 1; y >= 0; --y)
        {   /* Write row by row with 4 byte padding */
            fwrite(&image->imagedata[y * image->width * 3], paddedwidth, sizeof(uint8_t), file);
        }
    }
    else
    {
        int y;
        for (y = 0; y < image->height; ++y)
        {   /* Write row by row with 4 byte padding */
            fwrite(&image->imagedata[y * image->width * 3], paddedwidth, sizeof(uint8_t), file);
        }
    }
    fclose(file);
}

/* Incomplete 16 bit BMP function */
void write_bmp2_555(imagestruct * image, char * imagename)
{   /* Padded width for 16 bit bitmap in pixels(2 bytes each) */
    uint32_t paddedwidth = pad(image->width, 2);
    uint32_t imagesize = paddedwidth * image->height;
    /* Get memory for 16 bit bitmap copnversion */
    uint16_t * image16bit = (uint16_t *)malloc(imagesize * sizeof(uint16_t));
    /* Do the conversion */
    uint32_t y;
    uint32_t x, roworiginal, rownew, location;
    for (y = 0; y < image->height; ++y)
    {   /* Convert row by row to 16 bit */
        rownew = paddedwidth * y;
        roworiginal = image->width * 3 * y;
        for (x = 0; x < image->width; x++)
        {
            location = roworiginal + (x * 3);
            image16bit[rownew + x] = pixel24to555(image->imagedata[location + 2], image->imagedata[location + 1], image->imagedata[location]);
        }
    }
    /* Open/create file */
    FILE * file = fopen(imagename,"wb");
    /* Create header(look at imageheaders/bmpheader.h) and write it to file */
    bitmapfileheader header1 = { 0x4D42, (26 + imagesize), 0, 0, 26 };
    fwrite(&header1, 1, sizeof(bitmapfileheader), file);
    bitmap2header header2 = { 12, image->width, image->height, 1, 16 };
    fwrite(&header2, 1, sizeof(bitmap2header), file);
    /* Go down the rows and write pixels - with padding */
    if (image->flip_image)
    {
        int y;
        for (y = image->height - 1; y >= 0; --y)
        {   /* Write row by row with 4 byte padding */
            fwrite(&image->imagedata[y * paddedwidth], paddedwidth, sizeof(uint16_t), file);
        }
    }
    else
    {
        int y;
        for (y = 0; y < image->height; ++y)
        {   /* Write row by row with 4 byte padding */
            fwrite(&image->imagedata[y * paddedwidth], paddedwidth, sizeof(uint16_t), file);
        }
    }
    fclose(file);
}

/* Incomplete 16 bit BMP function */
void write_bmp3_565(imagestruct * image, char * imagename)
{   /* Padded width for 16 bit bitmap in pixels(2 bytes each) */
    uint32_t paddedwidth = pad(image->width, 2);
    uint32_t imagesize = paddedwidth * image->height;
    /* Get memory for 16 bit bitmap copnversion */
    uint16_t * image16bit = (uint16_t *)malloc(imagesize * sizeof(uint16_t));
    /* Do the conversion */
    uint32_t y;
    uint32_t x, roworiginal, rownew, location;
    for (y = 0; y < image->height; ++y)
    {   /* Convert row by row to 16 bit */
        rownew = paddedwidth * y;
        roworiginal = image->width * 3 * y;
        for (x = 0; x < image->width; x++)
        {
            location = roworiginal + (x * 3);
            image16bit[rownew + x] = pixel24to565(image->imagedata[location + 2], image->imagedata[location + 1], image->imagedata[location]);
        }
    }
    /* Open/create file */
    FILE * file = fopen(imagename,"wb");
    /* Create header(look at imageheaders/bmpheader.h) and write it to file */
    bitmapfileheader header1 = { 0x4D42, (54 + imagesize), 0, 0, 66 };
    fwrite(&header1, 1, sizeof(bitmapfileheader), file);
    bitmap3header header2 = { 40, image->width, image->height, 1, 16, 3, imagesize * 2, 2835, 2835, 0, 0 };
    fwrite(&header2, 1, sizeof(bitmap3header), file);
    /* Create bitfield mask(see in imageheaders/bmpheader.h) and write it to file */
    bitmap3bitfieldsmask bitheader = { 0x0000F800, 0x000007E0, 0x0000001F };
    fwrite(&bitheader, 1, sizeof(bitmap3bitfieldsmask), file);
    /* Go down the rows and write pixels - with padding */
    if (image->flip_image)
    {
        int y;
        for (y = image->height - 1; y >= 0; --y)
        {   /* Write row by row with 4 byte padding */
            fwrite(&image->imagedata[y * paddedwidth], paddedwidth, sizeof(uint16_t), file);
        }
    }
    else
    {
        int y;
        for (y = 0; y < image->height; ++y)
        {   /* Write row by row with 4 byte padding */
            fwrite(&image->imagedata[y * paddedwidth], paddedwidth, sizeof(uint16_t), file);
        }
    }
    fclose(file);
}

imagestruct bmpread(char * imagename)
{   /* Open file */
    FILE * file = fopen(imagename,"rb");
    uint32_t imagewidth, imageheight, offset;
    /* Read where image data will start(probably 54) */
    fseek(file, 10, SEEK_SET);
    fread(&offset, sizeof(uint32_t), 1, file);
    /* Read image height and width */
    fseek(file, 18, SEEK_SET);
    fread(&imagewidth, sizeof(uint32_t), 1, file);
    fread(&imageheight, sizeof(uint32_t), 1, file);
    uint32_t paddedwidth = pad(imagewidth * 3, 4); /* Calculate padded width in bytes */
    uint32_t imagesize = imageheight * imagewidth * 3; /* Total size of image bytes without padding */
    /* Memory to store image */
    uint8_t * imagedata = (uint8_t *)malloc(imagesize * sizeof(uint8_t));
    fseek(file, offset, SEEK_SET); /* Move to start of image */
    uint32_t v;
    for (v = 0; v < imageheight; v++)
    {
        fseek(file, ( offset + v * paddedwidth ), SEEK_SET); /* Move to current row */
        fread(&imagedata[v * imagewidth * 3], sizeof(uint8_t), (imagewidth * 3), file); /* Read row */
    }
    fclose(file); /* Close file */
    /* Create and return image structure */
    imagestruct image = { imagewidth, imageheight, imagedata };
    return image;
}
