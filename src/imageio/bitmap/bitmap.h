/* Bitmap file header structures
 * Made for BMP Version 2 and 3 */
#ifndef _bmpheader_
#define _bmpheader_

/* Bitmap version 2/3 24 bit, version 2 has smaller header */
void write_bmp3_24(imagestruct * image, char * imagename);
void write_bmp2_24(imagestruct * image, char * imagename);

void write_bmp2_555(imagestruct * image, char * imagename);
void write_bmp3_565(imagestruct * image, char * imagename);

/* Bitmap reading function */
imagestruct bmpread(char * imagename);

#endif
