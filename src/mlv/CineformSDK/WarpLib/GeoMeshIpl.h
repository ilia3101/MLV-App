/*! @file GeoMeshIpl.h

*  @brief Mesh tools
*
*  @version 1.0.0
*  (C) Copyright 2017 GoPro Inc (http://gopro.com/).
*
*  Licensed under either:
*  - Apache License, Version 2.0, http://www.apache.org/licenses/LICENSE-2.0  
*  - MIT license, http://opensource.org/licenses/MIT
*  at your option.
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*/

#ifndef GEOMESHIPL_H
#define GEOMESHIPL_H

#include <iostream>
#include <iomanip>
#include <stdexcept>

#include <stdio.h>

#include "opencv/cv.h"
#include "opencv/highgui.h"

#include "GeoMesh.h"
#include "GeoMesh_priv.h"
#include "sse_types.h"

#undef USE_SSE

#define MININT(x,y) ((x) < (y) ? (x) : (y))
#define MAXINT(x,y) ((x) > (y) ? (x) : (y))


// Implementation is in this header file beacuse (a) I didn't want to burden the
// main WarpLib library with an OpenCV dependency, and (b) I didn't want to
// create a second library just for the IplImage / OpenCV interface. This way,
// the developer simply needs to include this header and link against the
// main library and they get an Ipl interface to the library.

class GeoMeshIpl : public GeoMesh
{
    public:

    GeoMeshIpl()
    {
    }

    GeoMeshIpl(int rows, int cols)
        : GeoMesh(rows, cols)
    {
    }

    virtual ~GeoMeshIpl()
    {
    }

    void apply(IplImage *src, IplImage *dest, int row0, int row1)
    {
        geomesh_t *gm = (geomesh_t *)opaque;

        double r = 0.0;
        double c = 0.0;
        unsigned char *destptr;
        int col0 = 0;
        int col1 = gm->destwidth;
        int stride = src->widthStep;
        int *iptr = gm->cache + gm->destwidth * 3 * row0;

        for (int row = row0; row < row1; row++)
        {
            destptr = (unsigned char *)(dest->imageData) + row * dest->widthStep;

            for (int col = col0; col < col1; col++)
            {
                int srcidx = *iptr++;
                int xlever = *iptr++;
                int ylever = *iptr++;

                if (srcidx < 0)
                {
                    destptr += 3;
                    continue;
                }

    //#ifdef USE_SSE
    #if 0
    #if 1
                m128i r, g, b, w0, w1;
                m128i r1, g1, b1;
                m128i accr0, accr1, accg0, accg1, accb0, accb1;
                m128i acc0, acc1;
                m128i rgb0, rgb1;

                unsigned short w00 = (65535 - xlever) * (65535 - ylever);
                unsigned short w01 = (xlever)       * (65535 - ylever);
                unsigned short w10 = (65535 - xlever) * (ylever);
                unsigned short w11 = (xlever)       * (ylever);

                w0.sdata[0] = w00;
                w0.sdata[1] = w00;
                w0.sdata[2] = w00;
                w0.sdata[3] = w01;
                w0.sdata[4] = w01;
                w0.sdata[5] = w01;

                w1.sdata[0] = w10;
                w1.sdata[1] = w10;
                w1.sdata[2] = w10;
                w1.sdata[3] = w11;
                w1.sdata[4] = w11;
                w1.sdata[5] = w11;

                unsigned char *srcptr = (unsigned char *)(src->imageData) + srcidx;

                rgb0.m128 = _mm_loadu_si128((__m128i *)srcptr);
                acc0.m128 = _mm_mulhi_epu16(rgb0.m128, w0.m128);
                rgb1.m128 = _mm_loadu_si128((__m128i *)(srcptr + stride));
                acc1.m128 = _mm_mulhi_epu16(rgb1.m128, w1.m128);

                *destptr++ = (unsigned char)(acc0.cdata[0] + acc0.cdata[6]  + acc1.cdata[0] + acc1.cdata[6]);
                *destptr++ = (unsigned char)(acc0.cdata[2] + acc0.cdata[8]  + acc1.cdata[2] + acc1.cdata[8]);
                *destptr++ = (unsigned char)(acc0.cdata[4] + acc0.cdata[10] + acc1.cdata[4] + acc1.cdata[10]);
    #else
                m128i r, g, b, w;
                m128i r1, g1, b1, w1;
                m128i accr0, accr1, accg0, accg1, accb0, accb1;
                m128i accr, accg, accb;

                w.idata[0] = (256 - xlever) * (256 - ylever);
                w.idata[2] = (xlever) * (256 - ylever);
                w1.idata[0] = (256 - xlever) * (ylever);
                w1.idata[2] = (xlever) * (ylever);

                unsigned char *srcptr = (unsigned char *)(src->imageData) + srcidx;

                r.cdata[0] = *srcptr++;
                g.cdata[0] = *srcptr++;
                b.cdata[0] = *srcptr++;
                r.cdata[8] = *srcptr++;
                g.cdata[8] = *srcptr++;
                b.cdata[8] = *srcptr++;

                srcptr = (unsigned char *)(src->imageData) + srcidx + stride;

                r1.cdata[0] = *srcptr++;
                g1.cdata[0] = *srcptr++;
                b1.cdata[0] = *srcptr++;
                r1.cdata[8] = *srcptr++;
                g1.cdata[8] = *srcptr++;
                b1.cdata[8] = *srcptr++;

                accr0.m128 = _mm_mul_epu32(r.m128, w.m128);
                accg0.m128 = _mm_mul_epu32(g.m128, w.m128);
                accb0.m128 = _mm_mul_epu32(b.m128, w.m128);

                accr1.m128 = _mm_mul_epu32(r1.m128, w1.m128);
                accg1.m128 = _mm_mul_epu32(g1.m128, w1.m128);
                accb1.m128 = _mm_mul_epu32(b1.m128, w1.m128);

                accr.m128 = _mm_add_epi64(accr0.m128, accr1.m128);
                accg.m128 = _mm_add_epi64(accg0.m128, accg1.m128);
                accb.m128 = _mm_add_epi64(accb0.m128, accb1.m128);

                *destptr++ = (unsigned char)((accr.idata[0] + accr.idata[2]) >> 16);
                *destptr++ = (unsigned char)((accg.idata[0] + accg.idata[2]) >> 16);
                *destptr++ = (unsigned char)((accb.idata[0] + accb.idata[2]) >> 16);
    #endif
    #else
                unsigned char *srcptr = (unsigned char *)(src->imageData) + srcidx;

                unsigned char r00  = *srcptr++;
                unsigned char g00  = *srcptr++;
                unsigned char b00  = *srcptr++;
                unsigned char r01  = *srcptr++;
                unsigned char g01  = *srcptr++;
                unsigned char b01  = *srcptr++;

                srcptr = (unsigned char *)(src->imageData) + srcidx + stride;

                unsigned char r10  = *srcptr++;
                unsigned char g10  = *srcptr++;
                unsigned char b10  = *srcptr++;
                unsigned char r11  = *srcptr++;
                unsigned char g11  = *srcptr++;
                unsigned char b11  = *srcptr++;

                int w00 = (256 - xlever) * (256 - ylever);
                int w01 = (xlever) * (256 - ylever);
                int w10 = (256 - xlever) * (ylever);
                int w11 = (xlever) * (ylever);

                *destptr++ = (unsigned char)((r00 * w00 + r01 * w01 + r10 * w10 + r11 * w11) >> 16);
                *destptr++ = (unsigned char)((g00 * w00 + g01 * w01 + g10 * w10 + g11 * w11) >> 16);
                *destptr++ = (unsigned char)((b00 * w00 + b01 * w01 + b10 * w10 + b11 * w11) >> 16);
    #endif
            }
        }
    }

};

#endif /* GEOMESHIPL_H */

