
/*
 * Copyright (c) 2009-2011, A. Buades <toni.buades@uib.es>
 * All rights reserved.
 *
 *
 * Patent warning:
 *
 * This file implements algorithms possibly linked to the patents
 *
 * # A. Buades, T. Coll and J.M. Morel, Image data processing method by
 * reducing image noise, and camera integrating means for implementing
 * said method, EP Patent 1,749,278 (Feb. 7, 2007).
 *
 * This file is made available for the exclusive aim of serving as
 * scientific tool to verify the soundness and completeness of the
 * algorithm description. Compilation, execution and redistribution
 * of this file may violate patents rights in certain countries.
 * The situation being different for every country and changing
 * over time, it is your responsibility to determine which patent
 * rights restrictions apply to you before you compile, use,
 * modify, or redistribute this file. A patent lawyer is qualified
 * to make this determination.
 * If and only if they don't conflict with any patent terms, you
 * can benefit from the following license terms attached to this
 * file.
 *
 * License:
 *
 * This program is provided for scientific and educational only:
 * you can use and/or modify it for these purposes, but you are
 * not allowed to redistribute this work or derivative works in
 * source or executable form. A license must be obtained from the
 * patent right holders for any other use.
 *
 *
 */

#ifndef _LIBDENOISING_H_
#define _LIBDENOISING_H_

#include "stdint.h"

/**
 * @file   libdenoising.cpp
 * @brief  Denoising functions
 */

//TODO: no access from C code yet

void denoiseNlMeans(uint16_t *data, int width, int height, float sigma);

void nlmeans_ipol(int iDWin,                    // Half size of comparison window
                  int iDBloc,           // Half size of research window
                  float fSigma,         // Noise parameter
                  float fFiltPar,       // Filtering parameter
                  float **fpI,          // Input
                  float **fpO,          // Output
                  int iChannels, int iWidth,int iHeight);

///// LUT tables
#define LUTMAX 30.0
#define LUTMAXM1 29.0
#define LUTPRECISION 1000.0

void  wxFillExpLut(float *lut,int size);        // Fill exp(-x) lut
float wxSLUT(float dif,float *lut);                     // look at LUT
void fpClear(float *fpI,float fValue, int iLength);
float fiL2FloatDist1 ( float * u0, float *u1, int i0, int j0,int i1,int j1,int radius, int width0, int width1);
float fiL2FloatDist2 (float **u0, float **u1,int i0,int j0,int i1,int j1,int radius,int channels, int width0, int width1);

#endif
