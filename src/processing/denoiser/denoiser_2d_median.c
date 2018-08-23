/*!
 * \file denoise_2D_median.c
 * \author masc4ii
 * \copyright 2018
 * \brief an very easy 2D median denoiser
 */

#include <stdlib.h>
#include <string.h>

//Compare function, needed by qsort
int compare (const void * a, const void * b)
{
    return ( *(int*)a - *(int*)b );
}

//2D median filter
void denoise_2D_median(uint16_t *data, int width, int height, uint8_t window, uint8_t strength)
{
    //Parameter limitation and conversion
    if( strength > 100 ) strength = 100;
    float strengthF = strength / 100.0;
    float antiStrengthF = 1 - strengthF;

    uint32_t imageSize = width*height*3;

    //Make a copy
    uint16_t * noisy = malloc( imageSize * sizeof( uint16_t ) );
    memcpy( noisy, data, imageSize * sizeof( uint16_t ) );

    //Allocate window
    uint16_t winSize = window * window;
    uint16_t edgeX = window / 2;
    uint16_t edgeY = window / 2;
    uint8_t middle = window * window / 2;

#pragma omp parallel for collapse(2)
    for( uint16_t x = edgeX; x < width-edgeX; x++ )
    {
        for( uint16_t y = edgeY; y < height-edgeY; y++ )
        {
            //Allocate window
            int * windowR = malloc( winSize * sizeof( int ) );
            int * windowB = malloc( winSize * sizeof( int ) );
            int * windowG = malloc( winSize * sizeof( int ) );

            //Fill window
            uint32_t i = 0;
            for( uint16_t fx = 0; fx < window; fx++ )
            {
                for( uint16_t fy = 0; fy < window; fy++ )
                {
                    uint16_t w = x + fx - edgeX;
                    uint16_t h = y + fy - edgeY;
                    windowR[i] = noisy[(h*width+w)*3+0];
                    windowG[i] = noisy[(h*width+w)*3+1];
                    windowB[i] = noisy[(h*width+w)*3+2];
                    i++;
                }
            }
            //sort window values
            qsort (windowR, winSize, sizeof(int), compare);
            qsort (windowG, winSize, sizeof(int), compare);
            qsort (windowB, winSize, sizeof(int), compare);

            //write output
            data[(y*width+x)*3+0] = strengthF*windowR[middle] + antiStrengthF*data[(y*width+x)*3+0];
            data[(y*width+x)*3+1] = strengthF*windowG[middle] + antiStrengthF*data[(y*width+x)*3+1];
            data[(y*width+x)*3+2] = strengthF*windowB[middle] + antiStrengthF*data[(y*width+x)*3+2];

            //Cleanup window
            free( windowR );
            free( windowG );
            free( windowB );
        }
    }

    //Cleanup
    free( noisy );
}
