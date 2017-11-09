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

#include "libauxiliar.h"
#include <math.h>

/**
 * \brief  Initializate a float vector.
 * 	
 * @param[in]  u  vector input.
 * @param[out] u  vector output.
 * @param[in]  value  value inserted.
 * @param[in]  dim  vector size.
 *
 */

void fpClear(float *u, float value, int dim) 
{
    for(int i = 0; i < dim; i++) 
	    u[i] = value;
}

/**
 * \brief  Copy the values of a float vector into another.
 *
 * @param[in]  input  vector input.
 * @param[out] output  vector output.
 * @param[in]  dim  vectors size.
 *
 */

void fpCopy(float *input, float *output, int dim)
{
    if (input != output)  
        memcpy((void *) output, (const void *) input, dim * sizeof(float));
}

/**
  * \brief  Tabulate exp(-x) function
  *
  * @param[out] lut  vector.
  * @param[in]  size  length of the vector.
  *
  */

void wxFillExpLut(float *lut, int size)
{
    for(int i = 0; i < size; i++)
        lut[i] = expf(- (float) i / LUTPRECISION);
}

/**
  * \brief  Compute exp(-x) using lut table.
  *
  * @param[in] argument	 argument of the exponential.
  * @param[in] lut	lookup table.
  * @return exponential value.
  */

float wxSLUT(float argument, float *lut)
{
    if(argument >= (float) LUTMAXM1)
        return 0.0f;

    int  x = (int) floor((double) argument * (float) LUTPRECISION);

    float y1 = lut[x];
    float y2 = lut[x+1];

    return y1 + (y2 - y1) * (argument * LUTPRECISION - x);
}

/**
  * \brief  Compute patch distances.
  *
  * @param[in] u0, u1  images where distances are computed.
  * @param[in] i0, j0  position of central pixel.
  * @param[in] i1, j1  position of neighbouring pixel.
  * @param[in] xradius, yradius  half-size of comparison window.
  * @param[in] width0, width1  image sizes.
  * @return distance between patches.
  */

float fiL2FloatDist(float *u0, float *u1, int i0, int j0, int i1, int j1,
                    int xradius, int yradius, int width0, int width1)
{
    float dist = 0.0f;     
  
    for(int s = -yradius; s <= yradius; s++)
    {
        int l = (j0 + s) * width0 + (i0 - xradius);
        float *ptr0 = &u0[l];

        l = (j1 + s) * width1 + (i1 - xradius);
        float *ptr1 = &u1[l];    

        for(int r = -xradius; r <= xradius; r++, ptr0++, ptr1++)
        {
            float dif = (*ptr0 - *ptr1); 
            dist += (dif * dif); 
        }
    }

    return dist;
}

/**
 * \brief  Transform RGB image into the YUV space.
 *
 * @param[in]  R, G, B  red, green and blue components in the common RGB space.
 * @param[out] Y, U, V  luminance, U-chrominance, and V-chrominance components
 *             in the YUV space.
 * @param[in]  dim  image size.
 */

void fiRgb2Yuv(float *R, float *G, float *B, float *Y, float *U, float *V,
               int dim)
{
    for(int i = 0; i < dim; i++)
    {
        Y[i] = COEFF_YR * R[i] + COEFF_YG * G[i] + COEFF_YB * B[i];
        U[i] = R[i] - Y[i];
        V[i] = B[i] - Y[i];
    }
}

typedef struct stf_qsort
{
    float value;
    float index;
} stf_qsort;


int order_stf_qsort_increasing(const void *pVoid1, const void *pVoid2)
{
    struct stf_qsort *p1, *p2;
    
    p1 = (struct stf_qsort *) pVoid1;
    p2 = (struct stf_qsort *) pVoid2;
    
    if(p1->value < p2->value) return -1;
    if(p1->value > p2->value) return  1;
    
    return 0;
}

/**
 * \brief  Order a vector
 *
 * @param[in]  fpI  vector which is to be ordered.
 * @param[in]  fpS  vector which is to be ordered according to fpI.
 * @param[out] fpI  vector ordered from minimum to maximum values.
 * @param[out] fpS  vector ordered according to fpI.
 * @param[in]  dim  image size.
 */

void fpQuickSort(float *fpI, float *fpS, int dim)
{
    struct stf_qsort *vector = (struct stf_qsort *)malloc(dim * sizeof(struct stf_qsort));
    
    for(int i = 0; i < dim; i++)
    {
        vector[i].value = fpI[i];
        vector[i].index = fpS[i];
    }
    
    qsort(vector, dim, sizeof(stf_qsort), order_stf_qsort_increasing);
    
    for(int i = 0; i < dim; i++)
    {
        fpI[i] = vector[i].value;
        fpS[i] = vector[i].index;
    }
    
    free(vector);
}

