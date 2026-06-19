/*! @file GeoMesh.c

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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "GeoMesh.h"
#include "GeoMeshPrivate.h"
#include "GeoMeshApply.h"

#ifndef PI
#define PI 3.14159f
#endif

//
// private (to this library) functions
//

int geomesh_interp_bilinear(void *opaque, float row, float col, float *x, float *y);

int geomesh_dealloc_mesh(geomesh_t *gm)
{
    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS);

    if (gm->meshx != NULL)
    {
        free(gm->meshx);
        gm->meshx = NULL;
    }

    if (gm->meshy != NULL)
    {
        free(gm->meshy);
        gm->meshy = NULL;
    }

    gm->mesh_allocated = 0;
    gm->mesh_initialized = 0;

    return WARPLIB_SUCCESS;
}

int geomesh_alloc_mesh(geomesh_t *gm)
{
    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS);

    geomesh_dealloc_mesh(gm);
    if (gm->meshwidth <= 0 || gm->meshheight <= 0)
        return -1;

    gm->meshx = (float *)malloc(gm->meshwidth * gm->meshheight * sizeof(float));
    gm->meshy = (float *)malloc(gm->meshwidth * gm->meshheight * sizeof(float));

    gm->mesh_allocated = 1;

    return WARPLIB_SUCCESS;
}

int geomesh_check(void *opaque, unsigned int check_type)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    if (gm == NULL)
        return WARPLIB_ERROR_OBJECT_UNALLOCATED;

    if (check_type & GEOMESH_CHECK_OBJ_EXISTS)
    {
        if (strcmp(gm->signature, GEOMESH_SIGNATURE) != 0)
        {
            assert(WARPLIB_ERROR_OBJECT_UNINITIALIZED);
            return WARPLIB_ERROR_OBJECT_UNINITIALIZED;
        }
    }

    if (check_type & GEOMESH_CHECK_MESH_EXISTS)
    {
        if (gm->meshx == NULL || gm->meshy == NULL || gm->mesh_allocated == 0)
        {
            assert(WARPLIB_ERROR_MESH_UNALLOCATED);
            return WARPLIB_ERROR_MESH_UNALLOCATED;
        }
    }

    if (check_type & GEOMESH_CHECK_MESH_INITIALIZED)
    {
        if (gm->mesh_initialized == 0)
        {
            assert(WARPLIB_ERROR_MESH_UNINITIALIZED);
            return WARPLIB_ERROR_MESH_UNINITIALIZED;
        }
    }

    if (check_type & GEOMESH_CHECK_CACHE_EXISTS)
    {
        if (gm->cache == NULL || gm->num_elements_allocated == 0)
        {
            assert(WARPLIB_ERROR_CACHE_UNINITIALIZED);
            return WARPLIB_ERROR_CACHE_UNINITIALIZED;
        }
    }

    if (check_type & GEOMESH_CHECK_CACHE_INITIALIZED)
    {
        if (gm->cache_initialized == 0)
        {
            assert(WARPLIB_ERROR_CACHE_UNINITIALIZED);
            return WARPLIB_ERROR_CACHE_UNINITIALIZED;
        }
    }

    return WARPLIB_SUCCESS;
}

//
// public functions
//

void * geomesh_create(int meshwidth, int meshheight)
{
    geomesh_t *gm = (geomesh_t *)malloc(sizeof(geomesh_t));

    gm->meshwidth = meshwidth;
    gm->meshheight = meshheight;
    gm->separable = 0;
    gm->meshx = NULL;
    gm->meshy = NULL;
    gm->cache = NULL;
    gm->mesh_allocated = 0;
    gm->mesh_initialized = 0;
    gm->num_elements_allocated = 0;
    gm->cache_initialized = 0;
    gm->xstep = 0;
    gm->ystep = 0;

#ifdef _WIN32
	strcpy_s(gm->signature, sizeof(gm->signature), GEOMESH_SIGNATURE);
#else
	strcpy(gm->signature, GEOMESH_SIGNATURE);
#endif

    geomesh_alloc_mesh(gm);

    return gm;
}

void * geomesh_clone(void *opaque)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    geomesh_t *gmnew = NULL;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    gmnew = (geomesh_t *)geomesh_create(gm->meshwidth, gm->meshheight);
    geomesh_copy(gm, gmnew);

    return gmnew;
}

void geomesh_destroy(void *opaque)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS);

    geomesh_dealloc_cache(gm);
    geomesh_dealloc_mesh(gm);

    free(gm);
}

int geomesh_copy(void *opaque_src, void *opaque_dest)
{
    geomesh_t *gmsrc = (geomesh_t *)opaque_src;
    geomesh_t *gmdest = (geomesh_t *)opaque_dest;
    int meshrow, meshcol;

    GEOMESH_CHECK(gmsrc, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);
    GEOMESH_CHECK(gmdest, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    if (gmsrc->meshwidth != gmdest->meshwidth || gmsrc->meshheight != gmdest->meshheight)
        return -1;

    gmdest->srcformat = gmsrc->srcformat;
    gmdest->srcwidth = gmsrc->srcwidth;
    gmdest->srcheight = gmsrc->srcheight;
    gmdest->srcstride = gmsrc->srcstride;
    gmdest->srcbpp = gmsrc->srcbpp;
    gmdest->srcsubsampled = gmsrc->srcsubsampled;
    gmdest->srcchannels = gmsrc->srcchannels;
    gmdest->destformat = gmsrc->destformat;
    gmdest->destwidth = gmsrc->destwidth;
    gmdest->destheight = gmsrc->destheight;
    gmdest->deststride = gmsrc->deststride;
    gmdest->destbpp = gmsrc->destbpp;
    gmdest->destsubsampled = gmsrc->destsubsampled;
    gmdest->destchannels = gmsrc->destchannels;
    gmdest->separable = gmsrc->separable;
    gmdest->backgroundfill = gmsrc->backgroundfill;

    for (meshrow = 0; meshrow < gmdest->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gmdest->meshwidth; meshcol++)
        {
            int idx = meshrow * gmdest->meshwidth + meshcol;
            gmdest->meshx[idx] = gmsrc->meshx[idx];
            gmdest->meshy[idx] = gmsrc->meshy[idx];
        }
    }

    return WARPLIB_SUCCESS;
}


int geomesh_resize(void *opaque, int meshwidth, int meshheight)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS);

    gm->meshwidth = meshwidth;
    gm->meshheight = meshheight;
    geomesh_alloc_mesh(gm);

    return WARPLIB_SUCCESS;
}


int geomesh_init(void *opaque, int srcwidth, int srcheight, int srcstride, int srcformat, int destwidth, int destheight, int deststride, int destformat, int backgroundfill)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    int meshrow, meshcol;
    float x, y;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS);

    gm->srcformat  = srcformat;
    gm->srcwidth   = srcwidth;
    gm->srcheight  = srcheight;
    gm->srcstride  = srcstride;
    gm->destformat = destformat;
    gm->destwidth  = destwidth;
    gm->destheight = destheight;
    gm->deststride = deststride;
    gm->backgroundfill = backgroundfill;
    gm->srcsigned = 0;
    gm->destsigned = 0;
    gm->xstep = srcwidth / (float)(gm->meshwidth - 1);
    gm->ystep = srcheight / (float)(gm->meshheight - 1);

    switch (gm->srcformat)
    {
        case WARPLIB_FORMAT_YUY2:
        case WARPLIB_FORMAT_422YpCbCr8:
            gm->srcbpp = 2;
            gm->srcchannels = 3;
            gm->srcsubsampled = 1;
            break;
        case WARPLIB_FORMAT_32BGRA:
            gm->srcbpp = 4;
            gm->srcchannels = 4;
            gm->srcsubsampled = 0;
            break;
        case WARPLIB_FORMAT_64ARGB:
            gm->srcbpp = 8;
            gm->srcchannels = 4;
            gm->srcsubsampled = 0;
            break;
        case WARPLIB_FORMAT_RG48:
            gm->srcbpp = 6;
            gm->srcchannels = 3;
            gm->srcsubsampled = 0;
            break;
        case WARPLIB_FORMAT_WP13:
            gm->srcbpp = 6;
            gm->srcchannels = 3;
            gm->srcsubsampled = 0;
            gm->srcsigned = 1;
            break;
        case WARPLIB_FORMAT_W13A:
            gm->srcbpp = 8;
            gm->srcchannels = 4;
            gm->srcsubsampled = 0;
            gm->srcsigned = 1;
            break;
        default:
            gm->srcbpp = 2;
            gm->srcchannels = 3;
            gm->srcsubsampled = 1;
            break;
    }

    switch (gm->destformat)
    {
        case WARPLIB_FORMAT_YUY2:
        case WARPLIB_FORMAT_422YpCbCr8:
            gm->destbpp = 2;
            gm->destchannels = 3;
            gm->destsubsampled = 1;
            break;
        case WARPLIB_FORMAT_32BGRA:
            gm->destbpp = 4;
            gm->destchannels = 4;
            gm->destsubsampled = 0;
            break;
        case WARPLIB_FORMAT_64ARGB:
            gm->destbpp = 8;
            gm->destchannels = 4;
            gm->destsubsampled = 0;
            break;
        case WARPLIB_FORMAT_RG48:
            gm->destbpp = 6;
            gm->destchannels = 3;
            gm->destsubsampled = 0;
            break;
        case WARPLIB_FORMAT_WP13:
            gm->destbpp = 6;
            gm->destchannels = 3;
            gm->destsubsampled = 0;
            gm->destsigned = 1;
            break;
        case WARPLIB_FORMAT_W13A:
            gm->destbpp = 8;
            gm->destchannels = 4;
            gm->destsubsampled = 0;
            gm->destsigned = 1;
            break;
        default:
            gm->destbpp = 2;
            gm->destchannels = 3;
            gm->destsubsampled = 1;
            break;
    }

    if (srcstride == 0)
        gm->srcstride = gm->srcwidth * gm->srcbpp;
    if (deststride == 0)
        gm->deststride = gm->destwidth * gm->destbpp;

    // center vertically keeping scale the same
	y = 0;

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        // center horizontally keeping scale the same
		x = 0;
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            geomesh_setxy(gm, meshrow, meshcol, x, y);
            x += gm->xstep;
        }
        y += gm->ystep;
    }

    return WARPLIB_SUCCESS;
}

int geomesh_reinit(void *opaque)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    int reply;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS);

    reply = geomesh_init(opaque, gm->srcwidth, gm->srcheight, gm->srcstride, gm->srcformat,
        gm->destwidth, gm->destheight, gm->deststride, gm->destformat, gm->backgroundfill);

    return reply;
}

int geomesh_get_src_info(void *opaque, int *width, int *height, int *stride, int *bpp)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS);

    *width  = gm->srcwidth;
    *height = gm->srcheight;
    *stride = gm->srcstride;
    *bpp    = gm->srcbpp;

    return WARPLIB_SUCCESS;
}

int geomesh_get_dest_info(void *opaque, int *width, int *height, int *stride, int *bpp)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS);

    *width  = gm->destwidth;
    *height = gm->destheight;
    *stride = gm->deststride;
    *bpp    = gm->destbpp;

    return WARPLIB_SUCCESS;
}

void geomesh_dump(void *opaque, FILE *fp)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    int meshrow, meshcol;
    int meshrow1, meshcol1;
    float r = 0.0;
    float c = 0.0;
    float rstep, cstep;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    meshrow1 = gm->meshheight > 4 ? 4 : gm->meshheight;
    meshcol1 = gm->meshwidth > 4 ? 4 : gm->meshwidth;
    rstep = gm->destheight / (float)(gm->meshheight - 1);
    cstep = gm->destwidth / (float)(gm->meshwidth - 1);

    fprintf(fp, "    ");
    for (meshcol = 0; meshcol < meshcol1; meshcol++)
    {
        fprintf(fp, "          %7.1f", c);
        c += cstep;
    }
    fprintf(fp, "\n");

    fprintf(fp, "        +");
    for (meshcol = 0; meshcol < meshcol1; meshcol++)
    {
        fprintf(fp, "-----------------");
    }
    fprintf(fp, "\n");

    for (meshrow = 0; meshrow < meshrow1; meshrow++)
    {
        fprintf(fp, "%7.1f | ", r);
        r += rstep;
        for (meshcol = 0; meshcol < meshcol1; meshcol++)
        {
            fprintf(fp, "%7.1f %7.1f  ", geomesh_getx(gm, meshrow, meshcol), geomesh_gety(gm, meshrow, meshcol));
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, "\n");
}

//
// mesh getters and setters
//

float geomesh_getx(void *opaque, int meshrow, int meshcol)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    return gm->meshx[meshrow * gm->meshwidth + meshcol];
}

void geomesh_setx(void *opaque, int meshrow, int meshcol, float x)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    gm->meshx[meshrow * gm->meshwidth + meshcol] = x;
}

float geomesh_gety(void *opaque, int meshrow, int meshcol)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    return gm->meshy[meshrow * gm->meshwidth + meshcol];
}

void geomesh_sety(void *opaque, int meshrow, int meshcol, float y)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    gm->meshy[meshrow * gm->meshwidth + meshcol] = y;
}

void geomesh_getxy(void *opaque, int meshrow, int meshcol, float *x, float *y)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    *x = gm->meshx[meshrow * gm->meshwidth + meshcol];
    *y = gm->meshy[meshrow * gm->meshwidth + meshcol];
}

void geomesh_setxy(void *opaque, int meshrow, int meshcol, float x, float y)
{
    geomesh_t *gm = (geomesh_t *)opaque;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    gm->meshx[meshrow * gm->meshwidth + meshcol] = x;
    gm->meshy[meshrow * gm->meshwidth + meshcol] = y;
}

// search successively smaller perimeters until find no undefined pixels
int geomesh_calculate_scale(void *opaque, int algorithm, float *scale)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    int xc = gm->destwidth / 2;
    int yc = gm->destheight / 2;
    int r, c;
    float aspect;
    int out_of_bounds;
    int idx;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    aspect = gm->destwidth / (float)(gm->destheight);

    if (algorithm == WARPLIB_ALGORITHM_BEST_FIT)
    {
        int y0 = 0;
        int y1 = gm->destheight;
        int x0 = 0;
        int x1 = gm->destwidth;
        float scaleV, scaleH;
        out_of_bounds = 0;

        geomesh_cache_init_bilinear_range_vertical(opaque, xc, xc+1);

        // mid column, up
        for (r = yc; !out_of_bounds && r >= 0; r--)
        {
            idx = gm->num_elements_allocated * (r * gm->destwidth + xc);
            if (gm->cache[idx] < 0)
            {
                out_of_bounds = 1;
                y0 = r;
            }
        }
        // mid column, down
        for (out_of_bounds = 0, r = yc; !out_of_bounds && r < gm->destheight; r++)
        {
            idx = gm->num_elements_allocated * (r * gm->destwidth + xc);
            if (gm->cache[idx] < 0)
            {
                out_of_bounds = 1;
                y1 = r;
            }
        }

        scaleV = ((float)gm->destheight+1.0f) / (float)(y1 - y0);


        geomesh_cache_init_bilinear_range(opaque, yc, yc+1);

        // mid row, left
        for (c = xc; !out_of_bounds && c >= 0; c--)
        {
            idx = gm->num_elements_allocated * (yc * gm->destwidth + c);
            if (gm->cache[idx] < 0)
            {
                out_of_bounds = 1;
                x0 = c;
            }
        }
        // mid row, right
        for (out_of_bounds = 0, c = xc; !out_of_bounds && c < gm->destwidth; c++)
        {
            idx = gm->num_elements_allocated * (yc * gm->destwidth + c);
            if (gm->cache[idx] < 0)
            {
                out_of_bounds = 1;
                x1 = c;
            }
        }

        scaleH = ((float)gm->destwidth+1.0f) / (float)(x1 - x0);

        if(scaleH > scaleV)
            *scale = scaleH;
        else
            *scale = scaleV;

    }
    else if (algorithm == WARPLIB_ALGORITHM_PRESERVE_VERTICAL)
    {
        int y0 = 0;
        int y1 = gm->destheight;
        out_of_bounds = 0;

        geomesh_cache_init_bilinear_range_vertical(opaque, xc, xc+1);

        // mid column, up
        for (r = yc; !out_of_bounds && r >= 0; r--)
        {
            idx = gm->num_elements_allocated * (r * gm->destwidth + xc);
            if (gm->cache[idx] < 0)
            {
                out_of_bounds = 1;
                y0 = r;
            }
        }
        // mid column, down
        for (out_of_bounds = 0, r = yc; !out_of_bounds && r < gm->destheight; r++)
        {
            idx = gm->num_elements_allocated * (r * gm->destwidth + xc);
            if (gm->cache[idx] < 0)
            {
                out_of_bounds = 1;
                y1 = r;
            }
        }

        *scale = gm->destheight / (float)(y1 - y0);
    }
    else if (algorithm == WARPLIB_ALGORITHM_PRESERVE_HORIZONTAL)
    {
        int x0 = 0;
        int x1 = gm->destwidth;
        out_of_bounds = 0;

        geomesh_cache_init_bilinear_range(opaque, yc, yc+1);

        // mid row, left
        for (c = xc; !out_of_bounds && c >= 0; c--)
        {
            idx = gm->num_elements_allocated * (yc * gm->destwidth + c);
            if (gm->cache[idx] < 0)
            {
                out_of_bounds = 1;
                x0 = c;
            }
        }
        // mid row, right
        for (out_of_bounds = 0, c = xc; !out_of_bounds && c < gm->destwidth; c++)
        {
            idx = gm->num_elements_allocated * (yc * gm->destwidth + c);
            if (gm->cache[idx] < 0)
            {
                out_of_bounds = 1;
                x1 = c;
            }
        }

        *scale = gm->destwidth / (float)(x1 - x0);
    }
    else if (algorithm == WARPLIB_ALGORITHM_PRESERVE_EVERYTHING)
    {
        float posx,posy,x,y;

        //top left

        if (gm->num_elements_allocated == 0)
            geomesh_alloc_cache(gm);

        *scale = 1.0f;

        for(posx=0.0; posx<(float)(gm->destheight>>1); posx += 1.0f)
        {
            posy = posx;
            geomesh_interp_bilinear(opaque, posx, posy, &x, &y);
            srand(0);

            if (x > 0.0 && y > 0.0)
            {
                int interate;

                for (interate = 0; interate < 200; interate++)
                {
                    float rx,ry;

                    rx = posx + ((float)((rand() & 255)-127))/128.0f;
                    ry = posy + ((float)((rand() & 255)-127))/128.0f;

                    geomesh_interp_bilinear(opaque, rx, ry, &x, &y);

                    if (x > 0.0 && y > 0.0 && rx <= posx)
                    {
                        posx = rx;
                        posy = ry;
                    }
                }

                *scale = gm->destwidth / ((float)(gm->destwidth) - posx*2.0f);
                break;
            }
        }
    }

    return WARPLIB_SUCCESS;
}


