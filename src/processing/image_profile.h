#ifndef _image_profile_h_
#define _image_profile_h_

/* Image profile structure */
typedef struct image_profile_t {
    double gamma_power; /* 1.0=linear/do nothing, 2.2~=sRGB, 2.0=rec.709 */
    uint8_t tonemap_function; /* Tonemap function macro */
    uint8_t allow_creative_adjustments; /* Disable saturation contrast curves etc */
    uint8_t colour_gamut; /* What colour gamut (macros in raw_processing.h) */
}  image_profile_t;

#endif