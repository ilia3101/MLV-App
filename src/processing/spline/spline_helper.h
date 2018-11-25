/*!
* \file spline_helper.h
* \author masc4ii
* \copyright 2018
* \brief A little module which calculates a function using spline library
*/

#ifndef SPLINE_HELPER_H
#define SPLINE_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

int spline1dc( float *xi , float *yi , int *nin  ,
               float *xo , float *yo , int *nout );

#ifdef __cplusplus
}
#endif

#endif // SPLINE_HELPER_H
