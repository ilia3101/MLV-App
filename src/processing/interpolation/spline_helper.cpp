/*!
 * \file spline_helper.c
 * \author masc4ii
 * \copyright 2018
 * \brief A little module which calculates a function using spline library
 */

#include "spline.h"
#include <stdlib.h>
#include <vector>

//Calculate a 1d cubic spline function via C++ library
//xi[nin] & yi[nin]: nin input points
//xo[nout] & yo[nout]: nout output points
//return 0 - no error
int spline1dccc( float *xi, float *yi, int *nin, float *xo, float *yo, int *nout )
{
    //no equal x values at points
    float last = xi[0];
    for( int i = 1; i < *nin; i++ )
    {
        if( last >= xi[i] ) xi[i] = last + 0.000001;
        last = xi[i];
    }

    //Conversion of input for library
    std::vector<double> X(*nin), Y(*nin);
    for( int i = 0; i < *nin; i++ )
    {
        X[i] = xi[i];
        Y[i] = yi[i];
    }

    //Calculate spline
    tk::spline s;
    s.set_points(X,Y);    // currently it is required that X is already sorted
    for( int i = 0; i < *nout; i++ )
    {
        yo[i] = s( xo[i] );
    }
    return 0;
}

#ifdef __cplusplus
extern "C" {
#endif

//Calculate a 1d cubic spline function
//xi[nin] & yi[nin]: nin input points
//xo[nout] & yo[nout]: nout output points
//return 0 - no error
int spline1dc( float *xi, float *yi, int *nin, float *xo, float *yo, int *nout )
{
    return spline1dccc( xi, yi, nin, xo, yo, nout );
}

#ifdef __cplusplus
}
#endif
