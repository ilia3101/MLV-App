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
 * @file libauxiliar.cpp
 * @brief Auxiliar functions.
 * @author Joan Duran <joan.duran@uib.es>
 */

#ifndef _LIBAUXILIAR_H_
#define _LIBAUXILIAR_H_

#define MAX(i,j) ( (i)<(j) ? (j):(i) )
#define MIN(i,j) ( (i)<(j) ? (i):(j) )

#define LUTMAX 30.0
#define LUTMAXM1 29.0
#define LUTPRECISION 1000.0

#define COEFF_YR 0.299
#define COEFF_YG 0.587
#define COEFF_YB 0.114

#include <stdlib.h>
#include <string.h>
// #include <fstream>
// #include <cmath>

/**
 * \brief  Initializate a float vector.
 * 	
 * @param[in]  u  vector input.
 * @param[out] u  vector output.
 * @param[in]  value  value inserted.
 * @param[in]  dim  vector size.
 *
 */

void fpClear(float *u, float value, int dim);

/**
 * \brief  Copy the values of a float vector into another.
 *
 * @param[in]  input  vector input.
 * @param[out] output  vector output.
 * @param[in]  dim  vectors size.
 *
 */

void fpCopy(float *input, float *output, int dim);

/**
  * \brief  Tabulate exp(-x) function
  *
  * @param[out] lut  vector.
  * @param[in]  size  length of the vector.
  *
  */

void wxFillExpLut(float *lut, int size);

/**
  * \brief  Compute exp(-x) using lut table.
  *
  * @param[in] argument	 argument of the exponential.
  * @param[in] lut	lookup table.
  * @return exponential value.
  */

float wxSLUT(float argument, float *lut);

/**
  * \brief  Compute patch distances.
  *
  * @param[in] u0, u1  images where distances are computed.
  * @param[in] i0, j0  position of central pixel.
  * @param[in] i1, j1  position of neighbouring pixel.
  * @param[in] xradius, yradius  half-size of comparison window.
  * @param[in] width0, width1  image size.
  * @return distance between patches.
  */

float fiL2FloatDist(float *u0, float *u1, int i0, int j0, int i1, int j1,
                    int xradius, int yradius, int width0, int width1);

/**
 * \brief  Transform RGB image into the YUV space.
 *
 * @param[in]  R, G, B  red, green and blue components in the common RGB space.
 * @param[out] Y, U, V  luminance, U-chrominance, and V-chrominance components
 *             in the YUV space.
 * @param[in]  dim  image size.
 */

void fiRgb2Yuv(float *R, float *G, float *B, float *Y, float *U, float *V,
               int dim);

/**
 * \brief  Order a vector
 *
 * @param[in]  fpI  vector which is to be ordered.
 * @param[in]  fpS  vector which is to be ordered according to fpI.
 * @param[out] fpI  vector ordered from minimum to maximum values.
 * @param[out] fpS  vector ordered according to fpI.
 * @param[in]  dim  image size.
 */

void fpQuickSort(float *fpI, float *fpS, int dim);

#endif
