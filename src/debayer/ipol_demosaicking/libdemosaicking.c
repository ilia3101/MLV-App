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

#include "libdemosaicking.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

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

/* Assume RGGB */
static inline int FC(int row, int col)
{
    if ((row%2) == 0 && (col%2) == 0)
        return REDPOSITION;  /* red */
    else if ((row%2) == 1 && (col%2) == 1)
        return BLUEPOSITION;  /* blue */
    else
        return GREENPOSITION;  /* green */
}

int ipol_demosaic( float *red, float *green, float *blue, float *ored,
                   float *ogreen, float *oblue, float beta, float h,
                   float epsilon, float M, int halfL, int reswind,
                   int compwind, int N, int redx, int redy, int width,
                   int height )
{
    // Image size
    int dim = width * height;
    
    // Estimate beta and h if not fixed
    if(beta == 0.0f)
        adaptive_parameters(red, green, blue, &beta, &h, epsilon, M, halfL, redx,
                            redy, width, height);
    
    printf("beta: %2.5f\n", beta);
    
    // Fist step
    // Local directional interpolation with adaptive inter-channel correlation
    float *ired = (float *)malloc(dim * sizeof(float));
    float *igreen = (float *)malloc(dim * sizeof(float));
    float *iblue = (float *)malloc(dim * sizeof(float));
    
    local_algorithm(red, green, blue, ired, igreen, iblue, beta, epsilon, halfL,
                    redx, redy, width, height);
    
    // Second step
    // Nonlocal filtering of channel differences
    Gfiltering(ired, igreen, iblue, ogreen, beta, h, reswind, compwind, N, redx,
               redy, width, height);
    RBfiltering(ired, igreen, iblue, ored, ogreen, oblue, beta, h, reswind,
                compwind, N, redx, redy, width, height);
    
    // Delete allocated memory
    free(ired); free(igreen); free(iblue);
    
    return 1;
}

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
                  int direction, int redx, int redy, int width, int height)
{
    // Initializations
    int dim = width * height;
    int bluex = 1 - redx;
    int bluey = 1 - redy;
    
    // Bilinear interpolation of Green at boundaries
    // Average of four neighbouring green pixels taking a mirror simmetry
    // at the boundaries
    for(int y = 0; y < height; y++)
    {
        int loc = y * width;
        for(int x = 0; x < width; x++)
        {
            int l = loc + x;
            
            if((FC(y,x) != GREENPOSITION) && ((x < 3) || (y < 3) ||
                                                 (x >= width-3) ||
                                                 (y >= height-3)))
            {
                int gn, gs, ge, gw;
                
                if(y > 0) gn = y-1;
                else gn = 1;
                
                if(y < height-1) gs = y+1;
                else gs = height-2 ;
                
                if(x < width-1) ge = x+1;
                else ge = width-2;
                
                if(x > 0) gw = x-1;
                else gw = 1;
                
                green[l] = 0.25f * (green[gn*width+x] + green[gs*width+x]
                                    + green[y*width+gw] + green[y*width+ge]);
            }
        }
    }
    
    // Directional interpolation of Green inside image
    float *color;
    
    for (int y = 3; y < height-3; ++y)
    {
        int loc = y * width;
        for (int x = 3; x < width-3; ++x)
        {
            int l = loc + x;
            int cfa = FC(y,x);

            if(cfa != GREENPOSITION)
            {
                if(cfa == REDPOSITION)
                    color = red;
                else
                    color = blue;
                
                if(direction == NORTH)
                    green[l] = green[l-width] + 0.5f * beta * (color[l]
                                                            - color[l-2*width]);
                else if(direction == SOUTH)
                    green[l] = green[l+width] + 0.5f * beta * (color[l]
                                                            - color[l+2*width]);
                else if(direction == WEST)
                    green[l] = green[l-1] + 0.5f * beta * (color[l]
                                                           - color[l-2]);
                else
                    green[l] = green[l+1] + 0.5f * beta * (color[l]
                                                           - color[l+2]);
            }
        }
    }
}


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
               int redy, int width, int height)
{
    // Initializations
    int dim = width * height;
    int bluex = 1 - redx;
    int bluey = 1 - redy;
    
    // Compute difference channels
    for(int i = 0; i < dim; i++)
    {
        red[i] -= beta * green[i];
        blue[i] -= beta * green[i];
    }
    
    // Interpolate blue making the average of neihbouring blue pixels
    // Take a mirror simmetry at boundaries
    for (int y = 0; y < height; ++y)
    {
        int loc = y * width;
        for (int x = 0; x < width; ++x)
        {
            int l = loc + x;
            int cfa = FC(y,x);
            
            if(cfa != BLUEPOSITION)
            {
                int gn, gs, ge, gw;
                
                if(y > 0) gn = y-1;
                else gn = 1;
                
                if(y < height-1) gs = y+1;
                else gs = height-2;
                
                if(x < width-1) ge = x+1;
                else ge = width-2;
                
                if(x > 0) gw = x-1;
                else gw = 1;
                
                
                if((cfa == GREENPOSITION) && (y % 2 == bluey))
                    blue[l] = 0.5f * (blue[y*width+ge] + blue[y*width+gw]);
                else if ((cfa == GREENPOSITION)  && (x % 2 == bluex))
                    blue[l] = 0.5f * (blue[gn*width+x] + blue[gs*width+x]);
                else
                    blue[l] = 0.25f * (blue[gn*width+ge] + blue[gn*width+gw]
                                       + blue[gs*width+ge] + blue[gs*width+gw]);
            }
        }
    }
    
    // Interpolate red making the average of neihbouring red pixels
    // Take a mirror simmetry at boundaries
    for (int y = 0; y < height; ++y)
    {
        int loc = y * width;
        for (int x = 0; x < width; ++x)
        {
            int l = loc + x;
            int cfa = FC(y,x);
            
            if(cfa != REDPOSITION)
            {
                int gn, gs, ge, gw;
                
                if(y > 0) gn = y-1;
                else gn = 1;
                
                if(y < height-1) gs = y+1;
                else gs = height-2;
                
                if(x < width-1) ge = x+1;
                else ge = width-2;
                
                if(x > 0) gw = x-1;
                else gw = 1;
                
                //! Compute red
                if((cfa == GREENPOSITION) && (y % 2 == redy))
                    red[l] = 0.5f * (red[y*width+ge] + red[y*width+gw]);
                else if((cfa == GREENPOSITION) && (x % 2 == redx))
                    red[l] = 0.5f * (red[gn*width+x] + red[gs*width+x]);
                else
                    red[l] = 0.25f * (red[gn*width+ge] + red[gn*width+gw]
                                      + red[gs*width+ge] + red[gs*width+gw]);
            }
        }
    }
    
    // Make back differences
    for(int i = 0; i < dim; i++)
    {
        red[i] += beta * green[i];
        blue[i] += beta * green[i];
    }
}

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
                 int height)
{
    // Support size when computing variance
    int support = 2 * halfL + 1;
    
    // Compute variation of chromatic components along selected direction
    if (direction == NORTH)
    {
        for(int y = 0; y < height; y++)
        {
            for(int x = 0; x < width; x++)
            {
                float sum = 0.0f;
                float value0 = u[y*width+x];
                
                for(int i = -support; i <= 0; i++)
                {
                    int s = y + i;
                    
                    float value = 0.0f;
                    
                    if((s > 0) && (s < height))
                        value = u[s*width+x] - value0;
                    
                    sum += value * value;
                }
                
                v[y*width+x] = sqrtf(sum / (float) support);
            }
        }
        
    }
    else if (direction == SOUTH)
    {
        for(int y = 0; y < height; y++)
        {
            for(int x = 0; x < width; x++)
            {
                float sum = 0.0f;
                float value0 = u[y*width+x];
                
                for(int i = 0; i <= support; i++)
                {
                    int s = y + i;
                    
                    float value = 0.0f;
                    
                    if((s > 0) && (s < height))
                        value = u[s*width+x] - value0;
                    
                    sum += value * value;
                }
                
                v[y*width+x] = sqrtf(sum / (float) support);
            }
        }
        
    }
    else if (direction == WEST)
    {
        for(int y = 0; y < height; y++)
            for(int x = 0; x < width; x++)
            {
                float sum = 0.0f;
                int l0 = y * width;
                float value0 = u[l0+x];
                
                for(int i = -support; i <= 0; i++)
                {
                    int s = x + i;
                    
                    float value = 0.0f;
                    
                    if((s > 0) && (s < width))
                        value = u[l0+s] - value0;
                    
                    sum += value * value;
                }
                
                v[l0+x] = sqrtf(sum / (float) support);
            }
        
    }
    else
    {
        for(int y = 0; y < height; y++)
        {
            for(int x = 0; x < width; x++)
            {
                float sum = 0.0f;
                int l0 = y * width;
                float value0 = u[l0+x];
                
                for(int i = 0; i <= support; i++)
                {
                    int s = x + i;
                    
                    float value = 0.0f;
                    
                    if(s > 0 && s < width)
                        value = u[l0+s] - value0;
                    
                    sum += value * value;
                }
                
                v[l0+x] = sqrtf(sum / (float) support);
            }
        }
    }
}

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
                     int halfL, int redx, int redy, int width, int height)
{
    // Initializations
    int dim = width * height;
    
    // YUV vectors
    float *y = (float *)malloc(dim * sizeof(float));
    float *u = (float *)malloc(dim * sizeof(float));
    float *v = (float *)malloc(dim * sizeof(float));
    
    // Interpolation in the north direction
    float *arn = (float *)malloc(dim * sizeof(float));
    float *agn = (float *)malloc(dim * sizeof(float));
    float *abn = (float *)malloc(dim * sizeof(float));
    
    fpCopy(red, arn, dim);
    fpCopy(green, agn, dim);
    fpCopy(blue, abn, dim);
    
    Gdirectional(arn, agn, abn, beta, NORTH, redx, redy, width, height);
    RBbilinear(arn, agn, abn, beta, redx, redy, width, height);
    
    // Convert interpolated image into YUV space
    fiRgb2Yuv(arn, agn, abn, y, u, v, dim);
    
    // Compute variation of chormatic components
    float *northuTv = (float *)malloc(dim * sizeof(float));
    float *northvTv = (float *)malloc(dim * sizeof(float));
    
    variation4d(u, northuTv, NORTH, halfL, width, height);
    variation4d(v, northvTv, NORTH, halfL, width, height);
    
    for(int i = 0; i < dim; i++)
        northuTv[i] += northvTv[i];
    
    // Interpolation in the south direction
    float *ars = (float *)malloc(dim * sizeof(float));
    float *ags = (float *)malloc(dim * sizeof(float));
    float *abs = (float *)malloc(dim * sizeof(float));
    
    fpCopy(red, ars, dim);
    fpCopy(green, ags, dim);
    fpCopy(blue, abs, dim);
    
    Gdirectional(ars, ags, abs, beta, SOUTH, redx, redy, width, height);
    RBbilinear(ars, ags, abs, beta, redx, redy, width, height);
    
    // Convert interpolated image into YUV space
    fiRgb2Yuv(ars, ags, abs, y, u, v, dim);
    
    // Compute variation of chromatic components
    float *southuTv = (float *)malloc(dim * sizeof(float));
    float *southvTv = (float *)malloc(dim * sizeof(float));
    
    variation4d(u, southuTv, SOUTH, halfL, width, height);
    variation4d(v, southvTv, SOUTH, halfL, width, height);
    
    for(int i = 0; i < dim; i++)
        southuTv[i] += southvTv[i];
    
    // Interpolation in the east direction
    float *are = (float *)malloc(dim * sizeof(float));
    float *age = (float *)malloc(dim * sizeof(float));
    float *abe = (float *)malloc(dim * sizeof(float));
    
    fpCopy(red, are, dim);
    fpCopy(green, age, dim);
    fpCopy(blue, abe, dim);
    
    Gdirectional(are, age, abe, beta, EAST, redx, redy, width, height);
    RBbilinear(are, age, abe, beta, redx, redy, width, height);
    
    // Convert interpolated image into YUV space
    fiRgb2Yuv(are, age, abe, y, u, v, dim);
    
    // Compute variation of chromatic components
    float *eastuTv = (float *)malloc(dim * sizeof(float));
    float *eastvTv = (float *)malloc(dim * sizeof(float));
    
    variation4d(u, eastuTv, EAST, halfL, width, height);
    variation4d(v, eastvTv, EAST, halfL, width, height);
    
    for(int i = 0; i < dim; i++)
        eastuTv[i] += eastvTv[i];
    
    // Interpolation in the west direction
    float *arw = (float *)malloc(dim * sizeof(float));
    float *agw = (float *)malloc(dim * sizeof(float));
    float *abw = (float *)malloc(dim * sizeof(float));
    
    fpCopy(red, arw, dim);
    fpCopy(green, agw, dim);
    fpCopy(blue, abw, dim);
    
    Gdirectional(arw, agw, abw, beta, WEST, redx, redy, width, height);
    RBbilinear(arw, agw, abw, beta, redx, redy, width, height);
    
    // Convert interpolated image into YUV space
    fiRgb2Yuv(arw, agw, abw, y, u, v, dim);
    
    // Compute variation of chromatic components
    float *westuTv = (float *)malloc(dim * sizeof(float));
    float *westvTv = (float *)malloc(dim * sizeof(float));
    
    variation4d(u, westuTv, WEST, halfL, width, height);
    variation4d(v, westvTv, WEST, halfL, width, height);
    
    for(int i = 0; i < dim; i++)
        westuTv[i] += westvTv[i];
    
    // Pixel-level fusion of full color interpolated images
    for(int i = 0; i < dim; i++)
    {
        float wSum, wNorth, wSouth, wWest, wEast;
        
        wSum = northuTv[i] + southuTv[i] + westuTv[i] + eastuTv[i];
        
        if(wSum > 0.0000000001f)
        {
            wNorth = 1.0f / (northuTv[i] + epsilon);
            wSouth = 1.0f / (southuTv[i] + epsilon);
            wWest = 1.0f / (westuTv[i] + epsilon);
            wEast = 1.0f / (eastuTv[i] + epsilon);
            
            wSum = wNorth + wSouth + wWest + wEast;
            
            wNorth = wNorth / wSum;
            wSouth = wSouth / wSum;
            wWest = wWest / wSum;
            wEast = wEast / wSum;
        }
        else
        {
            wNorth = 0.25f;
            wSouth = 0.25f;
            wWest = 0.25f;
            wEast = 0.25f;
        }
        
        ored[i] = wNorth * arn[i] + wSouth * ars[i] + wWest * arw[i] + wEast * are[i];
        ogreen[i] = wNorth * agn[i] + wSouth * ags[i] + wWest * agw[i] + wEast * age[i];
        oblue[i] = wNorth * abn[i] + wSouth * abs[i] + wWest * abw[i] + wEast * abe[i];
    }
    
    // Delete allocated memory
    free(y); free(u); free(v);
    free(arn); free(agn); free(abn);
    free(ars); free(ags); free(abs);
    free(are); free(age); free(abe);
    free(arw); free(agw); free(abw);
    free(northuTv); free(northvTv);
    free(southuTv); free(southvTv);
    free(westuTv); free(westvTv);
    free(eastuTv); free(eastvTv);
}

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
                        int redy, int width, int height)
{
    // Image size
    int dim = width * height;
    
    // Compute local directionally interpolated image with beta = 1
    float *ired = (float *)malloc(sizeof(float) * dim);
    float *igreen = (float *)malloc(sizeof(float) * dim);
    float *iblue = (float *)malloc(sizeof(float) * dim);
    
    fpCopy(red, ired, dim);
    fpCopy(green, igreen, dim);
    fpCopy(blue, iblue, dim);
    
    local_algorithm(red, green, blue, ired, igreen, iblue, 1.0f, epsilon, halfL,
                    redx, redy, width, height);
    
    // Convert interpolated image into YUV space
    float *y = (float *)malloc(sizeof(float) * dim);
    float *u = (float *)malloc(sizeof(float) * dim);
    float *v = (float *)malloc(sizeof(float) * dim);
    
    fiRgb2Yuv(ired, igreen, iblue, y, u, v, dim);
    
    // Identify inter-channel correlation by means of chromatic gradients
    float gradUC = 0.0f;
    float gradVC = 0.0f;
    float ux, uy, vx, vy, yx, yy;
    float dimC = 0.0f;
    
    for(int j = 0; j < height; j++)
    {
        for(int i = 0; i < width; i++)
        {
            if(i < width -1)
            {
                ux = u[j*width+i+1] - u[j*width+i];
                vx = v[j*width+i+1] - v[j*width+i];
                yx = y[j*width+i+1] - y[j*width+i];
                
            } else
            {
                ux = 0.0f;
                vx = 0.0f;
                yx = 0.0f;
            }
            
            if(j < height-1)
            {
                uy = u[(j+1)*width+i] - u[j*width+i];
                vy = v[(j+1)*width+i] - v[j*width+i];
                yy = y[(j+1)*width+i] - y[j*width+i];
                
            } else
            {
                uy = 0.0f;
                vy = 0.0f;
                yy = 0.0f;
            }
            
            if(fabsf(yx) + fabsf(yy) > M)
            {
                gradUC += 0.5f * (fabs(ux) + fabs(uy));
                gradVC += 0.5f * (fabs(vx) + fabs(vy));
                dimC++;
            }
        }
    }
    
    gradUC = gradUC / dimC;
    gradVC = gradVC / dimC;
    
    float gradC = 0.5f * (gradUC + gradVC);
    
    // Compute beta and h
    float denom = 1.0f + expf(-150 * gradC + 490.0f);
    *beta = 1.0f - 0.3f / denom;
    *h = 32.0f - 31.0f / denom;
    
    // Delete allocated memory
    free(ired); free(igreen); free(iblue);
    free(y); free(u); free(v);
}

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
               int width, int height)
{
    // Initializations
    int bluex = 1 - redx;
    int bluey = 1 - redy;
    int dim = width * height;
    fpCopy(green, ogreen, dim);
    
    // Patch sizes
    int resdim = (2 * reswind + 1) * (2 * reswind + 1);
    float compdim = (float) (2 * compwind + 1) * (2 * compwind + 1);
    
    // Adapt filter parameter to size of comparison window
    float filter = 3.0f * h * h * compdim;
    
    // Tabulate function exp(-x) for x>0
    int luttaille = (int) (LUTMAX * LUTPRECISION);
    float *lut = (float *)malloc(luttaille * sizeof(float));
    wxFillExpLut(lut, luttaille);
    
    // Apply nonlocal filtering
#pragma omp parallel shared(red, green, blue, ogreen, lut)
    {
#pragma omp for schedule(dynamic) nowait
        for(int y = compwind; y < height-compwind; y++)
        {
            // Store patch distances
            float *dist_list = (float *)malloc(resdim * sizeof(float));
            float *index_list = (float *)malloc(resdim * sizeof(float));
            
            fpClear(dist_list, 0.0f, resdim);
            fpClear(index_list, 0.0f, resdim);
            
            for(int x = compwind; x < width-compwind; x++)
            {
                // Index of current pixel
                int l = y * width + x;
                int cfa = FC(y,x);
                
                // Only filtering green component at pixels where is missing
                if(cfa != GREENPOSITION)
                {
                    // Learning zone depending on the window size
                    int imin = MAX(x - reswind, compwind);
                    int jmin = MAX(y - reswind, compwind);
                    int imax = MIN(x + reswind, width - compwind - 1);
                    int jmax = MIN(y + reswind, height - compwind - 1);
                    
                    
                    // Auxiliary variables for ordering distances
                    int Nindex = 0;
                    int indexCentral;
                    float distMin = 10000000000.0f;
                    
                    // Compute distance for each pixel in the neighborhood
                    for(int j = jmin; j <= jmax; j++)
                    {
                        for(int i = imin; i <= imax; i++)
                        {
                            // Index of neighborhood pixel
                            int l0 = j * width + i;
                            
                            // Compute distances
                            float dist = 0.0f;
                            dist += fiL2FloatDist(red, red, x, y, i, j,
                                                  compwind, compwind, width,
                                                  width);
                            dist += fiL2FloatDist(green, green, x, y, i, j,
                                                  compwind, compwind, width,
                                                  width);
                            dist += fiL2FloatDist(blue, blue, x, y, i, j,
                                                  compwind, compwind, width,
                                                  width);
                            dist /= filter;
                            
                            // Position of central pixel
                            if((i == x) && (j == y))
                                indexCentral = Nindex;
                            
                            // Minimum distance
                            if((i != x || j != y) && (dist < distMin))
                                distMin = dist;
                            
                            dist_list[Nindex] = dist;
                            index_list[Nindex] = l0;
                            Nindex++;
                        }
                    }
                    
                    // Set minimum distance to central pixel
                    dist_list[indexCentral] = distMin;
                    
                    // Order distances
                    fpQuickSort(dist_list, index_list, Nindex);
                    
                    // Adapt N to window size
                    float fN = (float) MIN(N, Nindex);
                    
                    // Compute weight distribution
                    int cindex;
                    float weight;
                    float gvalue = 0.0f;
                    float gweight = 0.0f;
                    float gcweight = 0.0f;
                    
                    for(int k = 0; k < Nindex; k++)
                    {
                        cindex = (int) index_list[k];
                        weight = wxSLUT(dist_list[k], lut);
                        
                        if(gcweight < fN)
                        {
                            if(cfa == BLUEPOSITION)
                                gvalue += weight * green[cindex]
                                        - weight * beta * blue[cindex];
                            else
                                gvalue += weight * green[cindex]
                                        - weight * beta * red[cindex];
                            
                            gcweight++;
                            gweight += weight;
                        }
                    }
                    
                    // Set value to central pixel
                    if(gweight > 0.0000000001f)
                    {
                        ogreen[l] = gvalue / gweight;
                        
                        if(cfa == BLUEPOSITION )
                            ogreen[l] += beta * blue[l];
                        else
                            ogreen[l] += beta * red[l];
                        
                    } else
                        ogreen[l] = green[l];
                }
            }
            
            // Delete allocated memory
            free(dist_list);
            free(index_list);
        }
    }
    
    // Delete allocated memory
    free(lut);
}

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
                 int compwind, int N, int redx, int redy, int width, int height)
{
    // Initializations
    int bluex = 1 - redx;
    int bluey = 1 - redy;
    int dim = width * height;
    fpCopy(blue, oblue, dim);
    fpCopy(red, ored, dim);
    
    // Patch sizes
    int resdim = (2 * reswind + 1) * (2 * reswind + 1);
    float compdim = (float) (2 * compwind + 1) * (2 * compwind + 1);
    
    // Adapt filter parameter to size of comparison window
    float filter = 3.0f * h * h * compdim;
    
    // Tabulate function exp(-x) for x>0.
    int luttaille = (int) (LUTMAX * LUTPRECISION);
    float *lut = (float *)malloc(luttaille * sizeof(float));
    wxFillExpLut(lut, luttaille);
    
    // Apply nonlocal filtering
#pragma omp parallel shared(red, green, blue, ored, ogreen, oblue, lut)
    {
#pragma omp for schedule(dynamic) nowait
        for(int y = compwind; y < height-compwind; y++)
        {
            // Store patch distances
            float *dist_list = (float *)malloc(resdim * sizeof(float));
            float *index_list = (float *)malloc(resdim * sizeof(float));
            
            fpClear(dist_list, 0.0f, resdim);
            fpClear(index_list, 0.0f, resdim);
            
            for(int x = compwind; x < width-compwind; x++)
            {
                // Index of current pixel
                int l = y * width + x;

                // What cfa colour pixel is
                int cfa = FC(y,x);
                
                // Learning zone depending on the window size
                int imin = MAX(x - reswind, compwind);
                int jmin = MAX(y - reswind, compwind);
                int imax = MIN(x + reswind, width - compwind - 1);
                int jmax = MIN(y + reswind, height - compwind - 1);
                
                // Auxiliary variables for ordering distances
                int Nindex = 0;
                int indexCentral;
                float distMin = 10000000000.0f;

                // For each pixel in the neighborhood
                for(int j = jmin; j <= jmax; j++)
                {
                    for(int i = imin; i <= imax; i++)
                    {
                        // Index of neighborhood pixel
                        int l0 = j * width + i;
                        
                        // Compute distances
                        float dist = 0.0f;
                        dist += fiL2FloatDist(red, red, x, y, i, j,
                                              compwind, compwind, width,
                                              width);
                        dist += fiL2FloatDist(green, green, x, y, i, j,
                                              compwind, compwind, width,
                                              width);
                        dist += fiL2FloatDist(blue, blue, x, y, i, j,
                                              compwind, compwind, width,
                                              width);
                        dist /= filter;
                        
                        // Position of central pixel
                        if((i == x) && (j == y))
                            indexCentral = Nindex;
                        
                        // Minimum distance
                        if((i != x || j != y) && (dist < distMin))
                            distMin = dist;
                        
                        dist_list[Nindex] = dist;
                        index_list[Nindex] = l0;
                        Nindex++;
                    }
                }
                
                // Set minimum distance to central pixel
                dist_list[indexCentral] = distMin;
                
                // Order distances
                printf("\n\n\n\n\nNindex = %i\n\n\n\n\n", Nindex);
                fpQuickSort(dist_list, index_list, Nindex);
                
                // Adapt N to window size
                float fN = (float) MIN(N, Nindex);
                
                // Compute weight distribution
                int cindex;
                float weight;
                float rvalue = 0.0f;
                float rweight = 0.0f;
                float rcweight = 0.0f;
                float bvalue = 0.0f;
                float bweight = 0.0f;
                float bcweight = 0.0f;
                
                for(int k = 0; k < Nindex; k++)
                {
                    cindex = (int) index_list[k];
                    weight = wxSLUT(dist_list[k], lut);
                    
                    if(rcweight < fN)
                    {
                        rvalue += weight * (red[cindex]
                                            - beta * ogreen[cindex]);
                        rcweight++;
                        rweight += weight;
                    }
                    
                    if(bcweight < fN)
                    {
                        bvalue += weight * (blue[cindex]
                                            - beta * ogreen[cindex]);
                        bcweight++;
                        bweight += weight;
                    }
                }
                
                // Set value to central pixel if missing red or blue value
                if((cfa != REDPOSITION) && (rweight > 0.0000000001f))
                    ored[l] = rvalue / rweight + beta * ogreen[l];
                else
                    ored[l] = red[l];
                
                if((cfa != BLUEPOSITION) && (bweight > 0.0000000001f))
                    oblue[l] = bvalue / bweight + beta * ogreen[l];
                else
                    oblue[l] = blue[l];
            }
            
            // Delete allocated memory
            free(dist_list);
            free(index_list);
        }
    }
    
    // Delete allocated memory
    free(lut);
}
