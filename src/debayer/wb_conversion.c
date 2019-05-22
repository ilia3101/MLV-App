/*!
 * \file wb_conversion.c
 * \author masc4ii
 * \copyright 2019
 * \brief WB conversion do & undo for ideal debayer result
 */

#include "wb_conversion.h"
#include "../processing/raw_processing.h"

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

void wb_convert(float *rawData, int width, int height, int blacklevel)
{
    /* WB adaption, needed for correct operation */
    double wb_multipliers[3];
    get_kelvin_multipliers_rgb(6500, wb_multipliers);
    double max_wb = MAX( wb_multipliers[0], MAX( wb_multipliers[1], wb_multipliers[2] ) );
    for( int i = 0; i < 3; i++ ) wb_multipliers[i] /= max_wb;
    {
#pragma omp for schedule(dynamic) collapse(2) nowait
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

void wb_undo(uint16_t *debayeredFrame, int width, int height, int blacklevel)
{
    double wb_multipliers[3];
    get_kelvin_multipliers_rgb(6500, wb_multipliers);
    double max_wb = MAX( wb_multipliers[0], MAX( wb_multipliers[1], wb_multipliers[2] ) );
    for( int i = 0; i < 3; i++ ) wb_multipliers[i] /= max_wb;
    {
#pragma omp for schedule(dynamic) collapse(2) nowait
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
