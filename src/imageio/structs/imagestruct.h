#ifndef _imagestruct_
#define _imagestruct_

/* Image structure(for handling images inside program) */
typedef struct
{
    uint16_t    width;      /* Width of image                   */
    uint16_t    height;     /* Height of image                  */
    uint8_t  *  imagedata;  /* Image data - 8 bit only          */

    int flip_image;         /* Cos BMP is like that             */

}  imagestruct;

#endif