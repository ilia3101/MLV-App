/**
 * @file conv.h
 * @brief Convolution functions
 * @author Pascal Getreuer <getreuer@gmail.com>
 * 
 * 
 * Copyright (c) 2010-2011, Pascal Getreuer
 * All rights reserved.
 * 
 * This program is free software: you can use, modify and/or 
 * redistribute it under the terms of the simplified BSD License. You 
 * should have received a copy of this license along this program. If 
 * not, see <http://www.opensource.org/licenses/bsd-license.html>.
 */

#ifndef _CONV_H_
#define _CONV_H_

#include "basic.h"


/** @brief struct representing a 1D FIR filter */
typedef struct
{
    /** @brief Filter coefficients */
    float *Coeff;
    /** @brief The filter delay (negative for a non-causal filter) */
    int Delay;
    /** @brief The filter length, number of taps */
    int Length;
} filter;

/** @brief typedef representing a boundary extension function */
typedef float (*boundaryext)(const float*, int, int, int);


void SampledConv1D(float *Dest, int DestStride, const float *Src,
    int SrcStride, filter Filter, boundaryext Boundary, int N, 
    int nStart, int nStep, int nEnd);

void SeparableConv2D(float *Dest, float *Buffer, const float *Src,
    filter FilterX, filter FilterY, boundaryext Boundary, 
    int Width, int Height, int NumChannels);

filter MakeFilter(float *Coeff, int Delay, int Length);

filter AllocFilter(int Delay, int Length);

int IsNullFilter(filter Filter);

filter GaussianFilter(double Sigma, int R);

boundaryext GetBoundaryExt(const char *Boundary);


/* Macro definitions */

/** @brief Free a filter */
#define FreeFilter(Filter)  (Free((Filter).Coeff))

/**
 * @brief 1D FIR convolution with constant boundary extension
 *
 * @param Dest pointer to memory to hold the result
 * @param DestStride step between successive output samples
 * @param Src pointer to the input data
 * @param SrcStride step between successive input samples
 * @param Filter the filter
 * @param Boundary boundary extension
 * @param N the length of the convolution
 */
#define Conv1D(Dest, DestStride, Src, SrcStride, Filter, Boundary, N) \
    (SampledConv1D(Dest, DestStride, Src, SrcStride, Filter, Boundary, N, \
    0, 1, N - 1))


extern const filter NullFilter;

#endif /* _CONV_H_ */
