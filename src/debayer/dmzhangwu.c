/**
 * @file dmzhangwu.c
 * @brief Zhang-Wu LMMSE Image Demosaicking 
 * @author Pascal Getreuer <getreuer@gmail.com>
 * 
 * This file implements Zhang-Wu image demosaicking, introduced in
 * 
 *    Lei Zhang and Xiaolin Wu, "Color demosaicking via directional linear
 *    minimum mean square-error estimation," IEEE Transactions on Image 
 *    Processing, vol. 14, no. 12, pp. 2167-2178, 2005.
 * 
 * A MATLAB implementation of the method is available at
 * 
 *    http://www4.comp.polyu.edu.hk/~cslzhang/code/dlmmse.m
 * 
 * 
 * Copyright (c) 2010-2011, Pascal Getreuer
 * All rights reserved.
 * 
 * This program is free software: you can use, modify and/or 
 * redistribute it under the terms of the simplified BSD License. You 
 * should have received a copy of this license along this program. If 
 * not, see <http://www.opensource.org/licenses/bsd-license.html>.
 *
 * Changes for MLVApp by masc for multithreading.
 */

#include "basic.h"
#include "conv.h"
#include "dmzhangwu.h"
#include "../processing/raw_processing.h"

/* assume RGGB */
/* see RT rawimage.h */
static inline int FC(int row, int col)
{
    register int row2 = row%2;
    register int col2 = col%2;
    if (row2 == 0 && col2 == 0)
        return 0;  /* red */
    else if (row2 == 1 && col2 == 1)
        return 2;  /* blue */
    else
        return 1;  /* green */
}

#define MIN1(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX1(X, Y) (((X) > (Y)) ? (X) : (Y))
#define LIMIT16(X) MAX1(MIN1(X, 65535), 0)

#define MAX(a,b) \
({ __typeof__ ((a)+(b)) _a = (a); \
__typeof__ ((a)+(b)) _b = (b); \
_a > _b ? _a : _b; })

float DiagonalAverage(const float *Image, int Width, int Height, int x, int y);
float AxialAverage(const float *Image, int Width, int Height, int x, int y);


/** 
 * @brief Demosaicing using the LMMSE method of Zhang et al.
 *
 * @param Output pointer to memory to store the demosaiced image
 * @param Input the input image as a flattened 2D array
 * @param X, Y The crop starting point inside the image
 * @param Width, Height the image dimensions of the cropped area
 * @param numOverallPixels is the number of pixels in the whole image
 * RedX, RedY the coordinates of the upper-rightmost red pixel (in MLVApp = 0,0)
 * UseZhangCodeEst flag to determine how to estimate local signal mean (here always = 1)
 *
 * The Input image is a 2D float array of the input RGB values of size 
 * Width*Height in row-major order.  RedX, RedY are the coordinates of the 
 * upper-rightmost red pixel to specify the CFA pattern.
 * 
 * If UseZhangCodeEst is zero, then LMMSE is performed as described in Zhang
 * and Wu's paper.  If it is nonzero, then LMMSE is performed consistently 
 * with Zhang's reference MATLAB implementation 
 * 
 *    http://www4.comp.polyu.edu.hk/~cslzhang/code/dlmmse.m
 * 
 * The difference is in the estimation of the local signal mean.
 * 
 * In the paper, the denoising estimates the signal mean as the value of the 
 * smoothed signal averaged over a window.  In the MATLAB code, the smoothed
 * signal is used directly as the estimate of the signal mean.
 */

void ZhangWuDemosaic( lmmseinfo_t * inputdata )
{
    float * restrict Input = inputdata->Input;
    float * restrict Output = inputdata->Output;
    int winx = inputdata->winx; int winy = inputdata->winy; /* crop window for demosaicing */
    int winw = inputdata->winw; int winh = inputdata->winh;
    int Width = winw;
    int Height = winh;
    int dataOffset = winy * winw;
    int endx = winx + winw;
    int endy = winy + winh;
    int RedX = 0;
    int RedY = 0;
    int UseZhangCodeEst = 1;

    /* Window size for estimating LMMSE statistics */
    const int M = 4;
    /* Small value added in denominators to avoid divide-by-zero */
    const float DivEpsilon = 0.1f/(255*255);
    /* Interpolation filter used for horizontal and vertical interpolations */
    static float InterpCoeff[5] = {-0.25f, 0.5f, 0.5f, 0.5f, -0.25f};
    /* Approximately Gaussian smoothing filter used in the LMMSE denoising */
    static float SmoothCoeff[9] = {0.03125f, 0.0703125f, 0.1171875f, 
        0.1796875f, 0.203125f, 0.1796875f, 0.1171875f, 0.0703125f, 0.03125f};
    /* Use whole-sample symmeric boundary handling in convolutions */    
    boundaryext Boundary = GetBoundaryExt("symw");
        
    filter InterpFilter = MakeFilter(InterpCoeff, -2, 5);
    filter SmoothFilter = MakeFilter(SmoothCoeff, -4, 9);   
    const int NumPixels = Width*Height;
    const int Green = 1 - ((RedX + RedY) & 1);
    float *OutputRed = Output;
    float *OutputGreen = Output + inputdata->numOverallPixels;
    float *OutputBlue = Output + 2*inputdata->numOverallPixels;
    float *FilteredH = NULL, *FilteredV = NULL;
    float *DiffH = NULL, *DiffV = NULL, *DiffGR, *DiffGB;
    float mom1, h, H, mh, ph, Rh, v, V, mv, pv, Rv, Temp;
    int x, y, i, m, m0, m1, Success = 0;

    
    /* "white balance" and expo */
    //double wb_multipliers[3];
    //get_kelvin_multipliers_rgb(6500, wb_multipliers);
    //double max_wb = MAX( wb_multipliers[0], MAX( wb_multipliers[1], wb_multipliers[2] ) );
    //for( int i = 0; i < 3; i++ ) wb_multipliers[i] /= max_wb; //If not doing this, highlights get cyan
    //{
    //    int endx = winx + winw;
    //    int endy = winy + winh;
    //
    //    /* Applying */
    //    for (int y = winy; y < endy; ++y)
    //        for (int x = winx; x < endx; ++x)
    //            switch (FC(y,x))
    //            {
    //                case 0:
    //                    Input[y*winw+x] = LIMIT16( Input[y*winw+x] * wb_multipliers[0] );
    //                    break;
    //                case 1:
    //                    Input[y*winw+x] = LIMIT16( Input[y*winw+x] * wb_multipliers[1] );
    //                    break;
    //                case 2:
    //                    Input[y*winw+x] = LIMIT16( Input[y*winw+x] * wb_multipliers[2] );
    //            }
    //
    //}
    /* "white balance" */
        float wb_b, wb_g, wb_r; /* White balaance multipliers */
        /* Generate mulipliers and apply to the image */
        {
            int t_r=0,t_g=0,t_b=0; /* Totals we have counted (R,G and B) */
            float avg_r=0.0,avg_g=0.0,avg_b=0.0; /* Average values (RGB) */
            for (int y = winy; y < endy; y += 7) /* Skip amounts can be anything odd, lower = slower (and no point) */
                for (int x = winx; x < endx; x += 13)
                    switch (FC(y,x))
                    {
                        case 0:
                            avg_r += Input[y*winw+x]-inputdata->blacklevel;
                            t_r++;
                            break;
                        case 1:
                            avg_g += Input[y*winw+x]-inputdata->blacklevel;
                            t_g++;
                            break;
                        case 2:
                            avg_b += Input[y*winw+x]-inputdata->blacklevel;
                            t_b++;
                            break;
                    }

            /* Divide by total to get average value (and make 0-1) */
            avg_r /= (float)t_r;
            avg_g /= (float)t_g;
            avg_b /= (float)t_b;
            // printf("\nAverages:\nred %i\ngreen: %i\nblue: %i\n\n", (int)avg_r, (int)avg_g, (int)avg_b);
            avg_r = 1.0f/avg_r; /* inverty */
            avg_g = 1.0f/avg_g;
            avg_b = 1.0f/avg_b;

            /* Create multipliers */
            #define WB_POWER 2.3 /* Strengthen difference applied, seems to help */
            wb_r = powf(avg_r/MAX(MAX(avg_r, avg_g), avg_b), WB_POWER);
            wb_g = powf(avg_g/MAX(MAX(avg_r, avg_g), avg_b), WB_POWER);
            wb_b = powf(avg_b/MAX(MAX(avg_r, avg_g), avg_b), WB_POWER);

            // printf("\nWB Multipliers AMaZE\nred %f\ngreen: %f\nblue: %f\n\n", wb_r, wb_g, wb_b);

            /* Applying */
            for (int y = winy; y < endy; ++y)
                for (int x = winx; x < endx; ++x)
                    switch (FC(y,x))
                    {
                        case 0:
                            Input[y*winw+x] *= wb_r;
                            break;
                        case 1:
                            Input[y*winw+x] *= wb_g;
                            break;
                        case 2:
                            Input[y*winw+x] *= wb_b;
                    }
        }

    /* Allocate memory for workspace buffers */
    if(!(FilteredH = (float *)Malloc(sizeof(float)*NumPixels))
        || !(FilteredV = (float *)Malloc(sizeof(float)*NumPixels))
        || !(DiffGR = DiffH = (float *)Malloc(sizeof(float)*NumPixels))
        || !(DiffGB = DiffV = (float *)Malloc(sizeof(float)*NumPixels)))
        goto Catch;   
    
    /* Horizontal and vertical 1D interpolations */
    for(y = 0; y < Height; y++)
        Conv1D(FilteredH + Width*y, 1, 
            Input + dataOffset + Width*y, 1, InterpFilter, Boundary, Width);
    
    for(x = 0; x < Width; x++)
        Conv1D(FilteredV + x, Width, 
            Input + dataOffset + x, Width, InterpFilter, Boundary, Height);
    
    /* Local noise estimation for LMMSE */
    for(y = 0, i = 0; y < Height; y++)
        for(x = 0; x < Width; x++, i++)
        {
            if(((x + y) & 1) == Green)
            {
                DiffH[i] = Input[i+dataOffset] - FilteredH[i];
                DiffV[i] = Input[i+dataOffset] - FilteredV[i];
            }
            else
            {
                DiffH[i] = FilteredH[i] - Input[i+dataOffset];
                DiffV[i] = FilteredV[i] - Input[i+dataOffset];
            }
        }
    
    /* Compute the smoothed signals for LMMSE */
    for(y = 0; y < Height; y++)
        Conv1D(FilteredH + Width*y, 1, DiffH + Width*y, 1, 
            SmoothFilter, Boundary, Width);
    
    for(x = 0; x < Width; x++)
        Conv1D(FilteredV + x, Width, DiffV + x, Width,
            SmoothFilter, Boundary, Height);
    
    /* LMMSE interpolation of the green channel */
    for(y = 0, i = 0; y < Height; y++)
        for(x = 0; x < Width; x++, i++)
        {
            if(((x + y) & 1) != Green)  /* (x,y) is a red or blue location */
            {
                /* Adjust loop indices m = -M,...,M when necessary to
                   compensate for left and right boundaries.  We effectively
                   do zero-padded boundary handling. */
                m0 = (x >= M) ? -M : -x;
                m1 = (x < Width - M) ? M : (Width - x - 1);
                
                /* The following computes
                 * ph =   var   FilteredH[i + m]
                 *      m=-M,...,M
                 * Rh =   mean  (FilteredH[i + m] - DiffH[i + m])^2 
                 *      m=-M,...,M
                 * h = LMMSE estimate
                 * H = LMMSE estimate accuracy (estimated variance of h)
                 */
                
                for(m = m0, mom1 = ph = Rh = 0; m <= m1; m++)
                {
                    Temp = FilteredH[i + m];
                    mom1 += Temp;
                    ph += Temp*Temp;
                    Temp -= DiffH[i + m];
                    Rh += Temp*Temp;
                }
                
                if(!UseZhangCodeEst)
                    /* Compute mh = mean_m FilteredH[i + m] */
                    mh = mom1/(2*M + 1);
                else
                    /* Compute mh as in Zhang's MATLAB code */
                    mh = FilteredH[i];

                ph = ph/(2*M) - mom1*mom1/(2*M*(2*M + 1));
                Rh = Rh/(2*M + 1) + DivEpsilon;
                h = mh + (ph/(ph + Rh))*(DiffH[i] - mh);
                H = ph - (ph/(ph + Rh))*ph + DivEpsilon;
                
                /* Adjust loop indices for top and bottom boundaries. */
                m0 = (y >= M) ? -M : -y;
                m1 = (y < Height - M) ? M : (Height - y - 1);
                
                /* The following computes
                 * pv =   var   FilteredV[i + m]
                 *      m=-M,...,M
                 * Rv =   mean  (FilteredV[i + m] - DiffV[i + m])^2 
                 *      m=-M,...,M
                 * v = LMMSE estimate
                 * V = LMMSE estimate accuracy (estimated variance of v)
                 */
                
                for(m = m0, mom1 = pv = Rv = 0; m <= m1; m++)
                {
                    Temp = FilteredV[i + Width*m];
                    mom1 += Temp;
                    pv += Temp*Temp;
                    Temp -= DiffV[i + Width*m];
                    Rv += Temp*Temp;
                }
                
                if(!UseZhangCodeEst)
                    /* Compute mv = mean_m FilteredV[i + m] */
                    mv = mom1/(2*M + 1);
                else
                    /* Compute mv as in Zhang's MATLAB code */
                    mv = FilteredV[i];

                pv = pv/(2*M) - mom1*mom1/(2*M*(2*M + 1));
                Rv = Rv/(2*M + 1) + DivEpsilon;
                v = mv + (pv/(pv + Rv))*(DiffV[i] - mv);
                V = pv - (pv/(pv + Rv))*pv + DivEpsilon;
                
                /* Fuse the directional estimates to obtain 
                   the green component. */
                OutputGreen[i+dataOffset] = Input[i+dataOffset] + (V*h + H*v) / (H + V);
            }
            else
                OutputGreen[i+dataOffset] = Input[i+dataOffset];
        }
    
    /* Compute the primary difference signals:
          DiffGR = Green - Red   (known at red locations)
          DiffGB = Green - Blue  (known at blue locations)   */     
    for(y = winy, i = 0; y < endy; y++)
        for(x = winx; x < endx; x++, i++)
            if(((x + y) & 1) != Green)
                (((y & 1) == RedY) ? DiffGR : DiffGB)[i]
                    = OutputGreen[i+dataOffset] - Input[i+dataOffset];

    /* Interpolate DiffGR at blue locations and DiffGB at red locations */
    for(y = 0, i = 0; y < Height; y++)
        for(x = 0; x < Width; x++, i++)
            if(((x + y) & 1) != Green)
            {
                if((y & 1) == RedY)
                    DiffGB[i] = DiagonalAverage(DiffGB, Width, Height, x, y);
                else
                    DiffGR[i] = DiagonalAverage(DiffGR, Width, Height, x, y);                
            }
     
     /* Interpolate DiffGR and DiffGB at green locations */
    for(y = 0, i = 0; y < Height; y++)
        for(x = 0; x < Width; x++, i++)
            if(((x + y) & 1) == Green)
            {
                DiffGB[i] = AxialAverage(DiffGB, Width, Height, x, y);
                DiffGR[i] = AxialAverage(DiffGR, Width, Height, x, y);
            }
           
    /* Obtain the red and blue channel interpolations */
    for(y = 0, i = 0; y < Height; y++)
        for(x = 0; x < Width; x++, i++)
        {
            OutputRed[i+dataOffset] = OutputGreen[i+dataOffset] - DiffGR[i];
            OutputBlue[i+dataOffset] = OutputGreen[i+dataOffset] - DiffGB[i];
        }

    Success = 1;
Catch: /* This label is used for error handling.  If something went wrong
        above (which may be out of memory or a computation error), then
        execution jumps to this point to clean up and exit. */
    /* Free buffers */
    Free(DiffV);
    Free(DiffH);
    Free(FilteredV);
    Free(FilteredH);

    /* "white balance" and expo */
    //{
    //    int endx = winx + winw;
    //    int endy = winy + winh;
    //
    //    /* Applying */
    //    for (int y = winy; y < endy; ++y)
    //        for (int x = winx; x < endx; ++x)
    //        {
    //            OutputRed[y*winw+x] = LIMIT16( OutputRed[y*winw+x] / wb_multipliers[0] );
    //            OutputGreen[y*winw+x] = LIMIT16( OutputGreen[y*winw+x] / wb_multipliers[1] );
    //            OutputBlue[y*winw+x] = LIMIT16( OutputBlue[y*winw+x] / wb_multipliers[2] );
    //        }
    //
    //}
    /* "white balance" */
    {
        /* Invert the multipliers */
        wb_r = 1.0 / wb_r;
        wb_g = 1.0 / wb_g;
        wb_b = 1.0 / wb_b;
        for (int y = winy; y < endy; ++y)
            for (int x = winx; x < endx; ++x)
                OutputRed[y*winw+x] *= wb_r;
        for (int y = winy; y < endy; ++y)
            for (int x = winx; x < endx; ++x)
                OutputGreen[y*winw+x] *= wb_g;
        for (int y = winy; y < endy; ++y)
            for (int x = winx; x < endx; ++x)
                OutputBlue[y*winw+x] *= wb_b;
    }

    return;// Success;
}


/**
 * @brief Compute the average value of four diagonal neighbors 
 * 
 * @param Image an image in row major order
 * @param Width, Height dimensions of Image
 * @param x, y location in the image
 * 
 * Computes the average of the diagonal axial neighbors of (x,y).  For (x,y)
 * near the boundary, whole-sample symmetry is applied.
 */
float DiagonalAverage(const float *Image, int Width, int Height, int x, int y)
{
    Image += x + Width*y;
    
    if(y == 0)
    {
        if(x == 0)
            return Image[1 + Width];
        else if(x < Width - 1)
            return (Image[-1 + Width] + Image[1 + Width]) / 2;
        else 
            return Image[-1 + Width];
    }
    else if(y < Height - 1)
    {
        if(x == 0)
            return (Image[1 - Width] + Image[1 + Width]) / 2;
        else if(x < Width - 1)
            return (Image[-1 - Width] + Image[1 - Width]
                + Image[-1 + Width] + Image[1 + Width]) / 4;
        else 
            return (Image[-1 - Width] + Image[-1 + Width]) / 2;
    }
    else
    {
        if(x == 0)
            return Image[1 - Width];
        else if(x < Width - 1)
            return (Image[-1 - Width] + Image[1 - Width]) / 2;
        else 
            return Image[-1 - Width];
    }
}


/**
 * @brief Compute the average value of four axial neighbors 
 * 
 * @param Image an image in row major order
 * @param Width, Height dimensions of Image
 * @param x, y location in the image
 * 
 * Computes the average of the four axial neighbors of (x,y).  For (x,y) 
 * near the boundary, whole-sample symmetry is applied.
 */
float AxialAverage(const float *Image, int Width, int Height, int x, int y)
{    
    Image += x + Width*y;
    
    if(y == 0)
    {
        if(x == 0)
            return (Image[1] + Image[Width]) / 2;
        else if(x < Width - 1)
            return (Image[-1] + Image[1] + 2*Image[Width]) / 4;
        else 
            return (Image[-1] + Image[Width]) / 2;
        
    }
    else if(y < Height - 1)
    {
        if(x == 0)
            return (2*Image[1] + Image[-Width] + Image[Width]) / 4;
        else if(x < Width - 1)
            return (Image[-1] + Image[1] + Image[-Width] + Image[Width]) / 4;
        else 
            return (2*Image[-1] + Image[-Width] + Image[Width]) / 4;
    }
    else
    {
        if(x == 0)
            return (Image[1] + Image[-Width])/2;
        else if(x < Width - 1)
            return (Image[-1] + Image[1] + 2*Image[-Width]) / 4;
        else 
            return (Image[-1] + Image[-Width]) / 2;
    }
}
