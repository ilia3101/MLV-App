#ifndef _image_profile_h_
#define _image_profile_h_

/* Image profile structure */
typedef struct image_profile_t
{
    /* Allow creative controls or not */
    int allow_creative_adjustments;

    /* Tonemapping function pointer, used for tonemapping and log curves too */
    double (* tone_mapping_function)(double);

    /* Gamma for inside of processing, not output. Just for a
     * lighter nicer looking image, not anything scientific. */
    double gamma_power;
    /* Processing internal gamut */
    int processing_gamut;

    /* Output colour */

    /* Processing output gamut */
    int output_gamut;
    /* Processing output gamma */
    int output_gamma;

    /* Some links I added 2 years ago for some reason */
    /* https://ninedegreesbelow.com/photography/xyz-rgb.html
     * http://www.ryanjuckett.com/programming/rgb-color-space-conversion/ */

} image_profile_t;

#endif