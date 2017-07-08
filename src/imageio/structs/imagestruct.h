#ifndef _imagestruct_
#define _imagestruct_

/* Image structure(for handling images inside program) */
typedef struct
{
    uint16_t    width;      /* Width of image                   */
    uint16_t    height;     /* Height of image                  */
    uint8_t  *  imagedata;  /* Image data - 8 bit only          */

    uint8_t     alpha;      /* Transparency? 1 = yes, 0 = no    */
    uint8_t  *  alphadata;  /* Alpha data, possibly unused      */
}  imagestruct;

#endif