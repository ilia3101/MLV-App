#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "raw_processing.h"
#include "../mlv/video_mlv.h"

/* Matrix functions which are useful */
#include "../matrix/matrix.h"

#define STANDARD_GAMMA 3.15

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define LIMIT16(X) MAX(MIN(X, 65535), 0)

/* Because why compile a whole .o just for this? */
#include "processing.c"

/* Initialises processing thing with memory */
processingObject_t * initProcessingObject()
{
    processingObject_t * processing = calloc( 1, sizeof(processingObject_t) );

    processing->pre_calc_curve_r = malloc( 65536 * sizeof(uint16_t) );
    processing->pre_calc_curve_g = malloc( 65536 * sizeof(uint16_t) );
    processing->pre_calc_curve_b = malloc( 65536 * sizeof(uint16_t) );
    processing->pre_calc_gamma   = malloc( 65536 * sizeof(uint16_t) );
    processing->pre_calc_levels  = malloc( 65536 * sizeof(uint16_t) );
    processing->pre_calc_o_curve = malloc( 65535 * sizeof(uint16_t) );
    processing->pre_calc_sat     = malloc( 131072 * sizeof(int32_t) );

    /* For precalculated matrix values */
    for (int i = 0; i < 9; ++i)
        processing->pre_calc_matrix[i] = malloc( 65536 * sizeof(int32_t) );

    /* A nothing matrix */
    processing->xyz_to_cam_matrix[0] = 1.0;
    processing->xyz_to_cam_matrix[4] = 1.0;
    processing->xyz_to_cam_matrix[8] = 1.0;
    /* Different matrix BTW */
    processing->xyz_to_rgb_matrix[0] = 1.0;
    processing->xyz_to_rgb_matrix[4] = 1.0;
    processing->xyz_to_rgb_matrix[8] = 1.0;

    /* Default settings */
    processingSetWhiteBalance(processing, 6250.0, 0.0);
    processingSetBlackAndWhiteLevel(processing, 8192.0, 64000.0); /* 16 bit! */
    processingSetExposureStops(processing, 0.0);
    processingSetGamma(processing, STANDARD_GAMMA);
    processingSetSaturation(processing, 1.0);
    processingSetContrast(processing, 0.73, 5.175, 0.5, 0.0, 0.0);
    processingSetImageProfile(processing, PROFILE_TONEMAPPED);

    /* Just in case (should be done tho already) */
    processing_update_matrices(processing);
    
    return processing;
}

void processingSetImageProfile(processingObject_t * processing, int imageProfile)
{
    processing->image_profile = imageProfile;

    if (imageProfile == PROFILE_STANDARD)
    {
        /* output curve is not used in this case */
        processing->use_o_curve = 0;
        processing->use_rgb_curves = 1;
        processing->use_saturation = 1;
        processingSetGamma(processing, STANDARD_GAMMA);
        processing_disable_tonemapping(processing);
    }
    else if (imageProfile == PROFILE_TONEMAPPED)
    {
        processing->use_o_curve = 0;
        processing->use_rgb_curves = 1;
        processing->use_saturation = 1;
        processingSetGamma(processing, STANDARD_GAMMA);
        processing_enable_tonemapping(processing);
    }
    /* Canon-Log info from: http://learn.usa.canon.com/app/pdfs/white_papers/White_Paper_Clog_optoelectronic.pdf */
    // else if (imageProfile == PROFILE_CANON_LOG) /* Can't seem to get this right */
    // {
    //     processing->use_o_curve = 1;
    //     processing->use_rgb_curves = 0;
    //     processing->use_saturation = 0;

    //     /* Calculate Canon-Log curve */
    //     for (int i = 0; i < 65536; ++i)
    //     {
    //         double value = (double)i / 65535.0;
    //         value = 0.529136 * log10(value * 10.1596) + 0.0730597;
    //         value *= 65535.0;
    //         processing->pre_calc_o_curve[i] = (uint16_t)LIMIT16(value);
    //     }

    //     processingSetGamma(processing, 1.0);
    //     processing_disable_tonemapping(processing);
    // }
    /* Alexa Log info from: http://www.vocas.nl/webfm_send/964 */
    else if (imageProfile == PROFILE_ALEXA_LOG)
    {
        processing->use_o_curve = 1;
        processing->use_rgb_curves = 0;
        processing->use_saturation = 0;

        /* Calculate Alexa Log curve (iso 800 version) */
        for (int i = 0; i < 65536; ++i)
        {
            double value = (double)i / 65535.0;
            value = (value > 0.010591) ? (0.247190 * log10(5.555556 * value + 0.052272) + 0.385537) : (5.367655 * value + 0.092809);
            value *= 65535.0;
            processing->pre_calc_o_curve[i] = (uint16_t)value;
        }

        /* We won't even need gamma here */
        processingSetGamma(processing, 1.0);
        processing_disable_tonemapping(processing);
    }
    /* More Log info from: http://www.magiclantern.fm/forum/index.php?topic=15801.msg158145#msg158145 */
    else if (imageProfile == PROFILE_CINEON_LOG)
    {
        processing->use_o_curve = 1;
        processing->use_rgb_curves = 0;
        processing->use_saturation = 0;

        /* Calculate Cineon curve */
        for (int i = 0; i < 65536; ++i)
        {
            double value = (double)i / 65535.0;
            value = (((log10(value * (1.0 - 0.0108) + 0.0108)) * 300) + 685) / 1023;
            value *= 65535.0;
            processing->pre_calc_o_curve[i] = (uint16_t)value;
        }

        processingSetGamma(processing, 1.0);
        processing_disable_tonemapping(processing);
    }
    /* Sony Log info from: https://pro.sony.com/bbsccms/assets/files/mkt/cinema/solutions/slog_manual.pdf */
    else if (imageProfile == PROFILE_SONY_LOG)
    {
        processing->use_o_curve = 1;
        processing->use_rgb_curves = 0;
        processing->use_saturation = 0;

        /* Calculate S-Log curve */
        for (int i = 0; i < 65536; ++i)
        {
            double value = (double)i / 65535.0;
            value = (0.432699 * log10((value * 10.0) + 0.037584) + 0.616596) + 0.03;
            value *= 65535.0;
            processing->pre_calc_o_curve[i] = (uint16_t)LIMIT16(value);
        }

        processingSetGamma(processing, 1.0);
        processing_disable_tonemapping(processing);
    }
    else if (imageProfile == PROFILE_LINEAR)
    {
        processing->use_o_curve = 0;
        processing->use_rgb_curves = 1;
        processing->use_saturation = 1;
        processingSetGamma(processing, 1.0);
        processing_disable_tonemapping(processing);
    }
    else return;
}


/* Takes those matrices I learned about on the forum */
void processingSetXyzToCamMatrix(processingObject_t * processing, double * xyzToCamMatrix)
{
    memcpy(processing->xyz_to_cam_matrix, xyzToCamMatrix, sizeof(double) * 9);
    /* Calculates final main matrix */
    processing_update_matrices(processing);
}

/* Fun :DDDDDDD */
void processingSetXyzToRgbMatrix(processingObject_t * processing, double * xyzToCamMatrix)
{
    memcpy(processing->xyz_to_rgb_matrix, xyzToCamMatrix, sizeof(double) * 9);
    /* Calculates final main matrix */
    processing_update_matrices(processing);
}


/* Process a RAW frame with settings from a processing object
 * - image must be debayered and RGB plz + thx! */
void applyProcessingObject( processingObject_t * processing, 
                            int imageX, int imageY, 
                            uint16_t * inputImage, 
                            uint16_t * outputImage )
{
    /* Begin image processing... */

    /* (for shorter code) */
    int32_t ** pm = processing->pre_calc_matrix; /* 'pm' STANDS FOR PRECALCULATED MATRIX */
    uint16_t * img = outputImage;

    /* Number of elements */
    int img_s = imageX * imageY * 3;

    /* Point to the end */
    uint16_t * img_end = img + img_s;

    memcpy(outputImage, inputImage, img_s * sizeof(uint16_t));

    /* Apply some precalcuolated settings */
    for (int i = 0; i < img_s; ++i)
    {
        /* Black + white level */
        outputImage[i] = processing->pre_calc_levels[ outputImage[i] ];
    }

    /* NOW MATRIX! (white balance & exposure) */
    for (uint16_t * pix = img; pix < img_end; pix += 3)
    {
        int32_t pix0 = pm[0][pix[0]] + pm[1][pix[1]] + pm[2][pix[2]];
        int32_t pix1 = pm[3][pix[0]] + pm[4][pix[1]] + pm[5][pix[2]];
        int32_t pix2 = pm[6][pix[0]] + pm[7][pix[1]] + pm[8][pix[2]];

        pix[0] = LIMIT16(pix0);
        pix[1] = LIMIT16(pix1);
        pix[2] = LIMIT16(pix2);
    }

    /* Gamma */
    for (int i = 0; i < img_s; ++i)
    {
        outputImage[i] = processing->pre_calc_gamma[ outputImage[i] ];
    }

    /* Now highlilght reconstruction */
    if (processing->exposure_stops < 0.0 && processing->highest_green < 65535 && processing->highlight_reconstruction)
    {
        for (uint16_t * pix = img; pix < img_end; pix += 3)
        {
            /* Check if its the highest green value possible */
            if (pix[1] == processing->highest_green)
            {
                pix[1] = (pix[0] + pix[2]) / 2;
            }
        }
    }

    if (processing->use_saturation)
    {
        /* Now saturation (looks way better after gamma) */
        for (uint16_t * pix = outputImage; pix < img_end; pix += 3)
        {
            /* Pixel brightness = 4/16 R, 11/16 G, 1/16 blue; Try swapping the channels, it will look worse */
            int32_t Y1 = ((pix[0] << 2) + (pix[1] * 11) + pix[2]) >> 4;
            int32_t Y2 = Y1 - 65536;

            /* Increase difference between channels and the saturation midpoint */
            int32_t pix0 = processing->pre_calc_sat[pix[0] - Y2] + Y1;
            int32_t pix1 = processing->pre_calc_sat[pix[1] - Y2] + Y1;
            int32_t pix2 = processing->pre_calc_sat[pix[2] - Y2] + Y1;

            pix[0] = LIMIT16(pix0);
            pix[1] = LIMIT16(pix1);
            pix[2] = LIMIT16(pix2);
        }
    }

    if (processing->use_rgb_curves)
    {
        /* Contrast Curve (OMG putting this after gamma made it 999x better) */
        for (uint16_t * pix = outputImage; pix < img_end; pix += 3)
        {
            pix[0] = processing->pre_calc_curve_r[ pix[0] ];
            pix[1] = processing->pre_calc_curve_r[ pix[1] ];
            pix[2] = processing->pre_calc_curve_r[ pix[2] ];
        }
    }

    /* Ouput curve (if needed) */
    if (processing->use_o_curve)
    {
        for (int i = 0; i < img_s; ++i)
        {
            outputImage[i] = processing->pre_calc_o_curve[ outputImage[i] ];
        }
    }
}

/* Set contrast (S-curve really) */
void processingSetContrast( processingObject_t * processing, 
                            double DCRange,  /* Dark contrast range: 0.0 to 1.0 */
                            double DCFactor, /* Dark contrast strength: 0.0 to 8.0(any range really) */
                            double LCRange,  /* Light contrast range */
                            double LCFactor, /* Light contrast strength */
                            double lighten   /* 0-1 (for good highlight rolloff) */ )
{
    /* Basic things */
    processing->light_contrast_factor = LCFactor;
    processing->light_contrast_range = LCRange;
    processing->dark_contrast_factor = DCFactor; 
    processing->dark_contrast_range = DCRange;
    processing->lighten = lighten;

    processing_update_curves(processing);
}

void processingSetDCRange(processingObject_t * processing, double DCRange)
{
    processing->dark_contrast_range = DCRange;
    processing_update_curves(processing);
}
void processingSetDCFactor(processingObject_t * processing, double DCFactor)
{
    processing->dark_contrast_factor = DCFactor;
    processing_update_curves(processing);
}
void processingSetLCRange(processingObject_t * processing, double LCRange) 
{
    processing->light_contrast_range = LCRange;
    processing_update_curves(processing);
}
void processingSetLCFactor(processingObject_t * processing, double LCFactor)
{
    processing->light_contrast_factor = LCFactor;
    processing_update_curves(processing);
}
void processingSetLightening(processingObject_t * processing, double lighten)
{
    processing->lighten = lighten;
    processing_update_curves(processing);
}

/* Have a guess what this does */
void processingSetExposureStops(processingObject_t * processing, double exposureStops)
{
    processing->exposure_stops = exposureStops;

    processingSetGamma(processing, processing->gamma_power);
    processing_update_matrices(processing);
}


/* Sets and precalculaes saturation */
void processingSetSaturation(processingObject_t * processing, double saturationFactor)
{
    processing->saturation = saturationFactor;

    /* Precaluclate for the algorithm */
    for (int i = 0; i < 131072; ++i)
    {
        double value = (i - 65536) * saturationFactor;
        processing->pre_calc_sat[i] = value;
    }
}


/* Set white balance by kelvin + tint value */
void processingSetWhiteBalance(processingObject_t * processing, double WBKelvin, double WBTint)
{
    processing->kelvin = WBKelvin;
    processing->wb_tint = WBTint;
    
    /* Kalkulate channel (yes in cone space... soon) multipliers */
    get_kelvin_multipliers_rgb(WBKelvin, processing->wb_multipliers);

    /* Do tint (green and red channel seem to be main ones) */
    processing->wb_multipliers[2] += (WBTint / 11.0);
    processing->wb_multipliers[0] += (WBTint / 19.0);

    /* Make all channel multipliers be >= 1 */
    double lowest = MIN( MIN( processing->wb_multipliers[0], 
                              processing->wb_multipliers[1] ), 
                              processing->wb_multipliers[2] );

    for (int i = 0; i < 3; ++i) processing->wb_multipliers[i] /= lowest;

    /* White balance is part of the matrix */
    processing_update_matrices(processing);
}

/* WB just by kelvin */
void processingSetWhiteBalanceKelvin(processingObject_t * processing, double WBKelvin)
{
    processingSetWhiteBalance( processing, WBKelvin,
                               processingGetWhiteBalanceTint(processing) );
}

/* WB tint */
void processingSetWhiteBalanceTint(processingObject_t * processing, double WBTint)
{
    processingSetWhiteBalance( processing, processingGetWhiteBalanceKelvin(processing),
                               WBTint );
}

/* Tonemapping info from http://filmicworlds.com/blog/filmic-tonemapping-operators/ */

/* Values for uncharted tonemapping... they can be adjusted */
double u_A = 0.15;
double u_B = 0.50;
double u_C = 0.10;
double u_D = 0.20;
double u_E = 0.02;
double u_F = 0.30;
double u_W = 11.2; /* White point */
double u_bias = 2.0;

/* Uncharted tonemapping base funtion */
static double uncharted_tonemap(double value)
{
    return (((value*(u_A*value+u_C*u_B)+u_D*u_E) / (value*(u_A*value+u_B)+u_D*u_F)) - (u_E/u_F));
}

/* Wrapper with white scaling */
static double UnchartedTonemap(double value)
{
    value = uncharted_tonemap(u_bias * value);
    /* White scale */
    value *= (1.0 / uncharted_tonemap(u_W));
    return value;
}

static double ReinhardTonemap(double value)
{
    return value / (1.0 + value);
}

/* Choose tonemapping function */
#define TONEMAP(X) ReinhardTonemap(X)

/* Set gamma */
void processingSetGamma(processingObject_t * processing, double gammaValue)
{
    processing->gamma_power = gammaValue;

    /* Needs to be inverse */
    double gamma = 1.0 / gammaValue;

    if (processing->exposure_stops < 0.0 || !processing->tone_mapping)
    {
        /* Precalculate the exposure curve */
        for (int i = 0; i < 65536; ++i)
        {
            /* Tone mapping also (reinhard) */
            double pixel = (double)i/65535.0;
            if (processing->tone_mapping) pixel = TONEMAP(pixel);
            processing->pre_calc_gamma[i] = (uint16_t)(65535.0 * pow(pixel, gamma));
        }
    }
    /* Else exposure done here if it is positive */
    else
    {
        /* Exposure */
        double exposure_factor = pow(2.0, processing->exposure_stops);
        /* Precalculate the curve */
        for (int i = 0; i < 65536; ++i)
        {
            /* Tone mapping also (reinhard) */
            double pixel = (double)i/65535.0;
            pixel *= exposure_factor;
            pixel = TONEMAP(pixel);
            pixel = 65535.0 * pow(pixel, gamma);
            pixel = LIMIT16(pixel);
            processing->pre_calc_gamma[i] = pixel;
        }
    }
}

/* Range of saturation and hue is 0.0-1.0 */
void processingSet3WayCorrection( processingObject_t * processing,
                                  double highlightHue, double highlightSaturation,
                                  double midtoneHue, double midtoneSaturation,
                                  double shadowHue, double shadowSaturation )
{
    processing->highlight_hue = highlightHue;
    processing->highlight_sat = highlightSaturation;
    processing->midtone_hue = midtoneHue;
    processing->midtone_sat = midtoneSaturation;
    processing->shadow_hue = shadowHue;
    processing->shadow_sat = shadowSaturation;

    processing_update_curves(processing);
}

void processing_enable_tonemapping(processingObject_t * processing)
{
    (processing)->tone_mapping = 1;
    /* This will update everything necessary to enable tonemapping */
    processingSetGamma(processing, processing->gamma_power);
    processing_update_matrices(processing);
}

void processing_disable_tonemapping(processingObject_t * processing) 
{
    (processing)->tone_mapping = 0;
    /* This will update everything necessary to disable tonemapping */
    processingSetGamma(processing, processing->gamma_power);
    processing_update_matrices(processing);
}

/* Set black and white level */
void processingSetBlackAndWhiteLevel( processingObject_t * processing, 
                                      int blackLevel, int whiteLevel )
{
    processing->black_level = blackLevel;
    processing->white_level = whiteLevel;

    /* How much it needs to be stretched */
    double stretch = 65535.0 / (whiteLevel - blackLevel);

    for (int i = 0; i < 65536; ++i)
    {
        /* Stretch to the black-white level range */
        int new_value = (int)((double)(i - blackLevel) * stretch);

        if (new_value < 65536 && new_value > 0)
        {
            processing->pre_calc_levels[i] = new_value;
        }
        else if (new_value < 0)
        {
            processing->pre_calc_levels[i] = 0;
        }
        else if (new_value > 65535)
        {
            processing->pre_calc_levels[i] = 65535;
        }
    }
}

/* Cheat functions */
void processingSetBlackLevel(processingObject_t * processing, int blackLevel)
{
    processingSetBlackAndWhiteLevel( processing,
                                     blackLevel,
                                     processingGetWhiteLevel(processing) );
}
void processingSetWhiteLevel(processingObject_t * processing, int whiteLevel)
{
    processingSetBlackAndWhiteLevel( processing,
                                     processingGetBlackLevel(processing),
                                     whiteLevel );
}

/* Decomissions a processing object completely(I hope) */
void freeProcessingObject(processingObject_t * processing)
{
    free(processing->pre_calc_curve_r);
    free(processing->pre_calc_curve_g);
    free(processing->pre_calc_curve_b);
    free(processing->pre_calc_gamma);
    free(processing->pre_calc_levels);
    free(processing->pre_calc_o_curve);
    free(processing->pre_calc_sat);
    for (int i = 0; i < 9; ++i) free(processing->pre_calc_matrix[i]);
    free(processing);
}
