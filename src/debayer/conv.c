/**
 * @file conv.c
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

#include <string.h>
#include "conv.h"


/** @brief Clamp X to [A, B] */
#define CLAMP(X,A,B)    (((X) < (A)) ? (A) : (((X) > (B)) ? (B) : (X)))


/** @brief NULL filter object */
const filter NullFilter = {NULL, 0, 0};


/**
 * @brief (Sub)sampled 1D FIR convolution with constant boundary extension
 *
 * @param Dest pointer to memory to hold the result
 * @param DestStride step between successive output samples
 * @param Src pointer to the input data
 * @param SrcStride step between successive input samples
 * @param Filter the filter
 * @param Boundary boundary extension
 * @param N the length of the convolution
 * @param nStart, nStep, nEnd sample the convolution at nStart:nStep:nEnd
 */
void SampledConv1D(float *Dest, int DestStride, const float *Src,
    int SrcStride, filter Filter, boundaryext Boundary, int N, 
    int nStart, int nStep, int nEnd)
{    
    const int SrcStrideStep = SrcStride*nStep;
    const int LeftmostTap = 1 - Filter.Delay - Filter.Length;    
    const int StartInterior = CLAMP(-LeftmostTap, 0, N - 1);
    const int EndInterior = (Filter.Delay <  0) ? 
                            (N + Filter.Delay - 1) : (N - 1);    
    const float *SrcN, *SrcK;
    float Accum;
    int n, k;

    
    if(nEnd < nStart || nStep <= 0 || N <= 0)
        return;
    
    /* Handle the left boundary */
    for(n = nStart; n < StartInterior; n += nStep, Dest += DestStride)
    {
        for(k = 0, Accum = 0; k < Filter.Length; k++)
            Accum += Filter.Coeff[k] 
                * Boundary(Src, SrcStride, N, n - Filter.Delay - k);
        
        *Dest = Accum;
    }
    
    /* Compute the convolution on the interior of the signal:
    
       In the inner accumulation loop
       SrcK = &inputdata[n - FilterDelay - k],  k = FilterLength-1, ..., 0.
       
       The SrcN pointer is adjusted such that
       SrcN = &inputdata[n + LeftmostTap]. 
       
       If n == StartInterior, then the loop starts with
          n = -LeftmostTap, SrcN = &inputdata[0]  if LeftmostTap <= 0
          n = 0, SrcN = &inputdata[LeftmostTap]   if LeftmostTap >= 0. */
    SrcN = (LeftmostTap <= 0) ? Src : (Src + SrcStride*LeftmostTap);
    
    /* Adjust if n > StartInterior */
    SrcN += SrcStride*(n - StartInterior);
    
    for(; n <= EndInterior; n += nStep, SrcN += SrcStrideStep, Dest += DestStride)
    {
        Accum = 0;
        SrcK = SrcN;
        k = Filter.Length;
        
        while(k)
        {
            Accum += Filter.Coeff[--k] * (*SrcK);
            SrcK += SrcStride;
        }
        
        *Dest = Accum;
    }
    
    /* Handle the right boundary */
    for(; n <= nEnd; n += nStep, Dest += DestStride)
    {
        for(k = 0, Accum = 0; k < Filter.Length; k++)
            Accum += Filter.Coeff[k]
                * Boundary(Src, SrcStride, N, n - Filter.Delay - k);
        
        *Dest = Accum;
    }
}


/**
 * @brief Separable 2D FIR convolution with constant boundary extension
 * 
 * @param Dest pointer to memory to hold the result
 * @param Buffer workspace buffer of size Width*Height
 * @param Src pointer to the input image in row-major planar order
 * @param FilterX the horizontal filter
 * @param FilterY the vertical filter
 * @param Boundary boundary extension
 * @param Width image width
 * @param Height image height
 * @param NumChannels number of image channels
 */
void SeparableConv2D(float *Dest, float *Buffer, const float *Src,
    filter FilterX, filter FilterY, boundaryext Boundary, 
    int Width, int Height, int NumChannels)
{
    const int NumPixels = Width*Height;
    int i, Channel;
    
    for(Channel = 0; Channel < NumChannels; Channel++)
    {
        /* Filter Src horizontally and store the result in Buffer */
        for(i = 0; i < Height; i++)
            Conv1D(Buffer + Width*i, 1, Src + Width*i, 1, 
                FilterX, Boundary, Width);
        
        /* Filter Buffer vertically and store the result in Dest */
        for(i = 0; i < Width; i++)
            Conv1D(Dest + i, Width, Buffer + i, Width, FilterY, 
                Boundary, Height);
            
        Src += NumPixels;
        Dest += NumPixels;
    }
}


/** @brief Make a filter */
filter MakeFilter(float *Coeff, int Delay, int Length)
{
    filter Filter;
    
    Filter.Coeff = Coeff;
    Filter.Delay = Delay;
    Filter.Length = Length;
    return Filter;
}


/** @brief Allocate memory for a 1D FIR filter with length Length */
filter AllocFilter(int Delay, int Length)
{
    float *Coeff;
    
    if(Length > 0 && (Coeff = (float *)Malloc(sizeof(float)*Length)))
        return MakeFilter(Coeff, Delay, Length);
    else
        return NullFilter;
}


/** @brief Tests whether a filter is NULL */
int IsNullFilter(filter Filter)
{
    return (Filter.Coeff == NULL) ? 1:0;
}


/** 
 * @brief Construct an FIR approximation of a Gaussian filter 
 * @param Sigma standard deviation of the Gaussian filter
 * @param R support radius
 * 
 * This function returns an FIR filter approximating a Gaussian with standard
 * deviation Sigma.  It is the responsibility of the caller to call FreeFilter
 * to free the filter coefficients memory when done.
 * 
 * The support radius of the filter is R, and the filter length is 2*R + 1.  A
 * reasonable choice for R is R = (int)ceil(4*Sigma).  The coefficients are 
 * normalized to have unit sum.  If Sigma is zero, then the unit impulse filter
 * is returned.  
 */
filter GaussianFilter(double Sigma, int R)
{
    filter Filter = AllocFilter(-R, 2*R+1);
    

    if(!IsNullFilter(Filter))
    {
        if(Sigma == 0)
            Filter.Coeff[0] = 1;
        else
        {
            float Sum;
            int r;
            
            for(r = -R, Sum = 0; r <= R; r++)
            {
                Filter.Coeff[R + r] = (float)exp(-r*r/(2*Sigma*Sigma));
                Sum += Filter.Coeff[R + r];
            }
            
            for(r = -R; r <= R; r++)
                Filter.Coeff[R + r] /= Sum;
        }
    }
    
    return Filter;
}


static float ZeroPaddedExtension(const float *Src, int Stride, int N, int n)
{
    return (0 <= n && n < N) ? Src[Stride*n] : 0;
}


static float ConstantExtension(const float *Src, int Stride, int N, int n)
{
    return Src[(n < 0) ? 0 : ((n >= N) ? Stride*(N - 1) : n)];
}


static float LinearExtension(const float *Src, int Stride, int N, int n)
{
    if(0 <= n)
    {
        if(n < N)
            return Src[Stride*n];
        else if(N == 1)
            return Src[0];
        else
        {
            Src += Stride*(N - 1);
            return Src[0] + (N - 1 - n)*(Src[-Stride] - Src[0]);
        }
    }
    else if(N == 1)
        return Src[0];
    else
        return Src[0] + n*(Src[Stride] - Src[0]);
}


static float PeriodicExtension(const float *Src, int Stride, int N, int n)
{
    if(n < 0)
    {
        do
        {
            n += N;
        }while(n < 0);
    }
    else if(n >= N)
    {
        do
        {
            n -= N;
        }while(n >= N);
    }
    
    return Src[Stride*n];
}


static float SymhExtension(const float *Src, int Stride, int N, int n)
{
    while(1)
    {
        if(n < 0)
            n = -1 - n;
        else if(n >= N)
            n = 2*N - 1 - n;
        else
            break;
    }
    
    return Src[Stride*n];
}


static float SymwExtension(const float *Src, int Stride, int N, int n)
{
    while(1)
    {
        if(n < 0)
            n = -n;
        else if(n >= N)
            n = 2*(N - 1) - n;
        else
            break;
    }
    
    return Src[Stride*n];
}


static float AsymhExtension(const float *Src, int Stride, int N, int n)
{
    float Jump, Offset;
    
    
    /* Use simple formulas for -N <= n <= 2*N - 1 */
    if(0 <= n)
    {
        if(n < N)
            return Src[Stride*n];        
        else if(n <= 2*N - 1)
            return 3*Src[Stride*(N - 1)] - Src[Stride*(N - 2)]
                - Src[Stride*(2*N - 1 - n)];
    }
    else if(-N <= n)
        return 3*Src[0] - Src[Stride] - Src[Stride*(-1 - n)];
    
    /* N == 1 is a special case */
    if(N == 1)
        return Src[0];
    
    /* General formula for extension at an arbitrary n */
    Jump = 3*(Src[Stride*(N - 1)] - Src[0]) 
        - (Src[Stride*(N - 2)] - Src[Stride]);
    Offset = 0;        
    
    if(n >= N)
    {
        do
        {
            Offset += Jump;
            n -= 2*N;
        }while(n >= N);
    }
    else
    {
        while(n < -N)
        {
            Offset -= Jump;
            n += 2*N;
        }
    }
    
    if(n >= 0)
        return Src[Stride*n] + Offset;
    else
        return 3*Src[0] - Src[Stride] - Src[Stride*(-1 - n)] + Offset;
}


static float AsymwExtension(const float *Src, int Stride, int N, int n)
{
    float Jump, Offset;
    
    
    /* Use simple formulas for -N < n < 2*N - 1 */
    if(0 <= n)
    {
        if(n < N)
            return Src[Stride*n];        
        else if(n < 2*N - 1)
            return 2*Src[Stride*(N - 1)] - Src[Stride*(2*(N - 1) - n)];
    }
    else if(-N < n)
        return 2*Src[0] - Src[Stride*(-n)];
    
    /* N == 1 is a special case */
    if(N == 1)
        return Src[0];
    
    /* General formula for extension at an arbitrary n */
    Jump = 2*(Src[Stride*(N - 1)] - Src[0]);
    Offset = 0;        
    
    if(n >= N)
    {
        do
        {
            Offset += Jump;
            n -= 2*(N - 1);
        }while(n >= N);
    }
    else
    {
        while(n <= -N)
        {
            Offset -= Jump;
            n += 2*(N - 1);
        }
    }
    
    if(n >= 0)
        return Src[Stride*n] + Offset;
    else
        return 2*Src[0] - Src[Stride*(-n)] + Offset;
}


/** 
 * @brief Get function pointer to boundary extension function
 * @param Boundary string naming the boundary extension
 * 
 * Choices for Boundary are
 *    - "zpd":              zero-padded extension
 *    - "sp0" or "const":   constant extension
 *    - "sp1" or "linear":  linear extension
 *    - "per":              periodic extension
 *    - "sym" or "symh":    half-sample symmetric extension
 *    - "symw":             whole-sample symmetric extension
 *    - "asym" or "asymh":  half-sample antisymmetric extension
 *    - "asymw":            whole-sample antisymmetric extension
 */
boundaryext GetBoundaryExt(const char *Boundary)
{
    if(!strcmp(Boundary, "zpd") || !strcmp(Boundary, "zero"))
        return ZeroPaddedExtension;
    else if(!strcmp(Boundary, "sp0") || !strcmp(Boundary, "const"))
        return ConstantExtension;
    else if(!strcmp(Boundary, "sp1") || !strcmp(Boundary, "linear"))
        return LinearExtension;
    else if(!strcmp(Boundary, "per") || !strcmp(Boundary, "periodic"))
        return PeriodicExtension;
    else if(!strcmp(Boundary, "sym") 
        || !strcmp(Boundary, "symh") || !strcmp(Boundary, "hsym"))
        return SymhExtension;
    else if(!strcmp(Boundary, "symw") || !strcmp(Boundary, "wsym"))
        return SymwExtension;
    else if(!strcmp(Boundary, "asym") 
        || !strcmp(Boundary, "asymh") || !strcmp(Boundary, "hasym"))
        return AsymhExtension;
    else if(!strcmp(Boundary, "asymw") || !strcmp(Boundary, "wasym"))
        return AsymwExtension;
    else
    {
        ErrorMessage("Unknown boundary extension \"%s\".\n", Boundary);
        return NULL;
    }
}
