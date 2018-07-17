
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

#include "libdenoising.h"
#include "mt19937ar.h"
#include "math.h"

#define MAX(i,j) ( (i)<(j) ? (j):(i) )
#define MIN(i,j) ( (i)<(j) ? (i):(j) )


#define dTiny 1e-10
#define fTiny 0.00000001f
#define fLarge 100000000.0f

void denoiseNlMeans(uint16_t *data, int width, int height, float sigma)
{
    int d_c = 3;
    int d_wh = width * height;
    int d_whc = d_c * width * height;
    float *noisy = new float[d_whc];
    float **fpI = new float*[d_c];
    float **fpO = new float*[d_c];
    float *denoised = new float[d_whc];

    for (int ii=0; ii < d_c; ii++)
    {
        fpI[ii] = &noisy[ii * d_wh];
        fpO[ii] = &denoised[ii * d_wh];
    }

    //TODO: Copy data to input

    int win = 1;
    int bloc = 10;
    float fFiltPar = 0.55f;

    nlmeans_ipol(win, bloc, sigma, fFiltPar, fpI,  fpO, d_c, width, height);

    //TODO: Copy output to data
}

void nlmeans_ipol(int iDWin,            // Half size of patch
                  int iDBloc,           // Half size of research window
                  float fSigma,         // Noise parameter
                  float fFiltPar,       // Filtering parameter
                  float **fpI,          // Input
                  float **fpO,          // Output
                  int iChannels, int iWidth,int iHeight) {

    // length of each channel
    int iwxh = iWidth * iHeight;

    //  length of comparison window
    int ihwl = (2*iDWin+1);
    int iwl = (2*iDWin+1) * (2*iDWin+1);
    int icwl = iChannels * iwl;

    // filtering parameter
    float fSigma2 = fSigma * fSigma;
    float fH = fFiltPar * fSigma;
    float fH2 = fH * fH;

    // multiply by size of patch, since distances are not normalized
    fH2 *= (float) icwl;

    // tabulate exp(-x), faster than using directly function expf
    int iLutLength = (int) rintf((float) LUTMAX * (float) LUTPRECISION);
    float *fpLut = new float[iLutLength];
    wxFillExpLut(fpLut,iLutLength);

    // auxiliary variable
    // number of denoised values per pixel
    float *fpCount = new float[iwxh];
    fpClear(fpCount, 0.0f,iwxh);

    // clear output
    for (int ii=0; ii < iChannels; ii++) fpClear(fpO[ii], 0.0f, iwxh);

    // PROCESS STARTS
    // for each pixel (x,y)
#pragma omp parallel shared(fpI, fpO)
    {


#pragma omp for schedule(dynamic) nowait

        for (int y=0; y < iHeight ; y++) {
            // auxiliary variable
            // denoised patch centered at a certain pixel
            float **fpODenoised = new float*[iChannels];
            for (int ii=0; ii < iChannels; ii++) fpODenoised[ii] = new float[iwl];

            for (int x=0 ; x < iWidth;  x++) {
                // reduce the size of the comparison window if we are near the boundary
                int iDWin0 = MIN(iDWin,MIN(iWidth-1-x,MIN(iHeight-1-y,MIN(x,y))));

                // research zone depending on the boundary and the size of the window
                int imin=MAX(x-iDBloc,iDWin0);
                int jmin=MAX(y-iDBloc,iDWin0);

                int imax=MIN(x+iDBloc,iWidth-1-iDWin0);
                int jmax=MIN(y+iDBloc,iHeight-1-iDWin0);

                //  clear current denoised patch
                for (int ii=0; ii < iChannels; ii++) fpClear(fpODenoised[ii], 0.0f, iwl);

                // maximum of weights. Used for reference patch
                float fMaxWeight = 0.0f;

                // sum of weights
                float fTotalWeight = 0.0f;

                for (int j=jmin; j <= jmax; j++)
                    for (int i=imin ; i <= imax; i++)
                        if (i!=x || j!=y) {

                            float fDif = fiL2FloatDist2(fpI,fpI,x,y,i,j,iDWin0,iChannels,iWidth,iWidth);

                            // dif^2 - 2 * fSigma^2 * N      dif is not normalized
                            fDif = MAX(fDif - 2.0f * (float) icwl *  fSigma2, 0.0f);
                            fDif = fDif / fH2;

                            float fWeight = wxSLUT(fDif,fpLut);

                            if (fWeight > fMaxWeight) fMaxWeight = fWeight;

                            fTotalWeight += fWeight;


                            for (int is=-iDWin0; is <=iDWin0; is++) {
                                int aiindex = (iDWin+is) * ihwl + iDWin;
                                int ail = (j+is)*iWidth+i;

                                for (int ir=-iDWin0; ir <= iDWin0; ir++) {

                                    int iindex = aiindex + ir;
                                    int il= ail +ir;

                                    for (int ii=0; ii < iChannels; ii++)
                                        fpODenoised[ii][iindex] += fWeight * fpI[ii][il];
                                }
                            }
                        }
                // current patch with fMaxWeight
                for (int is=-iDWin0; is <=iDWin0; is++) {
                    int aiindex = (iDWin+is) * ihwl + iDWin;
                    int ail=(y+is)*iWidth+x;

                    for (int ir=-iDWin0; ir <= iDWin0; ir++) {

                        int iindex = aiindex + ir;
                        int il=ail+ir;

                        for (int ii=0; ii < iChannels; ii++)
                            fpODenoised[ii][iindex] += fMaxWeight * fpI[ii][il];
                    }
                }
                fTotalWeight += fMaxWeight;

                // normalize average value when fTotalweight is not near zero
                if (fTotalWeight > fTiny) {
                    for (int is=-iDWin0; is <=iDWin0; is++) {
                        int aiindex = (iDWin+is) * ihwl + iDWin;
                        int ail=(y+is)*iWidth+x;

                        for (int ir=-iDWin0; ir <= iDWin0; ir++) {
                            int iindex = aiindex + ir;
                            int il=ail+ ir;

                            fpCount[il]++;

                            for (int ii=0; ii < iChannels; ii++) {
                                fpO[ii][il] += fpODenoised[ii][iindex] / fTotalWeight;

                            }
                        }
                    }
                }
            }
            for (int ii=0; ii < iChannels; ii++) delete[] fpODenoised[ii];
            delete[] fpODenoised;
        }
    }

    for (int ii=0; ii < iwxh; ii++)
        if (fpCount[ii]>0.0) {
            for (int jj=0; jj < iChannels; jj++)  fpO[jj][ii] /= fpCount[ii];

        }       else {

            for (int jj=0; jj < iChannels; jj++)  fpO[jj][ii] = fpI[jj][ii];
        }

    // delete memory
    delete[] fpLut;
    delete[] fpCount;
}

void fpClear(float *fpI,float fValue, int iLength) 
{
    for (int ii=0; ii < iLength; ii++) fpI[ii] = fValue;
}

// LUT tables
void  wxFillExpLut(float *lut, int size) 
{
    for (int i=0; i< size; i++)   lut[i]=   expf( - (float) i / LUTPRECISION);
}

float wxSLUT(float dif, float *lut) 
{
    if (dif >= (float) LUTMAXM1) return 0.0;

    int  x= (int) floor( (double) dif * (float) LUTPRECISION);

    float y1=lut[x];
    float y2=lut[x+1];

    return y1 + (y2-y1)*(dif*LUTPRECISION -  x);
}

float fiL2FloatDist1(float *u0,float *u1,int i0,int j0,int i1,int j1,int radius,int width0, int width1) {
    float dist=0.0;
    for (int s=-radius; s<= radius; s++) 
    {
        int l = (j0+s)*width0 + (i0-radius);
        float *ptr0 = &u0[l];

        l = (j1+s)*width1 + (i1-radius);
        float *ptr1 = &u1[l];

        for (int r=-radius; r<=radius; r++,ptr0++,ptr1++) {
            float dif = (*ptr0 - *ptr1);
            dist += (dif*dif);
        }
    }
    return dist;
}

float fiL2FloatDist2(float **u0,float **u1,int i0,int j0,int i1,int j1,int radius,int channels, int width0, int width1)
{
    float dif = 0.0f;

    for (int ii=0; ii < channels; ii++) {
        dif += fiL2FloatDist1(u0[ii],u1[ii],i0,j0,i1,j1,radius,width0, width1);
    }
    return dif;
}
