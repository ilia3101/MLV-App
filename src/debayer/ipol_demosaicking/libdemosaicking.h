/*
 * Copyright 2009-2015 IPOL Image Processing On Line http://www.ipol.im/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file libdemosaicking.cpp
 * @brief Functions which implements the demosaicking algorithm proposed in
 * [J. Duran, A. Buades, "Self-Similarity and Spectral Correlation Adaptive
 * Algorithm for Color Demosaicking", IEEE Trans. Image Process., vol. 23(9),
 * pp. 4031-4040, 2014], which takes advantage of image self-similarity and
 * balances how much inter-channel correlation must be taken into account.
 * The first step of the algorithm consists in deciding a posteriori among a set
 * of four local directionally interpolated images. In a second step, a patch
 * based algorithm refines the locally interpolated image. In both cases, the
 * process applies on channel differences instead of channels themselves.
 * @author Joan Duran <joan.duran@uib.es>
 */

#ifndef _LIBDEMOSAICKING_H_
#define _LIBDEMOSAICKING_H_

#include "libauxiliar.h"
    
#define GREENPOSITION 0
#define REDPOSITION 1
#define BLUEPOSITION 2

#define NORTH 0
#define SOUTH 1
#define WEST 2
#define EAST 3

/**
 * \brief Algorithm chain for color demosaicking. It takes advantage of image
 *        self-similarity and balances how much inter-channel correlation must
 *        be taken into account. The first step of the algorithm consists in
 *        deciding a posteriori among a set of four local directionally
 *        interpolated images. In a second step, a patch-based algorithm refines
 *        the locally interpolated image. In both cases, the process applies on
 *        channel differences instead of channels themselves.
 *
 * @param[in]  red, green, blue  mosaicked red, green, and blue channels: the
 *             pointer accounts for the pixel position.
 * @param[out] ored, ogreen, oblue  full red, green, and blue channels
 *             obtained from the final algorithm chain: the pointer accounts for
 *             the pixel position.
 * @param[in]  beta  channel-correlation parameter.
 * @param[in]  h filtering parameter controlling the decay of the weights.
 * @param[in]  epsilon  thresholding parameter avoiding numerical intrincacies
 *             when computing local variation of chromatic components.
 * @param[in]  M  bounding parameter above which a discontinuity in the
 *             gradient of the luminance is considered.
 * @param[in]  halfL  half-size of the support zone where the variance of the
 *             chromatic components is computed.
 * @param[in]  reswind  half-size of research window.
 * @param[in]  compwind  half-size of comparison window.
 * @param[in]  N  number of most similar pixels for filtering.
 * @param[in]  redx, redy  coordinates of the first red value in the CFA.
 * @param[in]  width, height  image size.
 * @return 1 if exit success.
 *
 */

int ipol_demosaic( float *red, float *green, float *blue, float *ored,
                   float *ogreen, float *oblue, float beta, float h,
                   float epsilon, float M, int halfL, int reswind,
                   int compwind, int N, int redx, int redy, int width,
                   int height );

/**
 * \brief Fill in missing green values at each pixel by local directional
 *        interpolation.
 *
 * @param[in]  red, blue  mosaicked red and blue channels: the pointer accounts
 *             for the pixel position.
 * @param[in]  green  mosaicked green channel: the pointer accounts for the
 *             pixel position.
 * @param[out] green  full green channel obtained by directional interpolation:
 *             the pointer accounts for the pixel position.
 * @param[in]  beta  channel-correlation parameter.
 * @param[in]  direction  direction of local interpolation (north, south, east,
 *             or west).
 * @param[in]  redx, redy  coordinates of the first red value in the CFA.
 * @param[in]  width, height  image size.
 *
 */

void Gdirectional(float *red, float *green, float *blue, float beta,
                  int direction, int redx, int redy, int width, int height);

/**
 * \brief Fill in missing red anb blue values at each pixel by bilinear
 *        interpolation.
 *
 * @param[in]  red, blue  mosaicked red and blue channels: the pointer accounts
 *             for the pixel position.
 * @param[out] red, blue  full red and blue channels obtained by bilinear
 *             interpolation: the pointer accounts for the pixel position.
 * @param[in]  green  full green channel: the pointer accounts for the pixel
 *             position.
 * @param[in]  beta  channel-correlation parameter.
 * @param[in]  redx, redy  coordinates of the first red value in the CFA.
 * @param[in]  width, height  image size.
 *
 */

void RBbilinear(float *red, float *green, float *blue, float beta, int redx,
                int redy, int width, int height);

/**
 * \brief Compute the variation of the chromatic components U and V along
 *        one direction (north, south, east, or west).
 *
 * @param[out] u, v  variation of chromatic components U and V from the YUV
 *             space at each pixel: the pointer accounts for the pixel position.
 * @param[in]  direction  direction along which the variation of the chromatic
 *             components is computed (north, south, east, or west).
 * @param[in]  halfL  half-size of the support zone where the variance is
 *             computed.
 * @param[in]  width, height  image size.
 *
 */

void variation4d(float *u, float *v, int direction, int halfL, int width,
                 int height);

/**
 * \brief Demosaicking algorithm that fills in the missing color components by
 *        deciding a posteriori among four local directional (north, south,
 *        east, and west) interpolated images.
 *
 * @param[in]  red, green, blue  mosaicked red, green, and blue channels: the
 *             pointer accounts for the pixel position.
 * @param[out] ored, ogreen, oblue  full red, green, and blue channels obtained
 *             by local directionally interpolation: the pointer accounts for
 *             the pixel position.
 * @param[in]  beta  channel-correlation parameter.
 * @param[in]  epsilon  thresholding parameter avoiding numerical intrincacies
 *             when computing local variation of chromatic components.
 * @param[in]  halfL  half-size of the support zone where the variance of the
 *             chromatic components is computed.
 * @param[in]  redx, redy  coordinates of the first red value in the CFA.
 * @param[in]  width, height  image size.
 *
 */

void local_algorithm(float *red, float *green, float *blue, float *ored,
                     float *ogreen, float *oblue, float beta, float epsilon,
                     int halfL, int redx, int redy, int width, int height);

/**
 * \brief Algorithm that automatically estimate the values of @f$\beta$@f, which
 *        balances how much the channel-correlation can be taken advantage of,
 *        and @f$h@f$, which controls the decay of the nonlocal weight function.
 *
 * @param[in]  red, green, blue  mosaicked red, green, and blue channels: the
 *             pointer accounts for the pixel position.
 * @param[out] beta  channel-correlation parameter.
 * @param[out] h  filtering parameter controlling the decay of the nonlocal
 *             weights.
 * @param[in]  epsilon  thresholding parameter avoiding numerical intrincacies
 *             when computing local variation of chromatic components.
 * @param[in]  M  bounding parameter above which a discontinuity in the
 *             gradient of the luminance is considered.
 * @param[in]  halfL  half-size of the support zone where the variance of the
 *             chromatic components is computed.
 * @param[in]  redx, redy  coordinates of the first red value in the CFA.
 * @param[in]  width, height  image size.
 *
 */

void adaptive_parameters(float *red, float *green, float *blue, float * beta,
                         float * h, float epsilon, float M, int halfL, int redx,
                         int redy, int width, int height);

/**
 * \brief Refine the local interpolated green channel by nonlocal filtering at
 *        pixels where the green component is missing.
 *
 * @param[in]  red, green, blue  full red, green, and blue channels from local
 *             interpolation: the pointer accounts for the pixel position.
 * @param[out] ogreen  full refined green channel obtained by nonlocal
 *             filtering: the pointer accounts for the pixel position.
 * @param[in]  beta  channel-correlation parameter.
 * @param[in]  h filtering parameter controlling the decay of the weights.
 * @param[in]  reswind  half-size of research window.
 * @param[in]  compwind  half-size of comparison window.
 * @param[in]  N  number of most similar pixels for filtering.
 * @param[in]  redx, redy  coordinates of the first red value in the CFA.
 * @param[in]  width, height  image size.
 *
 */

void Gfiltering(float *red, float *green, float *blue, float *ogreen, float beta,
                float h, int reswind, int compwind, int N, int redx, int redy,
                int width, int height);

/**
 * \brief Refine the local interpolated red and blue channels by nonlocal
 *        filtering at pixels where the red and blue component, respectively,
 *        is missing.
 *
 * @param[in]  red, green, blue  full red, green, and blue channels from local
 *             interpolation: the pointer accounts for the pixel position.
 * @param[in]  ogreen  full refined green channel from nonlocal filtering: the
 *             pointer accounts for the pixel position.
 * @param[out] ored, oblue  full refined red and blue channels obtained by
 *             nonlocal filtering: the pointer accounts for the pixel position.
 * @param[in]  beta  channel-correlation parameter.
 * @param[in]  h filtering parameter controlling the decay of the weights.
 * @param[in]  reswind  half-size of research window.
 * @param[in]  compwind  half-size of comparison window.
 * @param[in]  N  number of most similar pixels for filtering.
 * @param[in]  redx, redy  coordinates of the first red value in the CFA.
 * @param[in]  width, height  image size.
 *
 */

void RBfiltering(float *red, float *green, float *blue, float *ored,
                 float *ogreen, float *oblue, float beta, float h, int reswind,
                 int compwind, int N, int redx, int redy, int width,
                 int height);

#endif
