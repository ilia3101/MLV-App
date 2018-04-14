/* Some functions that are used within raw_processing.c
 * This file is directly #included in raw_processing.c */

/* included so idiotic visual studio code shuts up */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "processing_object.h"

/* Measurements taken from 5D Mark II RAW photos using EXIFtool, surely Canon can't be wrong about WB mutipliers? */
static const int wb_kelvin[]   = {  2000,  2500,  3000,  3506,  4000,  4503,  5011,  5517,  6018,  6509,  7040,  7528,  8056,  8534,  9032,  9531, 10000 };
static const double wb_red[]   = { 1.134, 1.349, 1.596, 1.731, 1.806, 1.954, 2.081, 2.197, 2.291, 2.365, 2.444, 2.485, 2.528, 2.566, 2.612, 2.660, 2.702 };
static const double wb_green[] = { 1.155, 1.137, 1.112, 1.056, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000 };
static const double wb_blue[]  = { 4.587, 3.985, 3.184, 2.524, 2.103, 1.903, 1.760, 1.641, 1.542, 1.476, 1.414, 1.390, 1.363, 1.333, 1.296, 1.263, 1.229 };

static const double id_matrix[] = {1,0,0,0,1,0,0,0,1};

static const double xyz_to_rgb[] = {
    3.240710, -0.9692580,  0.0556352,
   -1.537260,  1.8759900, -0.2039960,
   -0.498571,  0.0415557,  1.0570700
};

/* Cone space! */
static const double ciecam02[] = {
    0.7328,  0.4296, -0.1624,
   -0.7036,  1.6975,  0.0061,
    0.0030,  0.0136,  0.9834
};

/* Tonemapping info from http://filmicworlds.com/blog/filmic-tonemapping-operators/ */

/* Reinhard - most basic but just werks */
double ReinhardTonemap(double x) { return x / (1.0 + x); }
float ReinhardTonemap_f(float x) { return x / (1.0f + x); }

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

void get_kelvin_multipliers_xyz(double kelvin, double * multiplier_output)
{
    double xyz_wb[3];
    double target[3];
    get_kelvin_multipliers_rgb(8000.0, xyz_wb);
    get_kelvin_multipliers_rgb(kelvin, target);
    for (int i = 0; i < 3; ++i) multiplier_output[i] = target[i] / xyz_wb[i];
    multiplier_output[1] *= 0.75;
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

void hsv_to_rgb(double hue, double saturation, double value, double * rgb)
{
    /* Red channel */
    if (hue < (120.0/360.0) && hue > (240.0/360.0))
    {
        if (hue < (60.0/360.0) || hue > (300.0/360.0))
        {
            rgb[0] = 1.0;
        }
        else
        {
            rgb[0] = hue;
            if (hue > (240.0/360.0)) rgb[0] = 1.0 - rgb[0];
            rgb[0] -= 60.0/360.0;
            rgb[0] *= 6.0;
        }
    }
    /* Green channel */
    if (hue < (240.0/360.0))
    {
        if (hue > (60.0/360.0) && hue < (180.0/360.0))
        {
            rgb[1] = 1.0;
        }
        else
        {
            rgb[1] = hue;
            if (rgb[1] > (180.0/360.0)) rgb[1] = (240.0/360.0) - rgb[1];
            rgb[1] *= 6.0;
        }
    }
    /* Blue channel */
    if (hue > (120.0/360.0))
    {
        if (hue > (180.0/360.0) && hue < (300.0/360.0))
        {
            rgb[2] = 1.0;
        }
        else
        {
            if (hue > (300.0/360.0)) rgb[2] = 1.0 - rgb[2];
            else rgb[2] = hue - (120.0/360.0);
            rgb[2] *= 6.0;
        }
    }

    /* Saturation + value */
    for (int i = 0; i < 3; ++i)
    {
        /* Desaturate */
        rgb[i] = rgb[i] * saturation + (1.0 - saturation);
        /* Value */
        rgb[i] *= value;
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
                      processing->xyz_to_rgb_matrix,
                      processing->final_matrix );

    /* Exposure, done here if smaller than 0, or no tonemapping - else done at gamma function */
    if (processing->exposure_stops < 0.0 || !processing->tone_mapping)
    {
        double exposure_factor = pow(2.0, processing->exposure_stops);
        for (int i = 0; i < 9; ++i) processing->final_matrix[i] *= exposure_factor;
    }

    /* Matrix stuff done I guess */

    /* Precalculate 0-65535 */
    for (int i = 0; i < 9; ++i)
    {
        for (int j = 0; j < 65536; ++j)
        {
            pm[i][j] = (int32_t)((double)j * processing->final_matrix[i]);
        }
    }

    processing_update_highest_green(processing);
    /* Highest green value - pixels at this value will need to be reconstructed */
    processing->highest_green = processing->pre_calc_gamma[ LIMIT16(MAX(pm[3][65535],pm[3][0]) + MAX(pm[4][65535],pm[4][0]) + MAX(pm[5][65535],pm[5][0])) ];

    /* This is nice */
    printMatrix(processing->final_matrix);

    /* done? */
}

void processing_update_highest_green(processingObject_t * processing)
{
    /* Highest green value - pixels at this value will need to be reconstructed */
    processing->highest_green = processing->pre_calc_gamma[ LIMIT16( MAX(processing->pre_calc_matrix[3][65535],processing->pre_calc_matrix[3][0]) 
                                                                   + MAX(processing->pre_calc_matrix[4][65535],processing->pre_calc_matrix[4][0]) 
                                                                   + MAX(processing->pre_calc_matrix[5][65535],processing->pre_calc_matrix[5][0]) ) ];
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
