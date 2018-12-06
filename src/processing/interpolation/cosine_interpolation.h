/*!
 * \file cosine_interpolation.h
 * \author masc4ii
 * \copyright 2018
 * \brief Cosine interpolation
 */

#ifndef COSINE_INTERPOLATION_H
#define COSINE_INTERPOLATION_H

#ifdef __cplusplus
extern "C" {
#endif

extern int cosine_interpolate( float *xi, float *yi, int *nin, float *xo, float *yo, int *nout );

#ifdef __cplusplus
}
#endif

#endif // COSINE_INTERPOLATION_H
