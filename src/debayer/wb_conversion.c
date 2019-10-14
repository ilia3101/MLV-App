/*!
 * \file wb_conversion.c
 * \author masc4ii
 * \copyright 2019
 * \brief WB conversion do & undo for ideal debayer result
 */

#include "wb_conversion.h"
#include "../processing/raw_processing.h"

#ifdef __linux__
#include <alloca.h>
#endif

#include <math.h>

static inline int FC(int row, int col)
{
    register int row2 = row%2;
    register int col2 = col%2;
    if (row2 == 0 && col2 == 0)
        return 0;  /* red */
    else if (row2 == 1 && col2 == 1)
        return 2;  /* blue */
    else
        return 1;  /* green */
}

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define LIMIT16(X) MAX(MIN(X, 65535), 0)

/* Use alexa log curve for encoding bayer data (done this way in arri processing I think) */
float WBCurve(float x) { return (x > 0.010591f) ? (0.247190f * log10f(5.555556f * x + 0.052272f) + 0.385537f) : (5.367655f * x + 0.092809f); }
float WBUnCurve(float x) { return (x > 0.149658) ? (pow(10, (x - 0.385537) / 0.247190) - 0.052272) / 5.555556 : (x - 0.092809) / 5.367655; }

/* standatd deviation */
static long double variance(float * a, int n, int stride)
{
    long double sum = 0;
    long double sq_sum = 0;
    for(int i = 0; i < n; ++i) {
       sum += (long double)a[i*stride];
       sq_sum += (long double)a[i*stride] * (long double)a[i*stride];
    }
    long double mean = sum / n;
    long double variance = sq_sum / n - mean * mean;
    return variance;
}

static long double std_dev(float * a, int n, int stride)
{
    return sqrtl(variance(a, n, stride));
}

// #define USE_LOG
// #define USE_BLACKLEVEL
#define USE_STANDARD_DEVIATION

void wb_convert(wb_convert_info_t * wb_info, float * rawData, int width, int height, int blacklevel)
{
//     /* Subtract black */
//     int framesize = width*height;

// #ifdef USE_BLACKLEVEL
//     for (int i = 0; i < framesize; ++i) {
//         rawData[i] -= (float)(blacklevel);
//     }
// #endif

//     double wb_multipliers[3];
//     get_kelvin_multipliers_rgb(6500, wb_multipliers);
//     double max_wb = MAX( wb_multipliers[0], MAX( wb_multipliers[1], wb_multipliers[2] ) );
//     for (int i = 0; i < 3; i++) wb_multipliers[i] /= max_wb;

//     float ** __restrict imagefloat2d = (float **)alloca(height * sizeof(float *));
//     for (int y = 0; y < height; ++y) imagefloat2d[y] = (float *)(rawData+(y*width));

//     /* Calculte standard deviation */
// #ifdef USE_STANDARD_DEVIATION
//     /* RED */
//     long double s_r=0, s_g=0, s_b=0;
//     int n_r = 0;
//     for (int y = 0; y < height; y += 2) {
//         s_r += variance(rawData + width * y, width/2, 2);
//         ++n_r;
//     }
//     s_r /= n_r;
//     /* GREEN 1 and 2 */
//     std_dev(rawData, framesize/2, 2);
//     /* BLUE */
//     int n_b = 0;
//     for (int y = 1; y < height; y += 2) {
//         s_b += variance(rawData + width * y + 1, width/2, 2);
//         ++n_b;
//     }
//     s_b /= n_b;

//     wb_multipliers[0] = (float)s_r;
//     wb_multipliers[1] = (float)s_g;
//     wb_multipliers[2] = (float)s_b;

//     max_wb = MAX( wb_multipliers[0], MAX( wb_multipliers[1], wb_multipliers[2] ) );
//     for (int i = 0; i < 3; i++) wb_multipliers[i] /= max_wb;
// #endif

//     /* Apply white balance */
//     for (int y = 0; y < height; y += 2) {
//         for (int x = 0; x < width; x += 2) {
//             imagefloat2d[y][x] *= wb_multipliers[0];
//         }
//     }
//     /* GREEN 1 and 2 */
//     for (int y = 0; y < height; y += 2) {
//         for (int x = 1; x < width; x += 2) {
//             imagefloat2d[y][x] *= wb_multipliers[1];
//         }
//     }
//     for (int y = 1; y < height; y += 2) {
//         for (int x = 0; x < width; x += 2) {
//             imagefloat2d[y][x] *= wb_multipliers[1];
//         }
//     }
//     /* BLUE */
//     for (int y = 1; y < height; y += 2) {
//         for (int x = 1; x < width; x += 2) {
//             imagefloat2d[y][x] *= wb_multipliers[2];
//         }
//     }


//     /* Log encoding */
// #ifdef USE_LOG
//     for (int i = 0; i < framesize; ++i)
//     {
//         float pix = WBCurve(rawData[i]/65535) * 65535;
//         rawData[i] = MIN(pix, 65535);
//     }
// #endif

//     wb_info->r = wb_multipliers[0];
//     wb_info->g = wb_multipliers[1];
//     wb_info->b = wb_multipliers[2];

//     // float ** __restrict imagefloat2d = (float **)alloca(height * sizeof(float *));
//     // for (int y = 0; y < height; ++y) imagefloat2d[y] = (float *)(rawData+(y*width));

//     // /* Stretch all channels to fill the whole range as much as possible */
//     // /* RED */
//     // float red_max = 0, red_min = 65535;
//     // for (int y = 0; y < height; y += 2) {
//     //     for (int x = 0; x < width; x += 2) {
//     //         float pix = imagefloat2d[y][x];
//     //         if (pix > red_max) red_max = pix;
//     //         if (pix < red_min) red_min = pix;
//     //     }
//     // }
//     // /* GREEN 1 and 2 */
//     // float green1_max = 0, green1_min = 65535;
//     // for (int y = 0; y < height; y += 2) {
//     //     for (int x = 1; x < width; x += 2) {
//     //         float pix = imagefloat2d[y][x];
//     //         if (pix > green1_max) green1_max = pix;
//     //         if (pix < green1_min) green1_min = pix;
//     //     }
//     // }
//     // for (int y = 1; y < height; y += 2) {
//     //     for (int x = 0; x < width; x += 2) {
//     //         float pix = imagefloat2d[y][x];
//     //         if (pix > green1_max) green1_max = pix;
//     //         if (pix < green1_min) green1_min = pix;
//     //     }
//     // }
//     // /* BLUE */
//     // float blue_max = 0, blue_min = 65535;
//     // for (int y = 1; y < height; y += 2) {
//     //     for (int x = 1; x < width; x += 2) {
//     //         float pix = imagefloat2d[y][x];
//     //         if (pix > blue_max) blue_max = pix;
//     //         if (pix < blue_min) blue_min = pix;
//     //     }
//     // }

//     // wb_info->min_r = red_min;
//     // wb_info->max_r = (red_max+65535) / 2;
//     // wb_info->min_g = green1_min;
//     // wb_info->max_g = (green1_max + 65535) / 2;
//     // wb_info->min_b = blue_min;
//     // wb_info->max_b = (blue_max+65535) / 2;

//     // /* Stretch all channels to fill the whole range as much as possible */
//     // /* RED */
//     // for (int y = 0; y < height; y += 2) {
//     //     for (int x = 0; x < width; x += 2) {
//     //         imagefloat2d[y][x] -= wb_info->min_r;
//     //         imagefloat2d[y][x] *= 65535.0f / (wb_info->max_r - wb_info->min_r);
//     //         imagefloat2d[y][x] = LIMIT16(imagefloat2d[y][x]);
//     //     }
//     // }
//     // /* GREEN 1 and 2 */
//     // for (int y = 0; y < height; y += 2) {
//     //     for (int x = 1; x < width; x += 2) {
//     //         imagefloat2d[y][x] -= wb_info->min_g;
//     //         imagefloat2d[y][x] *= 65535.0f / (wb_info->max_g - wb_info->min_g);
//     //         imagefloat2d[y][x] = LIMIT16(imagefloat2d[y][x]);
//     //     }
//     // }
//     // for (int y = 1; y < height; y += 2) {
//     //     for (int x = 0; x < width; x += 2) {
//     //         imagefloat2d[y][x] -= wb_info->min_g;
//     //         imagefloat2d[y][x] *= 65535.0f / (wb_info->max_g - wb_info->min_g);
//     //         imagefloat2d[y][x] = LIMIT16(imagefloat2d[y][x]);
//     //     }
//     // }
//     // /* BLUE */
//     // for (int y = 1; y < height; y += 2) {
//     //     for (int x = 1; x < width; x += 2) {
//     //         imagefloat2d[y][x] -= wb_info->min_b;
//     //         imagefloat2d[y][x] *= 65535.0f / (wb_info->max_b - wb_info->min_b);
//     //         imagefloat2d[y][x] = LIMIT16(imagefloat2d[y][x]);
//     //     }
//     // }

//     return;

    if(blacklevel < 1000) blacklevel = -1000; //TO BE REMOVED! But this fixes blue dots for clips with blacklevel=0

    /* WB adaption, needed for correct operation */
    double wb_multipliers[3];
    get_kelvin_multipliers_rgb(6500, wb_multipliers);
    double max_wb = MAX( wb_multipliers[0], MAX( wb_multipliers[1], wb_multipliers[2] ) );
    for( int i = 0; i < 3; i++ ) wb_multipliers[i] /= max_wb;
    {
#pragma omp parallel for collapse(2)
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                switch (FC(y,x))
                {
                    case 0:
                        rawData[y*width+x] = LIMIT16( ( rawData[y*width+x] - blacklevel ) * wb_multipliers[0] );
                        break;
                    case 1:
                        rawData[y*width+x] = LIMIT16( ( rawData[y*width+x] - blacklevel ) * wb_multipliers[1] );
                        break;
                    case 2:
                        rawData[y*width+x] = LIMIT16( ( rawData[y*width+x] - blacklevel ) * wb_multipliers[2] );
                        break;
                    default:
                        break;
                }
            }

    }
}

void wb_undo(const wb_convert_info_t * wb_info, uint16_t * debayeredFrame, int width, int height, int blacklevel)
{
//     /* Unstretch channels */
//     int framesize = width*height*3;

//     double wb_multipliers[3];
// #ifdef USE_STANDARD_DEVIATION
//     wb_multipliers[0] = wb_info->r;
//     wb_multipliers[1] = wb_info->g;
//     wb_multipliers[2] = wb_info->b;
// #else
//     get_kelvin_multipliers_rgb(6500, wb_multipliers);
// #endif
//     double max_wb = MAX(wb_multipliers[0], MAX(wb_multipliers[1], wb_multipliers[2]));
//     for (int i = 0; i < 3; i++) wb_multipliers[i] = max_wb / wb_multipliers[i];

// #ifdef USE_LOG
//     for (int i = 0; i < framesize; ++i)
//     {
//         float pix = WBUnCurve((float)debayeredFrame[i] / 65535.0) * 65535.0;
//         debayeredFrame[i] = LIMIT16(pix);
//     }
// #endif
// #ifdef USE_BLACKLEVEL
//     for (int i = 0; i < framesize; i += 3)
//     {
//         float r = ((float)debayeredFrame[ i ]) * wb_multipliers[0] + (float)(blacklevel);
//         float g = ((float)debayeredFrame[i+1]) * wb_multipliers[1] + (float)(blacklevel);
//         float b = ((float)debayeredFrame[i+2]) * wb_multipliers[2] + (float)(blacklevel);
//         debayeredFrame[i] = LIMIT16(r);
//         debayeredFrame[i+1] = LIMIT16(g);
//         debayeredFrame[i+2] = LIMIT16(b);
//     }
// #else
//     for (int i = 0; i < framesize; i += 3)
//     {
//         float r = ((float)debayeredFrame[ i ]) * wb_multipliers[0]; 
//         float g = ((float)debayeredFrame[i+1]) * wb_multipliers[1];
//         float b = ((float)debayeredFrame[i+2]) * wb_multipliers[2];
//         debayeredFrame[i] = LIMIT16(r);
//         debayeredFrame[i+1] = LIMIT16(g);
//         debayeredFrame[i+2] = LIMIT16(b);
//     }
// #endif


//     // for (int i = 0; i < framesize; i += 3)
//     // {
//     //     float r = ((float)debayeredFrame[ i ]) / (65535.0f / (wb_info->max_r - wb_info->min_r)) + wb_info->min_r + blacklevel;
//     //     float g = ((float)debayeredFrame[i+1]) / (65535.0f / (wb_info->max_g - wb_info->min_g)) + wb_info->min_g + blacklevel;
//     //     float b = ((float)debayeredFrame[i+2]) / (65535.0f / (wb_info->max_b - wb_info->min_b)) + wb_info->min_b + blacklevel;
//     //     debayeredFrame[i] = LIMIT16(r);
//     //     debayeredFrame[i+1] = LIMIT16(g);
//     //     debayeredFrame[i+2] = LIMIT16(b);
//     // }

    if(blacklevel < 1000) blacklevel = -1000; //TO BE REMOVED! But this fixes blue dots for clips with blacklevel=0

    double wb_multipliers[3];
    get_kelvin_multipliers_rgb(6500, wb_multipliers);
    double max_wb = MAX( wb_multipliers[0], MAX( wb_multipliers[1], wb_multipliers[2] ) );
    for( int i = 0; i < 3; i++ ) wb_multipliers[i] /= max_wb;
    {
#pragma omp parallel for collapse(2)
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                int idx = (y*width+x)*3;
                debayeredFrame[idx  ] = LIMIT16( ( debayeredFrame[idx  ] / wb_multipliers[0] ) + blacklevel );
                debayeredFrame[idx+1] = LIMIT16( ( debayeredFrame[idx+1] / wb_multipliers[1] ) + blacklevel );
                debayeredFrame[idx+2] = LIMIT16( ( debayeredFrame[idx+2] / wb_multipliers[2] ) + blacklevel );
            }
    }
}
