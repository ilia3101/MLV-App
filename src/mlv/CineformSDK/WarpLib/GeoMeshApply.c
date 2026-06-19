/*! @file GeoMeshApply.c

*  @brief Mesh tools
*
*  @version 1.0.0
*
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
#include <stdio.h>

#include "GeoMesh.h"
#include "GeoMeshPrivate.h"
#include "sse_types.h"

int geomesh_apply_bilinear(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED);

    // right now, only support dest format == src format
    if (gm->srcformat != gm->destformat)
        return WARPLIB_ERROR_UNSUPPORTED_CONVERSION;

    if (gm->separable)
        return geomesh_apply_bilinear_separable(opaque, src, dest, row0, row1);

    switch(gm->srcformat)
    {
        case WARPLIB_FORMAT_2vuy:
            switch(gm->srcformat)
            {
                case WARPLIB_FORMAT_YUY2:
                    geomesh_apply_bilinear_2vuy_yuy2(opaque, src, dest, row0, row1);
                    break;
                case WARPLIB_FORMAT_2vuy:
                    geomesh_apply_bilinear_2vuy(opaque, src, dest, row0, row1);
                    break;
                default:
                    return WARPLIB_ERROR_UNSUPPORTED_FORMAT;
                    break;
             }
             break;
        case WARPLIB_FORMAT_YUY2:
            switch(gm->srcformat)
            {
                case WARPLIB_FORMAT_YUY2:
                    geomesh_apply_bilinear_yuy2(opaque, src, dest, row0, row1);
                    break;
                case WARPLIB_FORMAT_2vuy:
                    //geomesh_apply_bilinear_yuy2_2vuy(opaque, src, dest, row0, row1);
                    return WARPLIB_ERROR_UNSUPPORTED_FORMAT;
                    break;
                default:
                    return WARPLIB_ERROR_UNSUPPORTED_FORMAT;
                    break;
             }
             break;
        case WARPLIB_FORMAT_422YpCbCr8:
            geomesh_apply_bilinear_422YpCbCr8(opaque, src, dest, row0, row1);
            break;
        case WARPLIB_FORMAT_32BGRA:
            geomesh_apply_bilinear_32BGRA(opaque, src, dest, row0, row1);
            break;
        case WARPLIB_FORMAT_64ARGB:
            geomesh_apply_bilinear_64ARGB(opaque, src, dest, row0, row1);
            break;
        case WARPLIB_FORMAT_RG48:
            geomesh_apply_bilinear_RG48(opaque, src, dest, row0, row1);
            break;;
        case WARPLIB_FORMAT_W13A:
            geomesh_apply_bilinear_W13A(opaque, src, dest, row0, row1);
            break;
        case WARPLIB_FORMAT_WP13:
            geomesh_apply_bilinear_WP13(opaque, src, dest, row0, row1);
            break;
        default:
            return WARPLIB_ERROR_UNSUPPORTED_FORMAT;
            break;
    }

    return WARPLIB_SUCCESS;
}


int geomesh_apply_bilinear_separable(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1)
{

    return WARPLIB_SUCCESS;
}


// REVISIT: copied from below - WRONG IMPL
int geomesh_apply_bilinear_yuy2(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    unsigned char *destptr;
    int col0 = 0;
    int col1;
    int *iptr;
    int stride;
    int row, col;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED);

    col1 = gm->destwidth;
    iptr = gm->cache + gm->destwidth * gm->num_elements_allocated * row0;
    stride = gm->srcstride;

	if (row1 >= gm->destheight) row1 = gm->destheight;

    for (row = row0; row < row1; row++)
    {
        destptr = dest + row * gm->deststride;

		if (row >= gm->destheight - 1) stride = 0;

        for (col = col0; col < col1; col++)
        {
            int yidx   = *iptr++;
            int uvidx  = *iptr++;
            int xlever = *iptr++;
            int ylever = *iptr++;
            int alpha = 0;
            unsigned char *yptr = src + yidx;
            unsigned char *uvptr = src + uvidx;
            unsigned char y00, y01, y10, y11;
            unsigned char uv00, uv10;
            int           w00, w01, w10, w11;

            if(gm->backgroundfill)
                alpha = *iptr++;

            if (yidx < 0)
            {
                *destptr++ = 0;
                *destptr++ = 128;
                continue;
            }

#ifdef USE_SSE
            m128i y, uv, w0, w1;
            m128i acc0, acc1, acc2;

            w0.idata[0] = (256 - xlever) * (256 - ylever);
            w0.idata[2] = (xlever) * (256 - ylever);
            w1.idata[0] = (256 - xlever) * (ylever);
            w1.idata[2] = (xlever) * (ylever);

            y.idata[0] = *yptr;
            y.idata[2] = *(yptr + stride);
            acc0.m128 = _mm_mul_epu32(y.m128, w0.m128);

            y.idata[0] = *(yptr + 2);
            y.idata[2] = *(yptr + stride + 2);
            acc1.m128 = _mm_mul_epu32(y.m128, w1.m128);

            acc2.m128 = _mm_add_epi64(acc0.m128, acc1.m128);

            *destptr++ = (unsigned char)((acc2.idata[0] + acc2.idata[2]) >> 16);

            uv.idata[0] = *uvptr;
            uv.idata[2] = *(uvptr + 4);
            acc0.m128 = _mm_mul_epu32(uv.m128, w0.m128);

            uv.idata[0] = *(uvptr + stride);
            uv.idata[2] = *(uvptr + stride + 4);
            acc1.m128 = _mm_mul_epu32(uv.m128, w1.m128);

            acc2.m128 = _mm_add_epi64(acc0.m128, acc1.m128);

            *destptr++ = (unsigned char)((acc2.idata[0] + acc2.idata[2]) >> 16);
#else
            y00  = *yptr;
            y01  = *(yptr + 2);
            y10  = *(yptr + stride);
            y11  = *(yptr + stride + 2);

            // TODO: 4:2:2 UV horizontal bilinear interpolation
            uv00 = *uvptr;
           // uv01 = *(uvptr + 4);
            uv10 = *(uvptr + stride);
           // uv11 = *(uvptr + stride + 4);

            w00 = (256 - xlever) * (256 - ylever);
            w01 = (xlever) * (256 - ylever);
            w10 = (256 - xlever) * (ylever);
            w11 = (xlever) * (ylever);

            if(alpha > 0)
            {
                alpha *= 32;
                if(alpha > 200) alpha = 200;

                *destptr = ((unsigned int)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256-alpha) + *(destptr-2)*alpha + 128 )>>8;
                destptr++;
                *destptr = ((unsigned int)((uv00 * (256 - ylever) +  uv10 * ylever) >> 8)*(256-alpha) + *(destptr-4)*alpha + 128)>>8;
                destptr++;
            }
            else
            {
                *destptr++ = (unsigned char)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16);
                *destptr++ = (unsigned char)((uv00 * (256 - ylever) +  uv10 * ylever) >> 8);
            }

#endif
        }
    }

    return WARPLIB_SUCCESS;
}


int geomesh_apply_bilinear_2vuy(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    unsigned char *destptr;
    int col0 = 0;
    int col1;
    int *iptr;
    int stride;
    int row, col;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED);

    col1 = gm->destwidth;
    iptr = gm->cache + gm->destwidth * gm->num_elements_allocated * row0;
    stride = gm->srcstride;

	if (row1 >= gm->destheight) row1 = gm->destheight;

    for (row = row0; row < row1; row++)
    {
        destptr = dest + row * gm->deststride;

		if (row >= gm->destheight - 1) stride = 0;

        for (col = col0; col < col1; col++)
        {
            int yidx   = *iptr++;
            int uvidx  = *iptr++;
            int xlever = *iptr++;
            int ylever = *iptr++;
            unsigned char *yptr = src + yidx;
            unsigned char *uvptr = src + uvidx;
            unsigned char y00, y01, y10, y11;
            unsigned char uv00, uv01, uv10, uv11;
            int           w00, w01, w10, w11;

            if (yidx < 0)
            {
                *destptr++ = 128;
                *destptr++ = 0;
                continue;
            }

            y00  = *(yptr);
            y01  = *(yptr + 2);
            y10  = *(yptr + stride);
            y11  = *(yptr + stride + 2);

            uv00 = *(uvptr);
            uv01 = *(uvptr + 4);
            uv10 = *(uvptr + stride);
            uv11 = *(uvptr + stride + 4);

            w00 = (256 - xlever) * (256 - ylever);
            w01 = (xlever) * (256 - ylever);
            w10 = (256 - xlever) * (ylever);
            w11 = (xlever) * (ylever);

            *destptr++ = (unsigned char)((uv00 * w00 + uv01 * w01 + uv10 * w10 + uv11 * w11) >> 16);
            *destptr++ = (unsigned char)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16);
        }
    }

    return WARPLIB_SUCCESS;
}


int geomesh_apply_bilinear_2vuy_yuy2(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    unsigned char *destptr;
    int col0 = 0;
    int col1;
    int *iptr;
    int stride;
    int row, col;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED);

    col1 = gm->destwidth;
    iptr = gm->cache + gm->destwidth * gm->num_elements_allocated * row0;
    stride = gm->srcstride;

	if (row1 >= gm->destheight) row1 = gm->destheight;

    for (row = row0; row < row1; row++)
    {
        destptr = dest + row * gm->deststride;

		if (row >= gm->destheight - 1) stride = 0;

        for (col = col0; col < col1; col++)
        {
            int yidx   = *iptr++;
            int uvidx  = *iptr++;
            int xlever = *iptr++;
            int ylever = *iptr++;
            unsigned char *yptr = src + yidx;
            unsigned char *uvptr = src + uvidx;
            unsigned char y00, y01, y10, y11;
            unsigned char uv00, uv01, uv10, uv11;
            int           w00, w01, w10, w11;

            if (yidx < 0)
            {
                *destptr++ = 0;
                *destptr++ = 128;
                continue;
            }

            y00  = *(yptr);
            y01  = *(yptr + 2);
            y10  = *(yptr + stride);
            y11  = *(yptr + stride + 2);

            uv00 = *uvptr;
            uv01 = *(uvptr + 4);
            uv10 = *(uvptr + stride);
            uv11 = *(uvptr + stride + 4);

            w00 = (256 - xlever) * (256 - ylever);
            w01 = (xlever) * (256 - ylever);
            w10 = (256 - xlever) * (ylever);
            w11 = (xlever) * (ylever);

            *destptr++ = (unsigned char)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16);
            *destptr++ = (unsigned char)((uv00 * w00 + uv01 * w01 + uv10 * w10 + uv11 * w11) >> 16);
        }
    }

    return WARPLIB_SUCCESS;
}


int geomesh_apply_bilinear_422YpCbCr8(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1)
{
    //geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED);

    return WARPLIB_SUCCESS;
}

int geomesh_apply_bilinear_32BGRA(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    int col0 = 0;
    int col1;
    int *iptr;
    int stride;
    int row, col;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED);

    col1 = gm->destwidth;
    iptr = gm->cache + gm->destwidth * gm->num_elements_allocated * row0;
    stride = gm->srcstride;

	if (row1 >= gm->destheight) row1 = gm->destheight;

    for (row = row0; row < row1; row++)
    {
        unsigned char *oT = dest + row * gm->deststride;

		if (row >= gm->destheight - 1) stride = 0;

        for (col = col0; col < col1; col++)
        {
            int yidx   = *iptr++;
            int xlever = *iptr++;
            int ylever = *iptr++;
            int alpha = 0;
            unsigned char *iT = src + yidx;
            unsigned char y00, y01, y10, y11;
            int           w00, w01, w10, w11;

            if(gm->backgroundfill)
                alpha = *iptr++;

            if (yidx < 0)
            {
                *oT++ = 0;
                *oT++ = 0;
                *oT++ = 0;
                *oT++ = 255;
                continue;
            }

            if(ylever == 0)
            {
                w00 = (256 - xlever);
                w01 = (xlever);

                if(alpha > 0 && col)
                {
                    alpha *= 32;
                    if(alpha > 200) alpha = 200;

                    y00  = *(iT);
                    y01  = *(iT + 4);
                    *oT = ((unsigned char)((y00 * w00 + y01 * w01) >> 8)*(256-alpha) + *(oT-4)*alpha + 128 )>>8;
                    oT++;

                    iT++;
                    y00  = *(iT);
                    y01  = *(iT + 4);
                    *oT = ((unsigned char)((y00 * w00 + y01 * w01) >> 8)*(256-alpha) + *(oT-4)*alpha + 128 )>>8;
                    oT++;

                    iT++;
                    y00  = *(iT);
                    y01  = *(iT + 4);
                    *oT = ((unsigned char)((y00 * w00 + y01 * w01) >> 8)*(256-alpha) + *(oT-4)*alpha + 128 )>>8;
                    oT++;

                    iT++;
                    y00  = *(iT);
                    y01  = *(iT + 4);
                    *oT = ((unsigned char)((y00 * w00 + y01 * w01) >> 8)*(256-alpha) + *(oT-4)*alpha + 128 )>>8;
                    oT++;
                }
                else
                {
                    y00  = *(iT);
                    y01  = *(iT + 4);
                    *oT++ = (unsigned char)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00  = *(iT);
                    y01  = *(iT + 4);
                    *oT++ = (unsigned char)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00  = *(iT);
                    y01  = *(iT + 4);
                    *oT++ = (unsigned char)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00  = *(iT);
                    y01  = *(iT + 4);
                   *oT++ = (unsigned char)((y00 * w00 + y01 * w01) >> 8);
                }
            }
            else
            {
                w00 = (256 - xlever) * (256 - ylever);
                w01 = (xlever) * (256 - ylever);
                w10 = (256 - xlever) * (ylever);
                w11 = (xlever) * (ylever);


                if (alpha == 0 || col == 0)
                {
                    oT[0] = (unsigned char)((iT[0] * w00 + iT[4] * w01 + iT[stride + 0] * w10 + iT[stride + 4] * w11) >> 16);
                    oT[1] = (unsigned char)((iT[1] * w00 + iT[5] * w01 + iT[stride + 1] * w10 + iT[stride + 5] * w11) >> 16);
                    oT[2] = (unsigned char)((iT[2] * w00 + iT[6] * w01 + iT[stride + 2] * w10 + iT[stride + 6] * w11) >> 16);
                    oT[3] = (unsigned char)((iT[3] * w00 + iT[7] * w01 + iT[stride + 3] * w10 + iT[stride + 7] * w11) >> 16);

                    oT += 4;
                    iT += 4;
                }
                else
                {
                    alpha *= 32;
                    if(alpha > 200) alpha = 200;

                    y00  = *(iT);
                    y01  = *(iT + 4);
                    y10  = *(iT + stride);
                    y11  = *(iT + stride + 4);
                    oT[0] = ((unsigned char)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - 4)*alpha + 128) >> 8;

                    iT++;
                    y00  = *(iT);
                    y01  = *(iT + 4);
                    y10  = *(iT + stride);
                    y11  = *(iT + stride + 4);
                    oT[1] = ((unsigned char)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - 4)*alpha + 128) >> 8;

                    iT++;
                    y00  = *(iT);
                    y01  = *(iT + 4);
                    y10  = *(iT + stride);
                    y11  = *(iT + stride + 4);
                    oT[2] = ((unsigned char)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - 4)*alpha + 128) >> 8;

                    iT++;
                    y00  = *(iT);
                    y01  = *(iT + 4);
                    y10  = *(iT + stride);
                    y11  = *(iT + stride + 4);
                    oT[3] = ((unsigned char)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - 4)*alpha + 128) >> 8;

                    oT += 4;
                }
            }
        }
    }


    return WARPLIB_SUCCESS;
}

int geomesh_apply_bilinear_64ARGB(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    int col0 = 0;
    int col1;
    int nxtln;
    int *iptr;
    int row, col;
    const int nxtpix = 4;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED);

    col1 = gm->destwidth;
    iptr = gm->cache + gm->destwidth * gm->num_elements_allocated * row0;
    nxtln = gm->srcstride >> 1;


	if (row1 >= gm->destheight) row1 = gm->destheight;

    for (row = row0; row < row1; row++)
    {
        unsigned char *oTchar = dest + row * gm->deststride;
        unsigned short *oT = (unsigned short *)oTchar;

		if (row >= gm->destheight - 1) nxtln = 0;

        for (col = col0; col < col1; col++)
        {
            int yidx = *iptr++;
            int xlever = *iptr++;
            int ylever = *iptr++;
            int alpha = 0;
            unsigned char *iTchar = src + yidx;
            unsigned short *iT = (unsigned short *)iTchar;
            unsigned short y00, y01, y10, y11;
            int           w00, w01, w10, w11;

            if (gm->backgroundfill)
                alpha = *iptr++;

            if (yidx < 0)
            {
                *oT++ = 0;
                *oT++ = 0;
                *oT++ = 0;
                *oT++ = 65535;
                continue;
            }


            if (ylever == 0)
            {
                w00 = (256 - xlever);
                w01 = (xlever);

                if (alpha > 0 && col)
                {
                    alpha *= 32;
                    if (alpha > 200) alpha = 200;

                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((unsigned short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((unsigned short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((unsigned short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((unsigned short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;
                }
                else
                {
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (unsigned short)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (unsigned short)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (unsigned short)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (unsigned short)((y00 * w00 + y01 * w01) >> 8);
                }
            }
            else
            {
                w00 = (256 - xlever) * (256 - ylever);
                w01 = (xlever)* (256 - ylever);
                w10 = (256 - xlever) * (ylever);
                w11 = (xlever)* (ylever);


                if (alpha == 0 || col == 0)
                {
                    oT[0] = (unsigned short)((iT[0] * w00 + iT[nxtpix + 0] * w01 + iT[nxtln + 0] * w10 + iT[nxtln + nxtpix + 0] * w11) >> 16);
                    oT[1] = (unsigned short)((iT[1] * w00 + iT[nxtpix + 1] * w01 + iT[nxtln + 1] * w10 + iT[nxtln + nxtpix + 1] * w11) >> 16);
                    oT[2] = (unsigned short)((iT[2] * w00 + iT[nxtpix + 2] * w01 + iT[nxtln + 2] * w10 + iT[nxtln + nxtpix + 2] * w11) >> 16);
                    oT[3] = (unsigned short)((iT[3] * w00 + iT[nxtpix + 3] * w01 + iT[nxtln + 3] * w10 + iT[nxtln + nxtpix + 3] * w11) >> 16);
                    oT += nxtpix;
                    iT += nxtpix;
                }
                else
                {
                    alpha *= 32;
                    if (alpha > 200) alpha = 200;

                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[0] = ((unsigned short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[1] = ((unsigned short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[2] = ((unsigned short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[3] = ((unsigned short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    oT += nxtpix;
                }
            }
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_apply_bilinear_RG48(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    int col0 = 0;
    int col1;
    int nxtln;
    int *iptr;
    int row, col;
    const int nxtpix = 3;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED);

    col1 = gm->destwidth;
    iptr = gm->cache + gm->destwidth * gm->num_elements_allocated * row0;
    nxtln = gm->srcstride>>1;

	if (row1 >= gm->destheight) row1 = gm->destheight;

    for (row = row0; row < row1; row++)
    {
        unsigned char *oTchar = dest + row * gm->deststride;
        unsigned short *oT = (unsigned short *)oTchar;

		if (row >= gm->destheight - 1) nxtln = 0;

        for (col = col0; col < col1; col++)
        {
            int yidx = *iptr++;
            int xlever = *iptr++;
            int ylever = *iptr++;
            int alpha = 0;
            unsigned char *iTchar = src + yidx;
            unsigned short *iT = (unsigned short *)iTchar;
            unsigned short y00, y01, y10, y11;
            int           w00, w01, w10, w11;

            if (gm->backgroundfill)
                alpha = *iptr++;

            if (yidx < 0)
            {
                *oT++ = 0;
                *oT++ = 0;
                *oT++ = 0;
                continue;
            }


            if (ylever == 0)
            {
                w00 = (256 - xlever);
                w01 = (xlever);

                if (alpha > 0 && col)
                {
                    alpha *= 32;
                    if (alpha > 200) alpha = 200;

                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((unsigned short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((unsigned short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((unsigned short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;
                }
                else
                {
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (unsigned short)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (unsigned short)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (unsigned short)((y00 * w00 + y01 * w01) >> 8);
                }
            }
            else
            {
                w00 = (256 - xlever) * (256 - ylever);
                w01 = (xlever)* (256 - ylever);
                w10 = (256 - xlever) * (ylever);
                w11 = (xlever)* (ylever);


                if (alpha == 0 || col == 0)
                {
                    oT[0] = (unsigned short)((iT[0] * w00 + iT[nxtpix + 0] * w01 + iT[nxtln + 0] * w10 + iT[nxtln + nxtpix + 0] * w11) >> 16);
                    oT[1] = (unsigned short)((iT[1] * w00 + iT[nxtpix + 1] * w01 + iT[nxtln + 1] * w10 + iT[nxtln + nxtpix + 1] * w11) >> 16);
                    oT[2] = (unsigned short)((iT[2] * w00 + iT[nxtpix + 2] * w01 + iT[nxtln + 2] * w10 + iT[nxtln + nxtpix + 2] * w11) >> 16);
                    oT += nxtpix;
                    iT += nxtpix;
                }
                else
                {
                    alpha *= 32;
                    if (alpha > 200) alpha = 200;

                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[0] = ((unsigned short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[1] = ((unsigned short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[2] = ((unsigned short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    oT += nxtpix;
                }
            }
        }
    }

    return WARPLIB_SUCCESS;
}


int geomesh_apply_bilinear_W13A(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    int col0 = 0;
    int col1;
    int nxtln;
    int *iptr;
    int row, col;
    const int nxtpix = 4;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED);

    col1 = gm->destwidth;
    iptr = gm->cache + gm->destwidth * gm->num_elements_allocated * row0;
    nxtln = gm->srcstride >> 1;

	if (row1 >= gm->destheight) row1 = gm->destheight;

    for (row = row0; row < row1; row++)
    {
        unsigned char *oTchar = dest + row * gm->deststride;
        short *oT = (short *)oTchar;

		if (row >= gm->destheight - 1) nxtln = 0;

        for (col = col0; col < col1; col++)
        {
            int yidx = *iptr++;
            int xlever = *iptr++;
            int ylever = *iptr++;
            int alpha = 0;
            unsigned char *iTchar = src + yidx;
            short *iT = (short *)iTchar;
            short y00, y01, y10, y11;
            int           w00, w01, w10, w11;

            if (gm->backgroundfill)
                alpha = *iptr++;

            if (yidx < 0)
            {
                *oT++ = 0;
                *oT++ = 0;
                *oT++ = 0;
                *oT++ = 8191;
                continue;
            }


            if (ylever == 0)
            {
                w00 = (256 - xlever);
                w01 = (xlever);

                if (alpha > 0 && col)
                {
                    alpha *= 32;
                    if (alpha > 200) alpha = 200;

                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;
                }
                else
                {
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (short)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (short)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (short)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (short)((y00 * w00 + y01 * w01) >> 8);
                }
            }
            else
            {
                w00 = (256 - xlever) * (256 - ylever);
                w01 = (xlever)* (256 - ylever);
                w10 = (256 - xlever) * (ylever);
                w11 = (xlever)* (ylever);


                if (alpha == 0 || col == 0)
                {
                    oT[0] = (short)((iT[0] * w00 + iT[nxtpix + 0] * w01 + iT[nxtln + 0] * w10 + iT[nxtln + nxtpix + 0] * w11) >> 16);
                    oT[1] = (short)((iT[1] * w00 + iT[nxtpix + 1] * w01 + iT[nxtln + 1] * w10 + iT[nxtln + nxtpix + 1] * w11) >> 16);
                    oT[2] = (short)((iT[2] * w00 + iT[nxtpix + 2] * w01 + iT[nxtln + 2] * w10 + iT[nxtln + nxtpix + 2] * w11) >> 16);
                    oT[3] = (short)((iT[3] * w00 + iT[nxtpix + 3] * w01 + iT[nxtln + 3] * w10 + iT[nxtln + nxtpix + 3] * w11) >> 16);
                    oT += nxtpix;
                    iT += nxtpix;
                }
                else
                {
                    alpha *= 32;
                    if (alpha > 200) alpha = 200;

                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[0] = ((short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[1] = ((short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[2] = ((short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[3] = ((short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    oT += nxtpix;
                }
            }
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_apply_bilinear_WP13(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    int col0 = 0;
    int col1;
    int nxtln;
    int *iptr;
    int row, col;
    const int nxtpix = 3;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED);

    col1 = gm->destwidth;
    iptr = gm->cache + gm->destwidth * gm->num_elements_allocated * row0;
    nxtln = gm->srcstride >> 1;
	
	if (row1 >= gm->destheight) row1 = gm->destheight;

    for (row = row0; row < row1; row++)
    {
        unsigned char *oTchar = dest + row * gm->deststride;
        short *oT = (short *)oTchar;

		if (row >= gm->destheight - 1) nxtln = 0;

		for (col = col0; col < col1; col++)
        {
            int yidx = *iptr++;
            int xlever = *iptr++;
            int ylever = *iptr++;
            int alpha = 0;
            unsigned char *iTchar = src + yidx;
            short *iT = (short *)iTchar;
            short y00, y01, y10, y11;
            int   w00, w01, w10, w11;

            if (gm->backgroundfill)
                alpha = *iptr++;

            if (yidx < 0)
            {
                *oT++ = 0;
                *oT++ = 0;
                *oT++ = 0;
                continue;
            }


            if (ylever == 0)
            {
                w00 = (256 - xlever);
                w01 = (xlever);

                if (alpha > 0 && col)
                {
                    alpha *= 32;
                    if (alpha > 200) alpha = 200;

                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT = ((short)((y00 * w00 + y01 * w01) >> 8)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;
                    oT++;
                }
                else
                {
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (short)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (short)((y00 * w00 + y01 * w01) >> 8);

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    *oT++ = (short)((y00 * w00 + y01 * w01) >> 8);
                }
            }
            else
            {
                w00 = (256 - xlever) * (256 - ylever);
                w01 = (xlever)* (256 - ylever);
                w10 = (256 - xlever) * (ylever);
                w11 = (xlever)* (ylever);


                if (alpha == 0 || col == 0)
                {
                    oT[0] = (short)((iT[0] * w00 + iT[nxtpix + 0] * w01 + iT[nxtln + 0] * w10 + iT[nxtln + nxtpix + 0] * w11)>> 16);
					oT[1] = (short)((iT[1] * w00 + iT[nxtpix + 1] * w01 + iT[nxtln + 1] * w10 + iT[nxtln + nxtpix + 1] * w11) >> 16);
					oT[2] = (short)((iT[2] * w00 + iT[nxtpix + 2] * w01 + iT[nxtln + 2] * w10 + iT[nxtln + nxtpix + 2] * w11) >> 16);
                    oT += nxtpix;
                    iT += nxtpix;
                }
                else
                {
                    alpha *= 32;
                    if (alpha > 200) alpha = 200;

                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[0] = ((short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[1] = ((short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    iT++;
                    y00 = *(iT);
                    y01 = *(iT + nxtpix);
                    y10 = *(iT + nxtln);
                    y11 = *(iT + nxtln + nxtpix);
                    oT[2] = ((short)((y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11) >> 16)*(256 - alpha) + *(oT - nxtpix)*alpha + 128) >> 8;

                    oT += nxtpix;
                }
            }
        }
    }

    return WARPLIB_SUCCESS;
}
