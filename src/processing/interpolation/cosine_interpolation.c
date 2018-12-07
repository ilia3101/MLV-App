/*!
 * \file cosine_interpolation.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief Cosine interpolation
 */

#include "cosine_interpolation.h"
#include "math.h"

#if defined(__linux)
#define M_PI		3.14159265358979323846	/* pi */
#endif

//Cosine interpolation function between 2 points
float fCos(float x, float y, float t)
{
    float F = (1-cos(t*M_PI))/2;
    return x*(1-F)+y*F;
}

//Interpolate with cosine function between points
//xi[nin] & yi[nin]: nin input points
//xo[nout] & yo[nout]: nout output points
//return 0 - no error
int cosine_interpolate( float *xi, float *yi, int *nin, float *xo, float *yo, int *nout )
{
    for( int outP = 0; outP < *nout; outP++ )
    {
        float x1, x2, y1, y2, t;
        int i = 0;
        x1 = xi[i];
        y1 = yi[i];
        while( xi[i] < xo[outP] && i < *nin-1 )
        {
            x1 = xi[i];
            y1 = yi[i];
            i++;
        }
        x2 = xi[i];
        y2 = yi[i];
        if( x1 != x2 ) t = (xo[outP]-x1) / (x2-x1);
        else t = 0;
        yo[outP] = fCos( y1, y2, t );
    }
    return 0;
}
