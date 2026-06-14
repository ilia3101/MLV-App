/*! @file GeoMeshCache.c

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
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <math.h>

#include "GeoMesh.h"
#include "GeoMeshPrivate.h"
#include "GeoMeshInterp.h"


#ifndef PI
#define PI 3.14159265359f
#define HPI 1.5707963268f
#endif

#define DEG2RAD(d)    (PI*(d)/180.0)
#define RAD2DEG(r)    (180.0*(r)/PI)
//
// private (to this library) cache functions
//

//
// cache buffer management
//

int geomesh_dealloc_cache(geomesh_t *gm)
{
    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS);

    if (gm->cache != NULL)
    {
        free(gm->cache);
        gm->cache = NULL;
    }

    gm->num_elements_allocated = 0;
    gm->cache_initialized = 0;

    return WARPLIB_SUCCESS;
}


int geomesh_alloc_cache(geomesh_t *gm)
{
    int elements_per_pixel = 3;
    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS);

    geomesh_dealloc_cache(gm);
    if (gm->destwidth <= 0 || gm->destheight <= 0)
        return -1;

    if(gm->srcsubsampled)
        elements_per_pixel++;

    if(gm->backgroundfill)
        elements_per_pixel++;

    gm->cache = (int *)malloc(elements_per_pixel * gm->destwidth * gm->destheight * sizeof(int));
    gm->num_elements_allocated = elements_per_pixel;

    return WARPLIB_SUCCESS;
}


int approx_equal2(int x, int y)
{
	if (y > 1080)
	{
		x >>= 6;
		y >>= 6;
	}
	else if (y > 540)
	{
		x >>= 5;
		y >>= 5;
	}
	else
	{
		x >>= 4;
		y >>= 4;
	}

	if (x == y || x + 1 == y || x == y + 1)
		return 1;

	return 0;
}

int ifequirect2(int x, int y)
{
    if(x == y*2)
        return 1;

    return 0;
}

//
// cache initialization
//

int geomesh_cache_init_bilinear(void *opaque)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float x, y;
    int *ptr;
    int row, col;
    int idx;
    int equirect;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    geomesh_alloc_cache(gm);
    ptr = gm->cache;

	equirect = ifequirect2(gm->srcwidth, gm->srcheight);

    if(gm->srcsubsampled == 1) //4:2:2
    {
        int yoffset, uvoffset;

        for (row = 0; row < gm->destheight; row++)
        {
            for (col = 0; col < gm->destwidth; col++)
            {
                // REVISIT: trailing edge (right, bottom) handling
                geomesh_interp_bilinear(gm, (float)row, (float)col, &x, &y);
                if (x < 0.0f || x >= gm->srcwidth - 1 || y < 0.0f || y >= gm->srcheight - 2)
                {
                    uvoffset = yoffset = -1;
                }
                else
                {
                    yoffset = (int)y * gm->srcstride + (int)x * gm->srcbpp;
                    uvoffset = yoffset + 1;

                    if ((col & 0x1) != (((int)x) & 0x1))
                        uvoffset += 2;
                    if (col >= gm->destwidth - 1)
                        uvoffset -= 4;
                }
                *ptr++ = yoffset;
                *ptr++ = uvoffset;
                *ptr++ = (int)((x - (int)x) * 256 + 0.5f);
                *ptr++ = (int)((y - (int)y) * 256 + 0.5f);
            }
        }
    }
    else
    {
        for (row = 0; row < gm->destheight; row++)
        {
            for (col = 0; col < gm->destwidth; col++)
            {
                // REVISIT: trailing (right, bottom) edge handling
                geomesh_interp_bilinear(gm, (float)row, (float)col, &x, &y);

                if(equirect)
                {
                    if (y < 0.0f || y >= gm->srcheight - 2)
                        idx = -1;
                    else
                        idx = (int)y * gm->srcstride + (int)x * gm->srcbpp;
                }
                else
                {
                    if (x < 0.0f || x >= gm->srcwidth - 1 || y < 0.0f || y >= gm->srcheight - 2)
                        idx = -1;
                    else
                        idx = (int)y * gm->srcstride + (int)x * gm->srcbpp;
                }
                *ptr++ = idx;
                *ptr++ = (int)((x - (int)x) * 256 + 0.5f);
                *ptr++ = (int)((y - (int)y) * 256 + 0.5f);
            }
        }
    }

    return WARPLIB_SUCCESS;
}




int geomesh_cache_init_bilinear_range(void *opaque, int rowStart, int rowStop)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float x, y;
    int yoffset, uvoffset;
    int *ptr;
    int row, col;
    int fill = -1;
    int equirect;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    if(gm->num_elements_allocated == 0)
        geomesh_alloc_cache(gm);

    if(gm->backgroundfill)
        fill = 0;

	equirect = ifequirect2(gm->srcwidth, gm->srcheight);

    ptr = gm->cache;
    ptr += gm->num_elements_allocated*rowStart*gm->destwidth;
	
    for (row = rowStart; row < rowStop; row++)
    {
        for (col = 0; col < gm->destwidth; col++)
        {
            int alpha = 0;
            // REVISIT: trailing edge (right, bottom) handling
            geomesh_interp_bilinear(gm, (float)row, (float)col, &x, &y);
            if (((x < 0.0f || x >= gm->srcwidth - 1) && !equirect)  || y < 0.0f || y >= gm->srcheight - 1)
            {
                if(fill >= 0)
                {
                    if(x<0.0 && !equirect) alpha = (int)(1-x*256/gm->srcwidth), y += ((rand() & 0xffff) * (int)(-x*4) / 0xffff) + x, x = 0;
                    if(x>(float)(gm->srcwidth-1) && !equirect) alpha = (int)(1+(x-gm->srcwidth)*256/gm->srcwidth), y += ((rand() & 0xffff) * (int)(-(x-(gm->srcwidth-1))*4) / 0xffff) + (x-(gm->srcwidth-1)), x = (float)(gm->srcwidth-1);
                    if(y<0.0) alpha = (int)(1-y*256/gm->srcheight), x += ((rand() & 0xffff) * (int)(-y*4) / 0xffff) + y, y=0;
                    if(y>(float)(gm->srcheight-1)) alpha = (int)(1+(y-gm->srcheight)*256/gm->srcheight), x += ((rand() & 0xffff) * (int)(-(y-(gm->srcheight-1))*4) / 0xffff) + (y-(gm->srcheight-1)), y = (float)(gm->srcheight-1);

                    if(x<0.0 && !equirect) x = 0;
                    if(x>(float)(gm->srcwidth-1) && !equirect) x = (float)(gm->srcwidth-1);
                    if(y<0.0) y=0;
                    if(y>(float)(gm->srcheight-1)) y = (float)(gm->srcheight-1);

                    yoffset = (int)y * gm->srcstride + (int)x * gm->srcbpp;
                }
                else
                    yoffset = fill;
            }
            else
            {
                yoffset = (int)y * gm->srcstride + (int)x * gm->srcbpp;

                if (yoffset >= ((gm->srcstride * (gm->srcheight - 1)) - gm->srcbpp))
                    yoffset = fill;
            }

            *ptr++ = yoffset;
            if(gm->srcsubsampled)
            {
                uvoffset = yoffset + 1;
                if ((col & 0x1) != (((int)x) & 0x1))
                    uvoffset += 2;
                if ((int)x >= gm->destwidth-3)
                    uvoffset -= 4;
                *ptr++ = uvoffset;
            }
            *ptr++ = (int)((x - (int)x) * 256 + 0.5f);

            if(y>=gm->srcheight-2)
                *ptr++ = 0;
            else
                *ptr++ = (int)((y - (int)y) * 256 + 0.5f);

            if(gm->backgroundfill)
                *ptr++ = (int)alpha;
        }
    }

    return WARPLIB_SUCCESS;
}



int geomesh_blur_vertical_range(void *opaque, int colStart, int colStop,
            uint8_t *output,        // Output frame buffer
            int pitch)            // Output frame pitch)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    //float x, y;
    int *ptr;
    int row, col;

    uint8_t *src;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    if(gm->num_elements_allocated == 0)
        geomesh_alloc_cache(gm);


    for (row = gm->destheight/2; row > 0; row--)
    {
        src = output;
        src += gm->srcbpp*colStart;
        src += gm->destwidth*gm->srcbpp*row;

        ptr = gm->cache;
        ptr += gm->num_elements_allocated*colStart;
        ptr += gm->num_elements_allocated*gm->destwidth*row;


        for (col = colStart; col < colStop; col++)
        {
            int alpha = 0;

            ptr+=gm->num_elements_allocated-1;
            alpha = *ptr++;

            if(alpha > 0)
            {
                alpha *= 32;
                if(alpha > 200) alpha = 200;

                src[0] = (src[0]*(256-alpha) + src[pitch]*alpha + 128)>>8;
                src[1] = (src[1]*(256-alpha) + src[1+pitch]*alpha + 128)>>8;
                if(!gm->srcsubsampled)
                {
                    src[2] = (src[2]*(256-alpha) + src[2-pitch]*alpha + 128)>>8;
                    if(gm->srcchannels > 3)
                        src[3] = (src[3]*(256-alpha) + src[3-pitch]*alpha + 128)>>8;
                }
            }
            src+=gm->srcbpp;
        }
    }

    for (row = gm->destheight/2; row < gm->destheight-1; row++)
    {
        src = output;
        src += gm->srcbpp*colStart;
        src += gm->destwidth*gm->srcbpp*row;

        ptr = gm->cache;
        ptr += gm->num_elements_allocated*colStart;
        ptr += gm->num_elements_allocated*gm->destwidth*row;

        for (col = colStart; col < colStop; col++)
        {
            int alpha = 0;

            ptr+=gm->num_elements_allocated-1;
            alpha = *ptr++;

            if(alpha > 0)
            {
                alpha *= 32;
                if(alpha > 200) alpha = 200;

                src[0] = (src[0]*(256-alpha) + src[-pitch]*alpha + 128)>>8;
                src[1] = (src[1]*(256-alpha) + src[1-pitch]*alpha + 128)>>8;

                if(!gm->srcsubsampled)
                {
                    src[2] = (src[2]*(256-alpha) + src[2-pitch]*alpha + 128)>>8;
                    if(gm->srcchannels > 3)
                        src[3] = (src[3]*(256-alpha) + src[3-pitch]*alpha + 128)>>8;
                }
            }
            src+=gm->srcbpp;
        }
    }

    return WARPLIB_SUCCESS;
}



int geomesh_cache_init_bilinear_range_vertical(void *opaque, int colStart, int colStop)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float x, y;
    int yoffset, uvoffset;
    int *ptr;
    int row, col;
    int fill = -1;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    if(gm->num_elements_allocated == 0)
        geomesh_alloc_cache(gm);

    if(gm->backgroundfill)
        fill = 0;

    for (row = 0; row < gm->destheight; row++)
    {
        ptr = gm->cache;
        ptr += gm->num_elements_allocated*row*gm->destwidth;
        ptr += gm->num_elements_allocated*colStart;

        for (col = colStart; col < colStop; col++)
        {
            // REVISIT: trailing edge (right, bottom) handling
            geomesh_interp_bilinear(gm, (float)row, (float)col, &x, &y);
            if (x < 0.0f || x >= gm->srcwidth - 1 || y < 0.0f || y >= gm->srcheight - 2)
            {
                if (fill >= 0)
                {
                    if (x<0.0) x = 0;
                    if (x>=gm->srcwidth - 1) x = (float)gm->srcwidth - 1.001f;
                    if (y<0.0) y = 0;
                    if (y>=gm->srcheight - 2) y = (float)gm->srcheight - 2.001f;
                    yoffset = (int)y * gm->srcstride + (int)x * gm->srcbpp;
                }
                else
                    yoffset = fill;
            }
            else
                yoffset = (int)y * gm->srcstride + (int)x * gm->srcbpp;

            *ptr++ = yoffset;
            if(gm->srcsubsampled)
            {
                uvoffset = yoffset + 1;
                if ((col & 0x1) != (((int)x) & 0x1))
                    uvoffset += 2;
                if (col >= gm->destwidth - 1)
                    uvoffset -= 4;
                *ptr++ = uvoffset;
            }
            *ptr++ = (int)((x - (int)x) * 256 + 0.5f);
            *ptr++ = (int)((y - (int)y) * 256 + 0.5f);

            if (gm->backgroundfill)
                *ptr++ = 0;
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_cache_init_bilinear_2vuy(void *opaque)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float x, y;
    int yoffset, uvoffset;
    int *ptr;
    int row, col;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    geomesh_alloc_cache(gm);

    ptr = gm->cache;

    for (row = 0; row < gm->destheight; row++)
    {
        for (col = 0; col < gm->destwidth; col++)
        {
            // REVISIT: trailing edge (right, bottom) handling
            geomesh_interp_bilinear(gm, (float)row, (float)col, &x, &y);
            if (x < 0.0f || x >= gm->srcwidth - 1 || y < 0.0f || y >= gm->srcheight - 2)
                yoffset = -1;
            else
                yoffset = (int)y * gm->srcstride + (int)x * gm->srcbpp + 1;
            uvoffset = yoffset - 1;
            if ((col & 0x1) != (((int)x) & 0x1))
                uvoffset += 2;
            if (col >= gm->destwidth - 1)
                uvoffset -= 4;
            *ptr++ = yoffset;
            *ptr++ = uvoffset;
            *ptr++ = (int)((x - (int)x) * 256 + 0.5f);
            *ptr++ = (int)((y - (int)y) * 256 + 0.5f);
        }
    }

    return WARPLIB_SUCCESS;
}

#if 0
static int geomesh_interp_bilinear2(void *opaque, float row, float col, float *x, float *y)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float rowidx, colidx;
    float xlever, ylever;
    int   meshrow0, meshcol0;
    int   meshrow1, meshcol1;
    float x00, y00, x01, y01, x10, y10, x11, y11;
    float w00, w01, w10, w11;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED);

    rowidx = row / (float)(gm->destheight) * (gm->meshheight - 1);
    colidx = col / (float)(gm->destwidth) * (gm->meshwidth - 1);
    meshrow0 = (int)rowidx;
    meshcol0 = (int)colidx;
    xlever = colidx - meshcol0;
    ylever = rowidx - meshrow0;

    if (meshrow0 < 0 ||
        meshrow0 >= gm->meshheight - 1 ||
        meshcol0 < 0 ||
        meshcol0 >= gm->meshwidth - 1)
    {
        *x = -10.0;
        *y = -10.0;
        return 1;
    }

    meshrow1 = meshrow0 + 1;
    meshcol1 = meshcol0 + 1;

    geomesh_getxy(gm, meshrow0, meshcol0, &x00, &y00);
    geomesh_getxy(gm, meshrow0, meshcol1, &x01, &y01);
    geomesh_getxy(gm, meshrow1, meshcol0, &x10, &y10);
    geomesh_getxy(gm, meshrow1, meshcol1, &x11, &y11);

    w00 = (1 - ylever) * (1 - xlever);
    w01 = (1 - ylever) * (xlever);
    w10 = (ylever) * (1 - xlever);
    w11 = (ylever) * (xlever);

    *x = x00 * w00 + x01 * w01 + x10 * w10 + x11 * w11;
    *y = y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11;

    return WARPLIB_SUCCESS;
}
#endif

int geomesh_generate_displacement_map(void *opaque, int w, int h, float *displacementMap)
{
    float *oT = displacementMap;
    geomesh_t *gm = (geomesh_t *)opaque;
    int x, y;
    uint32_t meshRowIdx;
    uint32_t meshColIdx;
    float meshRowIdxReal;
    float meshColIdxReal;
    float thisRowWeight;
    float thisColWeight;
    float nextRowWeight;
    float nextColWeight;
    
    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);
    
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            meshColIdxReal = x / gm->xstep;
            meshRowIdxReal = y / gm->ystep;
            meshColIdx = (uint32_t)meshColIdxReal;
            meshRowIdx = (uint32_t)meshRowIdxReal;
            thisColWeight = 1.0f - (meshColIdxReal - meshColIdx);
            thisRowWeight = 1.0f - (meshRowIdxReal - meshRowIdx);
            nextColWeight = 1.0f - thisColWeight;
            nextRowWeight = 1.0f - thisRowWeight;
            
            float dx = 0;
            float dy = 0;
            
            // this row/this column
            dx += ((*(gm->meshx + (gm->meshwidth * meshRowIdx) + meshColIdx)) * (thisRowWeight * thisColWeight))/((float)w-1);
            dy += ((*(gm->meshy + (gm->meshwidth * meshRowIdx) + meshColIdx)) * (thisRowWeight * thisColWeight))/((float)h-1);
            
            // this row/next column
            dx += ((*(gm->meshx + (gm->meshwidth * meshRowIdx) + (meshColIdx + 1))) * (thisRowWeight * nextColWeight))/((float)w-1);
            dy += ((*(gm->meshy + (gm->meshwidth * meshRowIdx) + (meshColIdx + 1))) * (thisRowWeight * nextColWeight))/((float)h-1);
            
            // next row/this column
            dx += ((*(gm->meshx + (gm->meshwidth * (meshRowIdx + 1)) + meshColIdx)) * (nextRowWeight * thisColWeight))/((float)w-1);
            dy += ((*(gm->meshy + (gm->meshwidth * (meshRowIdx + 1)) + meshColIdx)) * (nextRowWeight * thisColWeight))/((float)h-1);
            
            // next row/next column
            dx += ((*(gm->meshx + (gm->meshwidth * (meshRowIdx + 1)) + (meshColIdx + 1))) * (nextRowWeight * nextColWeight))/((float)w-1);
            dy += ((*(gm->meshy + (gm->meshwidth * (meshRowIdx + 1)) + (meshColIdx + 1))) * (nextRowWeight * nextColWeight))/((float)h-1);
            
            float sx = ((float)x)/((float)w-1);
            float sy = ((float)y)/((float)h-1);
            *oT++ = sx-dx;
            *oT++ = sy-dy;
        }
    }
    
    return WARPLIB_SUCCESS;
}
