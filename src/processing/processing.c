/* Some functions that are used within raw_processing.c
 * This file is directly #included in raw_processing.c */

/* included so idiotic visual studio code shuts up */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include "processing_object.h"
#include "bmd_film.h"

//Interpolation functions
double interpol(double x, double x1, double x2, double q00, double q01) {
    if( ( x2 - x1 ) == 0 ) return q00;
    else return ( ( ( x2 - x ) / ( x2 - x1 ) ) * q00 ) + ( ( ( x - x1 ) / ( x2 - x1 ) ) * q01 );
}

/* Measurements taken from 5D Mark II RAW photos using EXIFtool, surely Canon can't be wrong about WB mutipliers? */
static const int wb_kelvin[]   = {  2000,  2500,  3000,  3506,  4000,  4503,  5011,  5517,  6018,  6509,  7040,  7528,  8056,  8534,  9032,  9531, 10000 };
static const double wb_red[]   = { 1.134, 1.349, 1.596, 1.731, 1.806, 1.954, 2.081, 2.197, 2.291, 2.365, 2.444, 2.485, 2.528, 2.566, 2.612, 2.660, 2.702 };
static const double wb_green[] = { 1.155, 1.137, 1.112, 1.056, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000 };
static const double wb_blue[]  = { 4.587, 3.985, 3.184, 2.524, 2.103, 1.903, 1.760, 1.641, 1.542, 1.476, 1.414, 1.390, 1.363, 1.333, 1.296, 1.263, 1.229 };

static const double id_matrix[] = {1,0,0,0,1,0,0,0,1};

static const double xyz_to_rgb[] = {
     3.2404542, -1.5371385, -0.4985314,
    -0.9692660,  1.8760108,  0.0415560,
     0.0556434, -0.2040259,  1.0572252,
};

/* XYZ to Cone space (LMS) */
static const double ciecam02[] = {
    0.7328,  0.4296, -0.1624,
   -0.7036,  1.6975,  0.0061,
    0.0030,  0.0136,  0.9834
};

/* Danne temp fix version */
static const double xyz_to_rgb_danne[] = {
     3.2404542, -1.5371385, -0.4985314,
    -0.9692660,  1.8760108,  0.0415560,
     0.0030,  -0.0536,  0.9834
};

/* Danne temp fix XYZ to Cone space (LMS) */
static const double ciecam02_danne[] = {
    3.2404542, -1.5371385, -0.4985314,
   -0.7036,  1.8760108,  0.0415560,
    0.0030,  0.0536,  0.9834
};


/* Matrices XYZ -> RGB */
static double colour_gamuts[][10] = {
    { /* GAMUT_Rec709 */
         3.2404542, -1.5371385, -0.4985314,
        -0.9692660,  1.8760108,  0.0415560,
         0.0556434, -0.2040259,  1.0572252
    },
    { /* GAMUT_Rec2020 */
         1.72466, -0.36222, -0.25442,
        -0.66941,  1.62275,  0.01240,
         0.01826, -0.04444,  0.94329
    },
    { /* GAMUT_ACES_AP0 */
        1.0498110175, 0.0000000000, -0.0000974845,
        -0.4959030231, 1.3733130458, 0.0982400361,
        0.0000000000, 0.0000000000, 0.9912520182
    },
    { /* GAMUT_AdobeRGB */
         2.0413690, -0.5649464, -0.3446944,
        -0.9692660,  1.8760108,  0.0415560,
         0.0134474, -0.1183897,  1.0154096
    },
    { /* GAMUT_ProPhotoRGB */
         1.3459433, -0.2556075, -0.0511118,
        -0.5445989,  1.5081673,  0.0205351,
         0.0000000,  0.0000000,  1.2118128
    },
    { /* GAMUT_XYZ */
         1, 0, 0, 0, 1, 0, 0, 0, 1
    },
    { /* GAMUT_AlexaWideGamutRGB */
         1.789066, -0.482534, -0.200076,
        -0.639849,  1.396400,  0.194432,
        -0.041532,  0.082335,  0.878868
    },
    { /* GAMUT_SonySGamut3 */
         1.8467789693, -0.5259861230, -0.2105452114,
        -0.4441532629,  1.2594429028,  0.1493999729,
         0.0408554212,  0.0156408893,  0.8682072487
    },
    { /* GAMUT_BmdFilm */
         1.693614, -0.459157, -0.138632,
        -0.489970,  1.344410,  0.111740,
        -0.074796,  0.385269,  0.629528
    },
    { /* GAMUT_ACES_AP1 */
        1.6410233797, -0.3248032942, -0.2364246952,
        -0.6636628587, 1.6153315917, 0.0167563477,
        0.0117218943, -0.0082844420, 0.9883948585
    }
};

// BMDFILM: http://www.bmcuser.com/archive/index.php/t-15819.html

/* Tonemapping info from http://filmicworlds.com/blog/filmic-tonemapping-operators/ */

/* NO TRANSFER FUNCTION, EASIER TO CODE */
double NoTonemap(double x) { return x; }
float NoTonemap_f(float x) { return x; }

/* Reinhard - most basic but just werks */
double ReinhardTonemap(double x) { return x / (1.0 + x); }
float ReinhardTonemap_f(float x) { return x / (1.0f + x); }

/* Reinhard, but onnly applied to top 3/5 of the range, less darkening overall */
double Reinhard_3_5_Tonemap(double x) { return (x < 0.4) ? x : (ReinhardTonemap((x-0.4)/0.6)*0.6+0.4); }
float Reinhard_3_5_Tonemap_f(float x) { return (x < 0.4f) ? x : (ReinhardTonemap_f((x-0.4f)/0.6f)*0.6f+0.4f); }

/* Interesting inverse tangent curve */
double TangentTonemap(double x) { return atan(x) / atan(8.0); }
float TangentTonemap_f(float x) { return atanf(x) / atanf(8.0f); }

/* Canon C-Log: http://learn.usa.canon.com/app/pdfs/white_papers/White_Paper_Clog_optoelectronic.pdf (not working right) */
double CanonCLogTonemap(double x) { return (0.529136 * log10(x * 1015.96 + 1.0) + 0.0730597); }
float CanonCLogTonemap_f(float x) { return (0.529136f * log10f(x * 1015.96f + 1.0f) + 0.0730597f); }

/* Calculate Alexa Log curve (iso 800 version), from here: http://www.vocas.nl/webfm_send/964 */
double AlexaLogCTonemap(double x) { return (x > 0.010591) ? (0.247190 * log10(5.555556 * x + 0.052272) + 0.385537) : (5.367655 * x + 0.092809); }
float AlexaLogCTonemap_f(float x) { return (x > 0.010591f) ? (0.247190f * log10f(5.555556f * x + 0.052272f) + 0.385537f) : (5.367655f * x + 0.092809f); }

/* Cineon Log, formula from here: http://www.magiclantern.fm/forum/index.php?topic=15801.msg158145#msg158145 */
double CineonLogTonemap(double x) { return ((log10(x * (1.0 - 0.0108) + 0.0108)) * 300.0 + 685.0) / 1023.0; }
float CineonLogTonemap_f(float x) { return ((log10f(x * (1.0f - 0.0108f) + 0.0108f)) * 300.0f + 685.0f) / 1023.0f; }

/* Sony S-Log3, from here: https://www.sony.de/pro/support/attachment/1237494271390/1237494271406/technical-summary-for-s-gamut3-cine-s-log3-and-s-gamut3-s-log3.pdf */
double SonySLogTonemap(double x) { return (x >= 0.01125000) ? (420.0 + log10((x + 0.01) / (0.18 + 0.01)) * 261.5) / 1023.0 : (x * (171.2102946929 - 95.0) / 0.01125000 + 95.0) / 1023.0; }
float SonySLogTonemap_f(float x) { return (x >= 0.01125000f) ? (420.0f + log10f((x + 0.01f) / (0.18f + 0.01f)) * 261.5f) / 1023.0f : (x * (171.2102946929f - 95.0f) / 0.01125000f + 95.0f) / 1023.0f; }

/* sRGB */
double sRGBTransferFunction(double x) { return x < 0.0031308 ? x * 12.92 : (1.055 * pow(x, 1.0 / 2.4)) -0.055; }
float sRGBTransferFunction_f(float x) { return x < 0.0031308f ? x * 12.92f : (1.055f * powf(x, 1.0f / 2.4f)) -0.055f; }

/* rec709 */
double Rec709TransferFunction(double x) { return x <= 0.018 ? (x * 4.5) : 1.099 * pow( x, (0.45) ) - 0.099; }
float Rec709TransferFunction_f(float x) { return x <= 0.018f ? (x * 4.5f) : 1.099f * powf( x, (0.45f) ) - 0.099f; }

/* HLG (Hybrid Log Gamma) */
double HLG_TransferFunction(double E) { return (E <= 1.0) ? (sqrt(E) * 0.5) : 0.17883277 * log(E - 0.28466892) + 0.55991073; }
float HLG_TransferFunction_f(float E) { return (E <= 1.0f) ? (sqrtf(E) * 0.5f) : 0.17883277f * logf(E - 0.28466892f) + 0.55991073f; }

/* BMDFilm via LUT */
double BmdFilmTonemap(double x)
{
    double input = ( x * ( 4095.0 ) / pow(2.0, 1.2) / 5.7661304310 );
    if( input >= 4095 ) input = 4095;
    uint16_t in = (uint16_t) input;

    double pix00 = bmd_film[in  ][0];
    double pix01 = bmd_film[in+1][0];

    return interpol( input, in, in+1, pix00, pix01 );
}

static void * tonemap_functions[] =
{
    (void *)&NoTonemap,
    (void *)&ReinhardTonemap,
    (void *)&TangentTonemap,
    //(void *)&CanonCLogTonemap,
    (void *)&AlexaLogCTonemap,
    (void *)&CineonLogTonemap,
    (void *)&SonySLogTonemap,
    (void *)&sRGBTransferFunction,
    (void *)&Rec709TransferFunction,
    (void *)&HLG_TransferFunction,
    (void *)&BmdFilmTonemap,
    (void *)&Reinhard_3_5_Tonemap
};

/* Returns multipliers for white balance by (linearly) interpolating measured 
 * Canon values... stupidly simple, also range limited to 2500-10000 (pls obey) */
void get_kelvin_multipliers_rgb(double kelvin, double * multiplier_output)
{
    kelvin = MIN(MAX(kelvin, 2000.0), 10000.0);
    int k = 0;

    while (1 < 2)
    {
        if ( kelvin > wb_kelvin[k] && kelvin < wb_kelvin[k+1] ) 
        {
            break;
        }

        /* Doesn't need interpolation */
        else if ( kelvin == wb_kelvin[k] )
        {
            multiplier_output[0] = wb_red[k];
            multiplier_output[1] = wb_green[k];
            multiplier_output[2] = wb_blue[k];
            return;
        }

        k++;
    }

    double diff1 = (double)wb_kelvin[k+1] - (double)wb_kelvin[k];
    double diff2 = (double)wb_kelvin[k+1] - kelvin;

    /* Weight between the two */
    double weight1 = diff2 / diff1;
    double weight2 = 1.0 - weight1;

    multiplier_output[0] = (   wb_red[k] * weight1) + (   wb_red[k+1] * weight2);
    multiplier_output[1] = ( wb_green[k] * weight1) + ( wb_green[k+1] * weight2);
    multiplier_output[2] = (  wb_blue[k] * weight1) + (  wb_blue[k+1] * weight2);
}

/* Adds contrast(S-curve) to a value in a unique(or not) way */
double add_contrast ( double pixel, /* Range 0.0 - 1.0 */
                      double dark_contrast_range, 
                      double dark_contrast_factor, 
                      double light_contrast_range, 
                      double light_contrast_factor )
{
    /* Adjust based on the range, to make it not seem like range also controls strength */
    double dc_factor = dark_contrast_factor / (dark_contrast_range * 2);
    double lc_factor = light_contrast_factor / (light_contrast_range * 2);

    if (pixel < dark_contrast_range) 
        pixel = pow(pixel, 1 + (dark_contrast_range - pixel) 
        * (dc_factor
        /* Effect is reduced closer to end of range for a smooth curve: */
        * (1.0 - pixel/dark_contrast_range)) );

    pixel = 1.0 - pixel; /* Invert for contrasting the highlights */

    if (pixel < light_contrast_range && pixel > 0) 
        pixel = pow(pixel, 1 + (light_contrast_range - pixel) 
        * (lc_factor 
        * (1.0 - pixel/light_contrast_range)) );

    pixel = 1.0 - pixel; /* Invert back */

    return pixel;
}

void fromRGBtoHSV(float rgb[], float hsv[])
{
//    for(int i=0; i<3; ++i)
//        rgb[i] = max(0.0f, min(1.0f, rgb[i]));

     hsv[0] = 0.0f;
     hsv[2] = MAX(rgb[0], MAX(rgb[1], rgb[2]));
     const float delta = hsv[2] - MIN(rgb[0], MIN(rgb[1], rgb[2]));

     if (delta < FLT_MIN)
         hsv[1] = 0.0f;
     else
     {
         hsv[1] = delta / hsv[2];
         if (rgb[0] >= hsv[2])
         {
             hsv[0] = (rgb[1] - rgb[2]) / delta;
             if (hsv[0] < 0.0f)
                 hsv[0] += 6.0f;
         }
         else if (rgb[1] >= hsv[2])
             hsv[0] = 2.0f + (rgb[2] - rgb[0]) / delta;
         else
             hsv[0] = 4.0f + (rgb[0] - rgb[1]) / delta;
     }
     hsv[0] *= 60.0f;
}

void fromHSVtoRGB(const float hsv[], float rgb[])
{
    if(hsv[1] < FLT_MIN)
        rgb[0] = rgb[1] = rgb[2] = hsv[2];
    else
    {
        const float h = hsv[0] / 60.0f;
        const int i = (int)h;
        const float f = h - i;
        const float p = hsv[2] * (1.0f - hsv[1]);

        if (i & 1) {
            const float q = hsv[2] * (1.0f - (hsv[1] * f));
            switch(i) {
            case 1:
                rgb[0] = q;
                rgb[1] = hsv[2];
                rgb[2] = p;
                break;
            case 3:
                rgb[0] = p;
                rgb[1] = q;
                rgb[2] = hsv[2];
                break;
            default:
                rgb[0] = hsv[2];
                rgb[1] = p;
                rgb[2] = q;
                break;
            }
        }
        else
        {
            const float t = hsv[2] * (1.0f - (hsv[1] * (1.0f - f)));
            switch(i) {
            case 0:
                rgb[0] = hsv[2];
                rgb[1] = t;
                rgb[2] = p;
                break;
            case 2:
                rgb[0] = p;
                rgb[1] = hsv[2];
                rgb[2] = t;
                break;
            default:
                rgb[0] = t;
                rgb[1] = p;
                rgb[2] = hsv[2];
                break;
            }
        }
    }
}

static double array_average(double * array, int length)
{
    double average = 0.0;
    for (int i = 0; i < length; ++i)
    {
        average += array[i];
    }
    average /= (double)length;
    return average;
}

void colour_correct_3_way( double * rgb,
                           double h_hue, double h_sat,
                           double m_hue, double m_sat,
                           double s_hue, double s_sat )
{
    /* Not done :[ */
}

void processing_update_curves(processingObject_t * processing)
{
    /* For 3 way colour correction (highlights and shadows) */
    // double colour_highlights[3];
    // hsv_to_rgb(processing->highlight_hue, 1.0, 1.0 colour_highlights);

    // double lighten_pow = 0.6 - processing->lighten * 0.3;
    double lighten_pow = 1.0 - processing->lighten;

    /* Precalculate the contrast(curve) from 0 to 1 */
    #pragma omp parallel for
    for (int i = 0; i < 65536; ++i)
    {
        double pixel_value/*, pixel_rgb[3]*/;

        /* Pixel 0-1 */
        pixel_value = (double)i / (double)65535.0;

        /* Add contrast to pixel */
        pixel_value = add_contrast( pixel_value,
                                    processing->dark_contrast_range, 
                                    processing->dark_contrast_factor, 
                                    processing->light_contrast_range, 
                                    processing->light_contrast_factor );

        /* 'Lighten' */
        pixel_value = pow(pixel_value, lighten_pow);

        /* 3 way correction is not on right now */

        // /* Do 3-way colour correction */
        // pixel_rgb[0] = pixel_value;
        // pixel_rgb[1] = pixel_value;
        // pixel_rgb[2] = pixel_value;

        // /* Restore to original 0-65535 range */
        // pixel_rgb[0] *= 65535.0;
        // pixel_rgb[1] *= 65535.0;
        // pixel_rgb[2] *= 65535.0;

        pixel_value *= 65535.0;

        processing->pre_calc_curve_r[i] = (uint16_t)pixel_value;
        processing->pre_calc_curve_g[i] = (uint16_t)pixel_value;
        processing->pre_calc_curve_b[i] = (uint16_t)pixel_value;
    }
}

/* Calculates the final matrix (processing->main_matrix), and precalculates all the values;
 * Combines: exposure + white balance + camera specific adjustment all in one matrix! */
void processing_update_matrices(processingObject_t * processing)
{
    /* Temporary working matrix */
    double temp_matrix_a[9];
    double temp_matrix_b[9];
    double temp_matrix_c[9];

    /* (for shorter code) */
    int32_t ** pm = processing->pre_calc_matrix;

    /* Create a camera to sRGB matrix in temp_matrix_a */
    // memcpy(temp_matrix_a, processing->cam_to_sRGB_matrix, 9 * sizeof(double));
    memcpy(temp_matrix_a, (double *)id_matrix, 9 * sizeof(double)); /* just nothjng for now */

    /* whitebalance */

    // multiplyMatrices(temp_matrix_a, (double *)ciecam02, temp_matrix_b); /* No ciecam for now */
    multiplyMatrices(temp_matrix_a, (double *)id_matrix, temp_matrix_b); /* (nothing) */
    memcpy(temp_matrix_a, temp_matrix_b, 9 * sizeof(double));

    /* Multiply channels */
    for (int i = 0; i < 3; ++i) temp_matrix_b[i] *= processing->wb_multipliers[0];
    for (int i = 3; i < 6; ++i) temp_matrix_b[i] *= processing->wb_multipliers[1];
    for (int i = 6; i < 9; ++i) temp_matrix_b[i] *= processing->wb_multipliers[2];

    /* Convert back to XYZ space from cone space -> to temp_matrix_a */
    // invertMatrix((double *)ciecam02, temp_matrix_c);
    // multiplyMatrices(temp_matrix_b, temp_matrix_c, temp_matrix_a); /* No ciecam for now */
    multiplyMatrices(temp_matrix_b, (double *)id_matrix, temp_matrix_a); /* aka do nothing */

    /* Multiply the currently XYZ matrix back to RGB in to final_matrix */
    multiplyMatrices( temp_matrix_a,
                      id_matrix,
                      processing->final_matrix );

    /* Exposure, done here if smaller than 0, or no tonemapping - else done at gamma function */
    if (processing->exposure_stops < 0.0 || processing->tonemap_function == 0)
    {
        double exposure_factor = pow(2.0, processing->exposure_stops);
        for (int i = 0; i < 9; ++i) processing->final_matrix[i] *= exposure_factor;
    }

    /* Matrix stuff done I guess */

    if( processing->wbFindActive == 0 )
    {
        /* Precalculate 0-65535 */
        #pragma omp parallel for collapse(2)
        for (int i = 0; i < 9; ++i)
        {
            for (int j = 0; j < 65536; ++j)
            {
                pm[i][j] = (int32_t)((double)j * processing->final_matrix[i]);
            }
        }
    }
    else
    {
        /* If whitebalance find algorithm is on the run, we need it only for one single RGB -> faster */
        for (int i = 0; i < 9; ++i)
        {
            pm[i][processing->wbR] = (int32_t)((double)processing->wbR * processing->final_matrix[i]);
            pm[i][processing->wbG] = (int32_t)((double)processing->wbG * processing->final_matrix[i]);
            pm[i][processing->wbB] = (int32_t)((double)processing->wbB * processing->final_matrix[i]);
        }
    }

    /* Highest green value - pixels at this value will need to be reconstructed */
    processing_update_highest_green(processing);

    /* This is nice */
    printMatrix(processing->final_matrix);

    /* done? */
}

/* Calculates the final matrix for the gradient image;
 * Combines: exposure + white balance + camera specific adjustment all in one matrix! */
void processing_update_matrices_gradient(processingObject_t * processing)
{
    if( processing->wbFindActive == 1 ) return;

    /* Temporary working matrix */
    double temp_matrix_a[9];
    double temp_matrix_b[9];
    double final_matrix[9];

    /* (for shorter code) */
    int32_t ** pm = processing->pre_calc_matrix_gradient;

    /* Create a camera to sRGB matrix in temp_matrix_a */
    // memcpy(temp_matrix_a, processing->cam_to_sRGB_matrix, 9 * sizeof(double));
    memcpy(temp_matrix_a, (double *)id_matrix, 9 * sizeof(double)); /* just nothjng for now */

    /* whitebalance */

    // multiplyMatrices(temp_matrix_a, (double *)ciecam02, temp_matrix_b); /* No ciecam for now */
    multiplyMatrices(temp_matrix_a, (double *)id_matrix, temp_matrix_b); /* (nothing) */
    memcpy(temp_matrix_a, temp_matrix_b, 9 * sizeof(double));

    /* Multiply channels */
    for (int i = 0; i < 3; ++i) temp_matrix_b[i] *= processing->wb_multipliers[0];
    for (int i = 3; i < 6; ++i) temp_matrix_b[i] *= processing->wb_multipliers[1];
    for (int i = 6; i < 9; ++i) temp_matrix_b[i] *= processing->wb_multipliers[2];

    /* Convert back to XYZ space from cone space -> to temp_matrix_a */
    // invertMatrix((double *)ciecam02, temp_matrix_c);
    // multiplyMatrices(temp_matrix_b, temp_matrix_c, temp_matrix_a); /* No ciecam for now */
    multiplyMatrices(temp_matrix_b, (double *)id_matrix, temp_matrix_a); /* aka do nothing */

    /* Multiply the currently XYZ matrix back to RGB in to final_matrix */
    multiplyMatrices( temp_matrix_a,
                      id_matrix,
                      final_matrix );

    /* Exposure, done here if smaller than 0, or no tonemapping - else done at gamma function */
    if ((processing->exposure_stops + processing->gradient_exposure_stops) < 0.0 || processing->tonemap_function == 0)
    {
        double exposure_factor = pow(2.0, (processing->exposure_stops + processing->gradient_exposure_stops));
        for (int i = 0; i < 9; ++i) final_matrix[i] *= exposure_factor;
    }

    /* Matrix stuff done I guess */

    /* Precalculate 0-65535 */
    #pragma omp parallel for collapse(2)
    for (int i = 0; i < 9; ++i)
    {
        for (int j = 0; j < 65536; ++j)
        {
            pm[i][j] = (int32_t)((double)j * final_matrix[i]);
        }
    }

    /* Highest green value - pixels at this value will need to be reconstructed */
    processing_update_highest_green_gradient(processing);
}

void processing_update_highest_green(processingObject_t * processing)
{
    /* Highest green value - pixels at this value will need to be reconstructed */
    processing->highest_green = LIMIT16( MAX(processing->pre_calc_matrix[3][65535],processing->pre_calc_matrix[3][0])
                                       + MAX(processing->pre_calc_matrix[4][65535],processing->pre_calc_matrix[4][0]) 
                                       + MAX(processing->pre_calc_matrix[5][65535],processing->pre_calc_matrix[5][0]) );
}

void processing_update_highest_green_gradient(processingObject_t * processing)
{
    /* Highest green value - pixels at this value will need to be reconstructed */
    processing->highest_green_gradient = LIMIT16( MAX(processing->pre_calc_matrix_gradient[3][65535],processing->pre_calc_matrix_gradient[3][0])
                                                + MAX(processing->pre_calc_matrix_gradient[4][65535],processing->pre_calc_matrix_gradient[4][0])
                                                + MAX(processing->pre_calc_matrix_gradient[5][65535],processing->pre_calc_matrix_gradient[5][0]) );
}

/* Box blur */
void blur_image( uint16_t * __restrict in,
                 uint16_t * __restrict temp,
                 int width, int height, int radius,
                 int do_r, int do_g, int do_b, /* Which channels to blur or not */
                 int start_y, int end_y )
{
    /* Hide warnings */
    (void)start_y;
    (void)end_y;

    /* Row length */
    int rl = width * 3;

    int radius_x = radius*3;
    int y_max = height + radius;
    int x_max = (width + radius);
    int x_lim = rl-3;

    int blur_diameter = radius*2+1;

    int channels[3] = {do_r, do_g, do_b};

    /* Offset - do twice on channel '1' and '2' (Cb and Cr) */
    int limit_x = (width-radius-1)*3;
    for (int offset =0; offset < 3; ++offset)
    {
        if (channels[offset]) /* if this channel was requested */
        {
            /* Horizontal blur */
            for (int y = 0; y < height; ++y) /* rows */
            {
                uint16_t * temp_row = temp + (y * rl)+offset; /* current row ouptut */
                uint16_t * row = in + (y * rl)+offset; /* current row */

                int sum = row[0] * blur_diameter;

                /* Split in to 3 parts to avoid MIN/MAX */
                for (int x = -radius_x; x < radius_x; x+=3)
                {
                    sum -= row[MAX(x-radius_x, 0)];
                    sum += row[x+radius_x+3];
                    temp_row[MAX(x, 0)] = sum / blur_diameter;
                }
                for (int x = radius_x; x < limit_x; x+=3)
                {
                    sum -= row[x-radius_x];
                    sum += row[x+radius_x+3];
                    temp_row[x] = sum / blur_diameter;
                }
                for (int x = limit_x; x < rl; x+=3)
                {
                    sum -= row[x-radius_x];
                    sum += row[MIN(x+radius_x+3, rl-3)];
                    temp_row[x] = sum / blur_diameter;
                }
            }

            /* Vertical blur */
            int limit_y = height-radius-1;
            for (int x = 0; x < width; ++x) /* columns */
            {
                uint16_t * temp_col = in + (x*3);
                uint16_t * col = temp + (x*3);

                int sum = temp[x*3+offset] * blur_diameter;

                for (int y = -radius; y < radius; ++y)
                {
                    sum -= col[MAX((y-radius), 0)*rl+offset];
                    sum += col[(y+radius+1)*rl+offset];
                    temp_col[MAX(y, 0)*rl+offset] = sum / blur_diameter;
                }
                {
                    uint16_t * minus = col + (offset);
                    uint16_t * plus = col + ((radius*2+1)*rl + offset);
                    uint16_t * temp = temp_col + (radius*rl + offset);
                    uint16_t * end = temp_col + (limit_y*rl + offset);
                    do {
                        sum -= *minus;
                        sum += *plus;
                        *temp = sum / blur_diameter;
                        minus += rl;
                        plus += rl;
                        temp += rl;
                    } while (temp < end);
                }
                for (int y = limit_y; y < height; ++y)
                {
                    sum -= col[(y-radius)*rl+offset];
                    sum += col[MIN((y+radius+1), height-1)*rl+offset];
                    temp_col[y*rl+offset] = sum / blur_diameter;
                }
            }
        }
    }
}

void convert_rgb_to_YCbCr_omp(uint16_t * __restrict img, int32_t size, int32_t ** lut)
{
    int32_t ** ry = lut;
    uint16_t * end = img + size;

#pragma omp parallel for
    for (uint16_t * pix = img; pix < end; pix += 3)
    {
        /* RGB to YCbCr */
        int32_t pix_Y  =         ry[0][pix[0]] + ry[1][pix[1]] + ry[2][pix[2]];
        int32_t pix_Cb = 32768 + ry[3][pix[0]] + ry[4][pix[1]] + (pix[2] >> 1);
        int32_t pix_Cr = 32768 + (pix[0] >> 1) + ry[5][pix[1]] + ry[6][pix[2]];

        pix[0] = LIMIT16(pix_Y);
        pix[1] = LIMIT16(pix_Cb);
        pix[2] = LIMIT16(pix_Cr);
    }
}

void convert_YCbCr_to_rgb_omp(uint16_t * __restrict img, int32_t size, int32_t ** lut)
{
    int32_t ** yr = lut;
    uint16_t * end = img + size;

#pragma omp parallel for
    for (uint16_t * pix = img; pix < end; pix += 3)
    {
        int32_t pix_R = pix[0]                 + yr[0][pix[2]];
        int32_t pix_G = pix[0] + yr[1][pix[1]] + yr[2][pix[2]];
        int32_t pix_B = pix[0] + yr[3][pix[1]];

        pix[0] = LIMIT16(pix_R);
        pix[1] = LIMIT16(pix_G);
        pix[2] = LIMIT16(pix_B);
    }
}

void convert_rgb_to_YCbCr(uint16_t * __restrict img, int32_t size, int32_t ** lut)
{
    int32_t ** ry = lut;
    uint16_t * end = img + size;

    for (uint16_t * pix = img; pix < end; pix += 3)
    {
        /* RGB to YCbCr */
        int32_t pix_Y  =         ry[0][pix[0]] + ry[1][pix[1]] + ry[2][pix[2]];
        int32_t pix_Cb = 32768 + ry[3][pix[0]] + ry[4][pix[1]] + (pix[2] >> 1);
        int32_t pix_Cr = 32768 + (pix[0] >> 1) + ry[5][pix[1]] + ry[6][pix[2]];

        pix[0] = LIMIT16(pix_Y);
        pix[1] = LIMIT16(pix_Cb);
        pix[2] = LIMIT16(pix_Cr);
    }
}

void convert_YCbCr_to_rgb(uint16_t * __restrict img, int32_t size, int32_t ** lut)
{
    int32_t ** yr = lut;
    uint16_t * end = img + size;

    for (uint16_t * pix = img; pix < end; pix += 3)
    {
        int32_t pix_R = pix[0]                 + yr[0][pix[2]];
        int32_t pix_G = pix[0] + yr[1][pix[1]] + yr[2][pix[2]];
        int32_t pix_B = pix[0] + yr[3][pix[1]];

        pix[0] = LIMIT16(pix_R);
        pix[1] = LIMIT16(pix_G);
        pix[2] = LIMIT16(pix_B);
    }
}

void rgb_to_hsl(uint16_t *rgb, float *hsl) {
    for( int i = 0; i < 3; i++ )
    {
        hsl[i] = 0.0;
    }

    float r = (rgb[0] / 65535.0f);
    float g = (rgb[1] / 65535.0f);
    float b = (rgb[2] / 65535.0f);

    float min = MIN(MIN(r, g), b);
    float max = MAX(MAX(r, g), b);
    float delta = max - min;

    hsl[2] = (max + min) / 2;

    if (delta == 0)
    {
        hsl[0] = 0.0f;
        hsl[1] = 0.0f;
    }
    else
    {
        hsl[1] = (hsl[2] <= 0.5) ? (delta / (max + min)) : (delta / (2 - max - min));

        float hue;

        if (r == max)
        {
            hue = ((g - b) / 6) / delta;
        }
        else if (g == max)
        {
            hue = (1.0f / 3) + ((b - r) / 6) / delta;
        }
        else
        {
            hue = (2.0f / 3) + ((r - g) / 6) / delta;
        }

        if (hue < 0)
            hue += 1;
        if (hue > 1)
            hue -= 1;

        hsl[0] = (int)(hue * 360);
    }
}

static float hue_to_rgb(float v1, float v2, float vH)
{
    if (vH < 0)
        vH += 1;

    if (vH > 1)
        vH -= 1;

    if ((6 * vH) < 1)
        return (v1 + (v2 - v1) * 6 * vH);

    if ((2 * vH) < 1)
        return v2;

    if ((3 * vH) < 2)
        return (v1 + (v2 - v1) * ((2.0f / 3) - vH) * 6);

    return v1;
}

void hsl_to_rgb(float *hsl, uint16_t *rgb) {
    rgb[0] = 0;
    rgb[1] = 0;
    rgb[2] = 0;

    if (hsl[1] == 0)
    {
        rgb[0] = rgb[1] = rgb[2] = (uint16_t)LIMIT16(hsl[2] * 65535);
    }
    else
    {
        float v1, v2;
        float hue = (float)hsl[0] / 360.0;

        v2 = (hsl[2] < 0.5) ? (hsl[2] * (1 + hsl[1])) : ((hsl[2] + hsl[1]) - (hsl[2] * hsl[1]));
        v1 = 2 * hsl[2] - v2;

        rgb[0] = (uint16_t)LIMIT16(65535 * hue_to_rgb(v1, v2, hue + (1.0f / 3)));
        rgb[1] = (uint16_t)LIMIT16(65535 * hue_to_rgb(v1, v2, hue));
        rgb[2] = (uint16_t)LIMIT16(65535 * hue_to_rgb(v1, v2, hue - (1.0f / 3)));
    }
}
