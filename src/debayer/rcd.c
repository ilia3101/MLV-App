/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2017-2018 Luis Sanz Rodriguez (luis.sanz.rodriguez(at)gmail(dot)com), Ingo Weyrich (heckflosse67@gmx.de) and masc4ii
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "math.h"
#include "memory.h"
#include "stdlib.h"
#include "debayer.h"

#ifdef __GNUC__
        #define RESTRICT    __restrict__
        #define LIKELY(x)   __builtin_expect (!!(x), 1)
        #define UNLIKELY(x) __builtin_expect (!!(x), 0)
        #define ALIGNED64 __attribute__ ((aligned (64)))
        #define ALIGNED16 __attribute__ ((aligned (16)))
#else
        #define RESTRICT
        #define LIKELY(x)    (x)
        #define UNLIKELY(x)  (x)
        #define ALIGNED64
        #define ALIGNED16
#endif

#ifndef MIN
  #define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
  #define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#define LIM(x,min,max) MAX(min,MIN(x,max))
#define LIM01(x) MAX(0,MIN(x,1))
#define SQR(x) ((x)*(x))
#define INTP( a, b ,c ) (a * ( b - c ) + c )
typedef float* floatpointer ;
#define FP_SWAP(a,b) { floatpointer temp=(a);(a)=(b);(b)=temp; }

inline unsigned fc(const unsigned cfa[2][2], unsigned row, unsigned col)
{
    return cfa[row & 1][col & 1];
}

void bayerborder_demosaic(int winw, int winh, int lborders, const float * const *rawData, float **red, float **green, float **blue, const unsigned cfarray[2][2])
{
    int bord = lborders;
    int width = winw;
    int height = winh;

    for (int i = 0; i < height; i++) {

        float sum[6];

        for (int j = 0; j < bord; j++) { //first few columns
            for (int c = 0; c < 6; c++) {
                sum[c] = 0;
            }

            for (int i1 = i - 1; i1 < i + 2; i1++)
                for (int j1 = j - 1; j1 < j + 2; j1++) {
                    if ((i1 > -1) && (i1 < height) && (j1 > -1)) {
                        int c = fc(cfarray, i1, j1);
                        sum[c] += rawData[i1][j1];
                        sum[c + 3]++;
                    }
                }

            int c = fc(cfarray, i, j);

            if (c == 1) {
                red[i][j] = sum[0] / sum[3];
                green[i][j] = rawData[i][j];
                blue[i][j] = sum[2] / sum[5];
            } else {
                green[i][j] = sum[1] / sum[4];

                if (c == 0) {
                    red[i][j] = rawData[i][j];
                    blue[i][j] = sum[2] / sum[5];
                } else {
                    red[i][j] = sum[0] / sum[3];
                    blue[i][j] = rawData[i][j];
                }
            }
        }//j

        for (int j = width - bord; j < width; j++) { //last few columns
            for (int c = 0; c < 6; c++) {
                sum[c] = 0;
            }

            for (int i1 = i - 1; i1 < i + 2; i1++)
                for (int j1 = j - 1; j1 < j + 2; j1++) {
                    if ((i1 > -1) && (i1 < height ) && (j1 < width)) {
                        int c = fc(cfarray, i1, j1);
                        sum[c] += rawData[i1][j1];
                        sum[c + 3]++;
                    }
                }

            int c = fc(cfarray, i, j);

            if (c == 1) {
                red[i][j] = sum[0] / sum[3];
                green[i][j] = rawData[i][j];
                blue[i][j] = sum[2] / sum[5];
            } else {
                green[i][j] = sum[1] / sum[4];

                if (c == 0) {
                    red[i][j] = rawData[i][j];
                    blue[i][j] = sum[2] / sum[5];
                } else {
                    red[i][j] = sum[0] / sum[3];
                    blue[i][j] = rawData[i][j];
                }
            }
        }//j
    }//i

    for (int i = 0; i < bord; i++) {

        float sum[6];

        for (int j = bord; j < width - bord; j++) { //first few rows
            for (int c = 0; c < 6; c++) {
                sum[c] = 0;
            }

            for (int i1 = i - 1; i1 < i + 2; i1++)
                for (int j1 = j - 1; j1 < j + 2; j1++) {
                    if ((i1 > -1) && (i1 < height) && (j1 > -1)) {
                        int c = fc(cfarray, i1, j1);
                        sum[c] += rawData[i1][j1];
                        sum[c + 3]++;
                    }
                }

            int c = fc(cfarray, i, j);

            if (c == 1) {
                red[i][j] = sum[0] / sum[3];
                green[i][j] = rawData[i][j];
                blue[i][j] = sum[2] / sum[5];
            } else {
                green[i][j] = sum[1] / sum[4];

                if (c == 0) {
                    red[i][j] = rawData[i][j];
                    blue[i][j] = sum[2] / sum[5];
                } else {
                    red[i][j] = sum[0] / sum[3];
                    blue[i][j] = rawData[i][j];
                }
            }
        }//j
    }

    for (int i = height - bord; i < height; i++) {

        float sum[6];

        for (int j = bord; j < width - bord; j++) { //last few rows
            for (int c = 0; c < 6; c++) {
                sum[c] = 0;
            }

            for (int i1 = i - 1; i1 < i + 2; i1++)
                for (int j1 = j - 1; j1 < j + 2; j1++) {
                    if ((i1 > -1) && (i1 < height) && (j1 < width)) {
                        int c = fc(cfarray, i1, j1);
                        sum[c] += rawData[i1][j1];
                        sum[c + 3]++;
                    }
                }

            int c = fc(cfarray, i, j);

            if (c == 1) {
                red[i][j] = sum[0] / sum[3];
                green[i][j] = rawData[i][j];
                blue[i][j] = sum[2] / sum[5];
            } else {
                green[i][j] = sum[1] / sum[4];

                if (c == 0) {
                    red[i][j] = rawData[i][j];
                    blue[i][j] = sum[2] / sum[5];
                } else {
                    red[i][j] = sum[0] / sum[3];
                    blue[i][j] = rawData[i][j];
                }
            }
        }//j
    }

    return;
}

/*
* RATIO CORRECTED DEMOSAICING
* Luis Sanz Rodriguez (luis.sanz.rodriguez(at)gmail(dot)com)
*
* Release 2.3 @ 171125
*
* Original code from https://github.com/LuisSR/RCD-Demosaicing
* Licensed under the GNU GPL version 3
*/

// Changed/Adapted for MLVApp by masc4ii
// Tiled version by Ingo Weyrich (heckflosse67@gmx.de)
// Luis Sanz Rodriguez significantly optimised the v 2.3 code and simplified the directional
// coefficients in an exact, shorter and more performant formula.
// In cooperation with Hanno Schwalm (hanno@schwalm-bremen.de) and Luis Sanz Rodriguez this has been tuned for performance.
void rcd_demosaic(rcdinfo_t *inputdata)
{
    float ** restrict rawData = inputdata->rawData;    /* holds preprocessed pixel values, rawData[i][j] corresponds to the ith row and jth column */
    float ** restrict red = inputdata->red;        /* the interpolated red plane */
    float ** restrict green = inputdata->green;      /* the interpolated green plane */
    float ** restrict blue = inputdata->blue;       /* the interpolated blue plane */
    int width = inputdata->winw; int height = inputdata->winh;

    int chunkSize = inputdata->chunkSize; //default

    const unsigned cfarray[2][2] = {{0,1},{1,2}};
    
    static const int tileBorder = 9; // avoid tile-overlap errors
    static const int rcdBorder = 9;
    static const int tileSize = 194;
    static const int tileSizeN = tileSize - 2 * tileBorder;
    const int numTh = height / (tileSizeN) + ((height % (tileSizeN)) ? 1 : 0);
    const int numTw = width / (tileSizeN) + ((width % (tileSizeN)) ? 1 : 0);
    static const int w1 = tileSize, w2 = 2 * tileSize, w3 = 3 * tileSize, w4 = 4 * tileSize;
    //Tolerance to avoid dividing by zero
    static const float eps = 1e-5f;
    static const float epssq = 1e-10f;
    static const float scale = 65536.f;

{
    float *const cfa = (float*) calloc(tileSize * tileSize, sizeof *cfa);
    float (*const rgb)[tileSize * tileSize] = (float (*)[tileSize * tileSize])malloc(3 * sizeof *rgb);
    float *const VH_Dir = (float*) calloc(tileSize * tileSize, sizeof *VH_Dir);
    float *const PQ_Dir = (float*) calloc(tileSize * tileSize / 2, sizeof *PQ_Dir);
    float *const lpf = PQ_Dir; // reuse buffer, they don't overlap in usage
    float *const P_CDiff_Hpf = (float*) calloc(tileSize * tileSize / 2, sizeof *P_CDiff_Hpf);
    float *const Q_CDiff_Hpf = (float*) calloc(tileSize * tileSize / 2, sizeof *Q_CDiff_Hpf);

    {
#ifdef _OPENMP
        #pragma omp for schedule(dynamic, chunkSize) collapse(2) nowait
#endif
        for(int tr = 0; tr < numTh; ++tr) {
            for(int tc = 0; tc < numTw; ++tc) {
                const int rowStart = tr * tileSizeN;
                const int rowEnd = MIN(rowStart + tileSize, height);
                if(rowStart + rcdBorder == rowEnd - rcdBorder) {
                    continue;
                }
                const int colStart = tc * tileSizeN;
                const int colEnd = MIN(colStart + tileSize, width);
                if(colStart + rcdBorder == colEnd - rcdBorder) {
                    continue;
                }

                const int tileRows = MIN(rowEnd - rowStart, tileSize);
                const int tilecols = MIN(colEnd - colStart, tileSize);

                for (int row = rowStart; row < rowEnd; row++) {
                    const int c0 = fc(cfarray, row, colStart);
                    const int c1 = fc(cfarray, row, colStart + 1);
                    for (int col = colStart, indx = (row - rowStart) * tileSize; col < colEnd; ++col, ++indx) {
                        cfa[indx] = rgb[c0][indx] = rgb[c1][indx] = LIM01(rawData[row][col] / scale);
                    }
                }

                // Step 1: Find cardinal and diagonal interpolation directions
                float bufferV[3][tileSize - 8];

                // Step 1.1: Calculate the square of the vertical and horizontal color difference high pass filter
                for (int row = 3; row < MIN(tileRows - 3, 5); ++row) {
                    for (int col = 4, indx = row * tileSize + col; col < tilecols - 4; ++col, ++indx) {
                        bufferV[row - 3][col - 4] = SQR((cfa[indx - w3] - cfa[indx - w1] - cfa[indx + w1] + cfa[indx + w3]) - 3.f * (cfa[indx - w2] + cfa[indx + w2])  + 6.f * cfa[indx]);
                    }
                }

                // Step 1.2: Obtain the vertical and horizontal directional discrimination strength
                float bufferH[tileSize - 6] ALIGNED16;
                float* V0 = bufferV[0];
                float* V1 = bufferV[1];
                float* V2 = bufferV[2];
                for (int row = 4; row < tileRows - 4; ++row) {
                    for (int col = 3, indx = row * tileSize + col; col < tilecols - 3; ++col, ++indx) {
                        bufferH[col - 3] = SQR((cfa[indx -  3] - cfa[indx -  1] - cfa[indx +  1] + cfa[indx +  3]) - 3.f * (cfa[indx -  2] + cfa[indx +  2]) + 6.f * cfa[indx]);
                    }
                    for (int col = 4, indx = (row + 1) * tileSize + col; col < tilecols - 4; ++col, ++indx) {
                        V2[col - 4] = SQR((cfa[indx - w3] - cfa[indx - w1] - cfa[indx + w1] + cfa[indx + w3]) - 3.f * (cfa[indx - w2] + cfa[indx + w2])  + 6.f * cfa[indx]);
                    }
                    for (int col = 4, indx = row * tileSize + col; col < tilecols - 4; ++col, ++indx) {

                        float V_Stat = MAX(epssq, V0[col - 4] + V1[col - 4] + V2[col - 4]);
                        float H_Stat = MAX(epssq, bufferH[col -  4] + bufferH[col - 3] + bufferH[col -  2]);

                        VH_Dir[indx] = V_Stat / (V_Stat + H_Stat);
                    }
                    // rotate pointers from row0, row1, row2 to row1, row2, row0
                    FP_SWAP(V0, V2);
                    FP_SWAP(V0, V1);
                }

                // Step 2: Low pass filter incorporating green, red and blue local samples from the raw data
                for (int row = 2; row < tileRows - 2; ++row) {
                    for (int col = 2 + (fc(cfarray, row, 0) & 1), indx = row * tileSize + col, lpindx = indx / 2; col < tilecols - 2; col += 2, indx += 2, ++lpindx) {
                        lpf[lpindx] = cfa[indx] +
                                      0.5f * (cfa[indx - w1] + cfa[indx + w1] + cfa[indx - 1] + cfa[indx + 1]) +
                                      0.25f * (cfa[indx - w1 - 1] + cfa[indx - w1 + 1] + cfa[indx + w1 - 1] + cfa[indx + w1 + 1]);
                    }
                }

                // Step 3: Populate the green channel at blue and red CFA positions
                for (int row = 4; row < tileRows - 4; ++row) {
                    for (int col = 4 + (fc(cfarray, row, 0) & 1), indx = row * tileSize + col, lpindx = indx / 2; col < tilecols - 4; col += 2, indx += 2, ++lpindx) {
                        // Cardinal gradients
                        const float cfai = cfa[indx];
                        const float N_Grad = eps + (fabs(cfa[indx - w1] - cfa[indx + w1]) + fabs(cfai - cfa[indx - w2])) + (fabs(cfa[indx - w1] - cfa[indx - w3]) + fabs(cfa[indx - w2] - cfa[indx - w4]));
                        const float S_Grad = eps + (fabs(cfa[indx - w1] - cfa[indx + w1]) + fabs(cfai - cfa[indx + w2])) + (fabs(cfa[indx + w1] - cfa[indx + w3]) + fabs(cfa[indx + w2] - cfa[indx + w4]));
                        const float W_Grad = eps + (fabs(cfa[indx -  1] - cfa[indx +  1]) + fabs(cfai - cfa[indx -  2])) + (fabs(cfa[indx -  1] - cfa[indx -  3]) + fabs(cfa[indx -  2] - cfa[indx -  4]));
                        const float E_Grad = eps + (fabs(cfa[indx -  1] - cfa[indx +  1]) + fabs(cfai - cfa[indx +  2])) + (fabs(cfa[indx +  1] - cfa[indx +  3]) + fabs(cfa[indx +  2] - cfa[indx +  4]));

                        // Cardinal pixel estimations
                        const float lpfi = lpf[lpindx];
                        const float N_Est = cfa[indx - w1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx - w1]);
                        const float S_Est = cfa[indx + w1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx + w1]);
                        const float W_Est = cfa[indx -  1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx -  1]);
                        const float E_Est = cfa[indx +  1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx +  1]);

                        // Vertical and horizontal estimations
                        const float V_Est = (S_Grad * N_Est + N_Grad * S_Est) / (N_Grad + S_Grad);
                        const float H_Est = (W_Grad * E_Est + E_Grad * W_Est) / (E_Grad + W_Grad);

                        // G@B and G@R interpolation
                        // Refined vertical and horizontal local discrimination
                        const float VH_Central_Value = VH_Dir[indx];
                        const float VH_Neighbourhood_Value = 0.25f * ((VH_Dir[indx - w1 - 1] + VH_Dir[indx - w1 + 1]) + (VH_Dir[indx + w1 - 1] + VH_Dir[indx + w1 + 1]));

                        const float VH_Disc = fabs(0.5f - VH_Central_Value) < fabs(0.5f - VH_Neighbourhood_Value) ? VH_Neighbourhood_Value : VH_Central_Value;
                        rgb[1][indx] = INTP(VH_Disc, H_Est, V_Est);
                    }
                }

                /**
                * STEP 4: Populate the red and blue channels
                */

                // Step 4.0: Calculate the square of the P/Q diagonals color difference high pass filter
                for (int row = 3; row < tileRows - 3; ++row) {
                    for (int col = 3, indx = row * tileSize + col, indx2 = indx / 2; col < tilecols - 3; col+=2, indx+=2, indx2++ ) {
                        P_CDiff_Hpf[indx2] = SQR((cfa[indx - w3 - 3] - cfa[indx - w1 - 1] - cfa[indx + w1 + 1] + cfa[indx + w3 + 3]) - 3.f * (cfa[indx - w2 - 2] + cfa[indx + w2 + 2]) + 6.f * cfa[indx]);
                        Q_CDiff_Hpf[indx2] = SQR((cfa[indx - w3 + 3] - cfa[indx - w1 + 1] - cfa[indx + w1 - 1] + cfa[indx + w3 - 3]) - 3.f * (cfa[indx - w2 + 2] + cfa[indx + w2 - 2]) + 6.f * cfa[indx]);
                    }
                }

                // Step 4.1: Obtain the P/Q diagonals directional discrimination strength
                for (int row = 4; row < tileRows - 4; ++row) {
                    for (int col = 4 + (fc(cfarray, row, 0) & 1), indx = row * tileSize + col, indx2 = indx / 2, indx3 = (indx - w1 - 1) / 2, indx4 = (indx + w1 - 1) / 2; col < tilecols - 4; col += 2, indx += 2, indx2++, indx3++, indx4++ ) {
                        float P_Stat = MAX(epssq, P_CDiff_Hpf[indx3] + P_CDiff_Hpf[indx2] + P_CDiff_Hpf[indx4 + 1]);
                        float Q_Stat = MAX(epssq, Q_CDiff_Hpf[indx3 + 1] + Q_CDiff_Hpf[indx2] + Q_CDiff_Hpf[indx4]);
                        PQ_Dir[indx2] = P_Stat / (P_Stat + Q_Stat);
                    }
                }

                // Step 4.2: Populate the red and blue channels at blue and red CFA positions
                for (int row = 4; row < tileRows - 4; ++row) {
                    for (int col = 4 + (fc(cfarray, row, 0) & 1), indx = row * tileSize + col, c = 2 - fc(cfarray, row, col), pqindx = indx / 2, pqindx2 = (indx - w1 - 1) / 2, pqindx3 = (indx + w1 - 1) / 2; col < tilecols - 4; col += 2, indx += 2, ++pqindx, ++pqindx2, ++pqindx3) {

                        // Refined P/Q diagonal local discrimination
                        float PQ_Central_Value   = PQ_Dir[pqindx];
                        float PQ_Neighbourhood_Value = 0.25f * (PQ_Dir[pqindx2] + PQ_Dir[pqindx2 + 1] + PQ_Dir[pqindx3] + PQ_Dir[pqindx3 + 1]);

                        float PQ_Disc = (fabs(0.5f - PQ_Central_Value) < fabs(0.5f - PQ_Neighbourhood_Value)) ? PQ_Neighbourhood_Value : PQ_Central_Value;

                        // Diagonal gradients
                        float NW_Grad = eps + fabs(rgb[c][indx - w1 - 1] - rgb[c][indx + w1 + 1]) + fabs(rgb[c][indx - w1 - 1] - rgb[c][indx - w3 - 3]) + fabs(rgb[1][indx] - rgb[1][indx - w2 - 2]);
                        float NE_Grad = eps + fabs(rgb[c][indx - w1 + 1] - rgb[c][indx + w1 - 1]) + fabs(rgb[c][indx - w1 + 1] - rgb[c][indx - w3 + 3]) + fabs(rgb[1][indx] - rgb[1][indx - w2 + 2]);
                        float SW_Grad = eps + fabs(rgb[c][indx - w1 + 1] - rgb[c][indx + w1 - 1]) + fabs(rgb[c][indx + w1 - 1] - rgb[c][indx + w3 - 3]) + fabs(rgb[1][indx] - rgb[1][indx + w2 - 2]);
                        float SE_Grad = eps + fabs(rgb[c][indx - w1 - 1] - rgb[c][indx + w1 + 1]) + fabs(rgb[c][indx + w1 + 1] - rgb[c][indx + w3 + 3]) + fabs(rgb[1][indx] - rgb[1][indx + w2 + 2]);

                        // Diagonal colour differences
                        float NW_Est = rgb[c][indx - w1 - 1] - rgb[1][indx - w1 - 1];
                        float NE_Est = rgb[c][indx - w1 + 1] - rgb[1][indx - w1 + 1];
                        float SW_Est = rgb[c][indx + w1 - 1] - rgb[1][indx + w1 - 1];
                        float SE_Est = rgb[c][indx + w1 + 1] - rgb[1][indx + w1 + 1];

                        // P/Q estimations
                        float P_Est = (NW_Grad * SE_Est + SE_Grad * NW_Est) / (NW_Grad + SE_Grad);
                        float Q_Est = (NE_Grad * SW_Est + SW_Grad * NE_Est) / (NE_Grad + SW_Grad);

                        // R@B and B@R interpolation
                        rgb[c][indx] = rgb[1][indx] + INTP(PQ_Disc, Q_Est, P_Est);
                    }
                }

                // Step 4.3: Populate the red and blue channels at green CFA positions
                for (int row = 4; row < tileRows - 4; ++row) {
                    for (int col = 4 + (fc(cfarray, row, 1) & 1), indx = row * tileSize + col; col < tilecols - 4; col += 2, indx += 2) {

                        // Refined vertical and horizontal local discrimination
                        float VH_Central_Value = VH_Dir[indx];
                        float VH_Neighbourhood_Value = 0.25f * ((VH_Dir[indx - w1 - 1] + VH_Dir[indx - w1 + 1]) + (VH_Dir[indx + w1 - 1] + VH_Dir[indx + w1 + 1]));

                        float VH_Disc = (fabs(0.5f - VH_Central_Value) < fabs(0.5f - VH_Neighbourhood_Value)) ? VH_Neighbourhood_Value : VH_Central_Value;
                        float rgb1 = rgb[1][indx];
                        float N1 = eps + fabs(rgb1 - rgb[1][indx - w2]);
                        float S1 = eps + fabs(rgb1 - rgb[1][indx + w2]);
                        float W1 = eps + fabs(rgb1 - rgb[1][indx -  2]);
                        float E1 = eps + fabs(rgb1 - rgb[1][indx +  2]);

                        float rgb1mw1 = rgb[1][indx - w1];
                        float rgb1pw1 = rgb[1][indx + w1];
                        float rgb1m1 = rgb[1][indx - 1];
                        float rgb1p1 = rgb[1][indx + 1];
                        for (int c = 0; c <= 2; c += 2) {
                            // Cardinal gradients
                            float SNabs = fabs(rgb[c][indx - w1] - rgb[c][indx + w1]);
                            float EWabs = fabs(rgb[c][indx -  1] - rgb[c][indx +  1]);
                            float N_Grad = N1 + SNabs + fabs(rgb[c][indx - w1] - rgb[c][indx - w3]);
                            float S_Grad = S1 + SNabs + fabs(rgb[c][indx + w1] - rgb[c][indx + w3]);
                            float W_Grad = W1 + EWabs + fabs(rgb[c][indx -  1] - rgb[c][indx -  3]);
                            float E_Grad = E1 + EWabs + fabs(rgb[c][indx +  1] - rgb[c][indx +  3]);

                            // Cardinal colour differences
                            float N_Est = rgb[c][indx - w1] - rgb1mw1;
                            float S_Est = rgb[c][indx + w1] - rgb1pw1;
                            float W_Est = rgb[c][indx -  1] - rgb1m1;
                            float E_Est = rgb[c][indx +  1] - rgb1p1;

                            // Vertical and horizontal estimations
                            float V_Est = (N_Grad * S_Est + S_Grad * N_Est) / (N_Grad + S_Grad);
                            float H_Est = (E_Grad * W_Est + W_Grad * E_Est) / (E_Grad + W_Grad);

                            // R@G and B@G interpolation
                            rgb[c][indx] = rgb1 + INTP(VH_Disc, H_Est, V_Est);
                        }
                    }
                }

                // For the outermost tiles in all directions we can use a smaller border margin
                const int firstVertical = rowStart + ((tr == 0) ? rcdBorder : tileBorder);
                const int lastVertical = rowEnd - ((tr == numTh - 1) ? rcdBorder : tileBorder);
                const int firstHorizontal = colStart + ((tc == 0) ? rcdBorder : tileBorder);
                const int lastHorizontal =  colEnd - ((tc == numTw - 1) ? rcdBorder : tileBorder);
                for (int row = firstVertical; row < lastVertical; ++row) {
                    for (int col = firstHorizontal; col < lastHorizontal; ++col) {
                        int idx = (row - rowStart) * tileSize + col - colStart ;
                        red[row][col] = MAX(0.f, rgb[0][idx] * scale);
                        green[row][col] = MAX(0.f, rgb[1][idx] * scale);
                        blue[row][col] = MAX(0.f, rgb[2][idx] * scale);
                    }
                }
            }
        }
    }
    free(cfa);
    free(rgb);
    free(VH_Dir);
    free(PQ_Dir);
    free(P_CDiff_Hpf);
    free(Q_CDiff_Hpf);
}
    bayerborder_demosaic(width, height, rcdBorder, rawData, red, green, blue, cfarray);

    return;
}

