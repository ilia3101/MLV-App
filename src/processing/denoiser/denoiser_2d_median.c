/*!
 * \file denoise_2D_median.c
 * \author masc4ii
 * \copyright 2018
 * \brief an very easy 2D median denoiser
 */

#include <stdlib.h>
#include <string.h>
#include "denoiser_2d_median.h"

void swap(int *a, int *b)
{
    int temp;
    temp=*a;
    *a=*b;
    *b=temp;
}

/* the aim of the partition is to return the subscript of the exact */
/* position of the pivot when it is sorted */
// the low variable is used to point to the position of the next lowest element
int partition(int arr[], int first, int last)
{
    int pivot = arr[last]; // changed the pivot
    int low = first;
    int i = first; // changed
    while(i <= last-1 ){// or you can do for(i=first;i<last;i++)
        if(arr[i] < pivot){
            swap(&arr[i], &arr[low]);
            low++;
        }
        i++;
    }
    swap(&arr[last], &arr[low]);
    // after finishing putting all the lower element than the pivot
    // It's time to put the pivot into its place and return its position
    return low;
}

void quick_sort(int arr[], int first, int last)
{
    int pivot_pos;
    if(first < last){
        pivot_pos = partition(arr, first, last);
        quick_sort(arr, first, pivot_pos-1);
        quick_sort(arr, pivot_pos+1, last);
    }
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

#pragma omp parallel for// collapse(2) //collapse did not work here in test on Windows
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
            quick_sort( windowR, 0, winSize-1 );
            quick_sort( windowG, 0, winSize-1 );
            quick_sort( windowB, 0, winSize-1 );

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
