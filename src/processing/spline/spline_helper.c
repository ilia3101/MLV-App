/*!
 * \file spline_helper.c
 * \author masc4ii
 * \copyright 2018
 * \brief A little module which calculates a function using spline library
 */

#include "splineLib.h"
#include <stdlib.h>

//Calculate a 1d cubic spline function
//xi[nin] & yi[nin]: nin input points
//xo[nout] & yo[nout]: nout output points
//return 0 - no error
int spline1dc(float *xi, float *yi, int *nin, float *xo, float *yo, int *nout)
{
    //no equal x values at points
    float last = xi[0];
    for( int i = 1; i < *nin; i++ )
    {
        if( last >= xi[i] ) xi[i] = last + 0.000001;
        last = xi[i];
    }

    //Calculate spline
    float *ypp = (float*)malloc( *nin * sizeof( float ) );
    ypp = spline_cubic_set ( *nin, xi, yi, 0, 0.0, 0, 0.0 );

    //Calculate output function
#pragma omp parallel for
    for( int i = 0; i < *nout; i++ )
    {
        float ypval, yppval;
        yo[i] = spline_cubic_val( *nin, xi, yi, ypp, xo[i], &ypval, &yppval );
    }

    free( ypp );

    return 0;
}
