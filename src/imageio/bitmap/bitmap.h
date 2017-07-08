/* Bitmap file header structures
 * Made for BMP Version 2 and 3 */
#ifndef _bmpheader_
#define _bmpheader_

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

/* Bitmap version 2/3 24 bit, version 2 has smaller header */
void write_bmp3_24(imagestruct image, char * imagename);
void write_bmp2_24(imagestruct image, char * imagename);

void write_bmp2_555(imagestruct image, char * imagename);
void write_bmp3_565(imagestruct image, char * imagename);

/* Bitmap reading function */
imagestruct bmpread(char * imagename);

#endif
