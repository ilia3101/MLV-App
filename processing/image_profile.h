#ifndef _image_profile_h_
#define _image_profile_h_

/* Image profile structure */
typedef struct image_profile_t {
    /* Certain processing settings can be disabled for accuracy (1=on, 0=off) */
    struct disable_settings {
        int saturation; /* Saturation */
        int curves; /* All contrast + 'lighten' setting */
        int tonemapping; /* Tonemapping, via specified function */
    } disable_settings;
    /* Tonemapping function pointer, required if tonemapping is not disabled
     * this function can be any kind of transform for 0.0-1.0 double values */
    double (* tone_mapping_function)(double);
    double gamma_power; /* 1.0=linear/do nothing, 2.2~=sRGB, 2.0=rec.709 */
    /* xy chromaticities for output colour space */
    struct xy_chromaticity {
        struct { int x, y; } red;
        struct { int x, y; } green;
        struct { int x, y; } blue;
        struct { int x, y; } white;
    } xy_chromaticity;
    /* https://ninedegreesbelow.com/photography/xyz-rgb.html
     * http://www.ryanjuckett.com/programming/rgb-color-space-conversion/ */
}  image_profile_t;

#endif