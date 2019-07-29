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

void wb_convert(wb_convert_info_t * wb_info, float * rawData, int width, int height, int blacklevel)
{
    // blacklevel = 0;
    /* Subtract black */
    int framesize = width*height;
    for (int i = 0; i < framesize; ++i)
    {
        float pix = rawData[i]-(float)(blacklevel);
        rawData[i] = MAX(MIN(pix, 65535), 0);
    }

    float ** __restrict imagefloat2d = (float **)alloca(height * sizeof(float *));
    for (int y = 0; y < height; ++y) imagefloat2d[y] = (float *)(rawData+(y*width));

    /* Stretch all channels to fill the whole range as much as possible */
    /* RED */
    float red_max = 0, red_min = 65535;
    for (int y = 0; y < height; y += 2) {
        for (int x = 0; x < width; x += 2) {
            float pix = imagefloat2d[y][x];
            if (pix > red_max) red_max = pix;
            if (pix < red_min) red_min = pix;
        }
    }
    /* GREEN 1 and 2 */
    float green1_max = 0, green1_min = 65535;
    for (int y = 0; y < height; y += 2) {
        for (int x = 1; x < width; x += 2) {
            float pix = imagefloat2d[y][x];
            if (pix > green1_max) green1_max = pix;
            if (pix < green1_min) green1_min = pix;
        }
    }
    for (int y = 1; y < height; y += 2) {
        for (int x = 0; x < width; x += 2) {
            float pix = imagefloat2d[y][x];
            if (pix > green1_max) green1_max = pix;
            if (pix < green1_min) green1_min = pix;
        }
    }
    /* BLUE */
    float blue_max = 0, blue_min = 65535;
    for (int y = 1; y < height; y += 2) {
        for (int x = 1; x < width; x += 2) {
            float pix = imagefloat2d[y][x];
            if (pix > blue_max) blue_max = pix;
            if (pix < blue_min) blue_min = pix;
        }
    }

    wb_info->min_r = red_min;
    wb_info->max_r = red_max;
    wb_info->min_g = green1_min;
    wb_info->max_g = green1_max;
    wb_info->min_b = blue_min;
    wb_info->max_b = blue_max;

    /* Stretch all channels to fill the whole range as much as possible */
    /* RED */
    for (int y = 0; y < height; y += 2) {
        for (int x = 0; x < width; x += 2) {
            imagefloat2d[y][x] -= wb_info->min_r;
            imagefloat2d[y][x] *= 65535.0f / (wb_info->max_r - wb_info->min_r);
            imagefloat2d[y][x] = LIMIT16(imagefloat2d[y][x]);
        }
    }
    /* GREEN 1 and 2 */
    for (int y = 0; y < height; y += 2) {
        for (int x = 1; x < width; x += 2) {
            imagefloat2d[y][x] -= wb_info->min_g;
            imagefloat2d[y][x] *= 65535.0f / (wb_info->max_g - wb_info->min_g);
            imagefloat2d[y][x] = LIMIT16(imagefloat2d[y][x]);
        }
    }
    for (int y = 1; y < height; y += 2) {
        for (int x = 0; x < width; x += 2) {
            imagefloat2d[y][x] -= wb_info->min_g;
            imagefloat2d[y][x] *= 65535.0f / (wb_info->max_g - wb_info->min_g);
            imagefloat2d[y][x] = LIMIT16(imagefloat2d[y][x]);
        }
    }
    /* BLUE */
    for (int y = 1; y < height; y += 2) {
        for (int x = 1; x < width; x += 2) {
            imagefloat2d[y][x] -= wb_info->min_b;
            imagefloat2d[y][x] *= 65535.0f / (wb_info->max_b - wb_info->min_b);
            imagefloat2d[y][x] = LIMIT16(imagefloat2d[y][x]);
        }
    }

    return;
}

void wb_undo(const wb_convert_info_t * wb_info, uint16_t * debayeredFrame, int width, int height, int blacklevel)
{
    // blacklevel = 0;
    /* Unstretch channels */
    int framesize = width*height*3;
    for (int i = 0; i < framesize; i += 3)
    {
        float r = ((float)debayeredFrame[ i ]) / (65535.0f / (wb_info->max_r - wb_info->min_r)) + wb_info->min_r + blacklevel;
        float g = ((float)debayeredFrame[i+1]) / (65535.0f / (wb_info->max_g - wb_info->min_g)) + wb_info->min_g + blacklevel;
        float b = ((float)debayeredFrame[i+2]) / (65535.0f / (wb_info->max_b - wb_info->min_b)) + wb_info->min_b + blacklevel;
        debayeredFrame[i] = LIMIT16(r);
        debayeredFrame[i+1] = LIMIT16(g);
        debayeredFrame[i+2] = LIMIT16(b);
    }
}