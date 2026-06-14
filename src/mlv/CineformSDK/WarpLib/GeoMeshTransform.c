/*! @file GeoMeshTransform.c

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

#ifndef PI
#define PI 3.14159265359
#define HPI 1.5707963268
#define TWOPI 6.28318530718
#endif

float foffset1 = 0.0;
float foffset2 = 0.0;

#define DEG2RAD(d)    (PI*(d)/180.0)
#define RAD2DEG(r)    (180.0*(r)/PI)


//
// mesh transforms
//

int geomesh_transform_scale(void *opaque, float rowscale, float colscale)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float newx, newy;
    float x, y;
    float src_center_x;
    float src_center_y;
    int   meshrow, meshcol;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    src_center_x = gm->srcwidth / 2.0f;
    src_center_y = gm->srcheight / 2.0f;

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            geomesh_getxy(gm, meshrow, meshcol, &x, &y);
            x -= src_center_x;
            y -= src_center_y;
            newx = (x / colscale) + src_center_x;
            newy = (y / rowscale) + src_center_y;
            geomesh_setxy(gm, meshrow, meshcol, newx, newy);
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_transform_pan(void *opaque, float left, float top)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    int meshrow, meshcol;
    int idx;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            idx = meshrow * gm->meshwidth + meshcol;
            gm->meshx[idx] += left;
            gm->meshy[idx] += top;
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_transform_rotate(void *opaque, float angle_degrees)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float angle_radians;
    float sin_theta;
    float cos_theta;
    float x, y;
    float newx, newy;
    float centerx, centery;
    int   meshrow, meshcol;

    //foffset2 = angle_degrees / 30.0;
    //angle_degrees = 0.0;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    angle_radians = (float)(PI * angle_degrees / 180.0f);
    sin_theta = sinf(angle_radians);
    cos_theta = cosf(angle_radians);

    centerx = gm->srcwidth / 2.0f;
    centery = gm->srcheight / 2.0f;

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            // destination x,y
            geomesh_getxy(gm, meshrow, meshcol, &x, &y);
            x -= centerx;
            y -= centery;
            newx = x * cos_theta - y * sin_theta + centerx;
            newy = x * sin_theta + y * cos_theta + centery;
            geomesh_setxy(gm, meshrow, meshcol, newx, newy);
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_transform_fisheye(void *opaque, float max_theta_degrees)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float x, y;//, r;
    float newx, newy;
    float radius, newradius;
    float max_theta_radians = (float)(DEG2RAD(fabs(max_theta_degrees)));
    float f;
    float maxradius;
    float theta;
    float src_center_x, src_center_y;
    int   meshrow, meshcol;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    maxradius = sqrtf((gm->destwidth * gm->destwidth + gm->destheight * gm->destheight) / 4.0f);


    //for r = 1.0;
    //max_theta_radians = -(-12.047899*r*r*r + 5.3339*r*r + 80.560545*r);
    //r = 0.8811; //sensorcrop
    //max_theta_degrees = -(-12.047899*r*r*r + 5.3339*r*r + 80.560545*r);
    //max_theta_radians = (float)(PI * fabs(max_theta_degrees) / 180.0f);

    f = maxradius / tanf(max_theta_radians);
    src_center_x = gm->srcwidth / 2.0f;
    src_center_y = gm->srcheight / 2.0f;

    if (max_theta_degrees == 0.0f)
        return WARPLIB_SUCCESS;

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            // destination x,y
            geomesh_getxy(gm, meshrow, meshcol, &x, &y);
            x -= src_center_x;
            y -= src_center_y;
            radius = sqrtf(x * x + y * y);
            /*
            r = radius / maxradius;
            r *= 0.8811;

            if(r < 1.0)
                theta = (-12.047899*r*r*r + 5.3339*r*r + 80.560545*r);
            else
                theta = -8.94*r*r + 70.92*r + 11.85; // curved extension, less extreme then the cubic lens model.


            r += (4E-7*theta*theta*theta - 1.4E-5*theta*theta + 0.0123*theta - 0.0135*theta);  //r is now an ideal fisheye. IS IT?
            r /= 0.8811;
            radius = r * maxradius;
*/
            theta = atanf(radius / f);
            // lens adjustment function
            if (max_theta_degrees < 0)
            {
                newradius = f * theta;
            }
            else
            {
                newradius = radius;
                radius = f * theta;
            }
            newx = x * newradius / radius + src_center_x;
            newy = y * newradius / radius + src_center_y;


            geomesh_setxy(gm, meshrow, meshcol, newx, newy);
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_transform_gopro_to_rectilinear(void *opaque, float sensorcrop)
{
     geomesh_t *gm = (geomesh_t *)opaque;
    float x, y;
    float src_center_x;
    float src_center_y;
    int   meshrow, meshcol;

    float phi, theta, radius, r;
    float maxradius, normalized_radius;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    maxradius = sqrtf((gm->destwidth * gm->destwidth + gm->destheight * gm->destheight) / 4.0f);

    src_center_x = gm->srcwidth / 2.0f;
    src_center_y = gm->srcheight / 2.0f;


    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            // destination x,y
            geomesh_getxy(gm, meshrow, meshcol, &x, &y);
            x -= src_center_x;
            y -= src_center_y;

            radius = sqrtf(x * x + y * y);

            r = radius / maxradius;  // normalized_radius
            r *= sensorcrop;
            //phi = DEG2RAD(-12.047899*r*r*r + 5.3339*r*r + 80.560545*r);  // HERO3
            phi = (float)(DEG2RAD(-10.28871*r*r + 84.878*r));  //HERO3+/4 lens to image sphere



            if(x > 0.0)
            {
                if(y >= 0.0)
                    theta = atanf((float)(fabs(y)/fabs(x)));
                else
                    theta = -atanf((float)(fabs(y)/fabs(x)));
            }
            else if(x == 0.0)
            {
                if(y >= 0.0)
                    theta = (float)(HPI);
                else
                    theta = (float)(-HPI);
            }
            else // x< 0.0
            {
                if(y >= 0.0)
                    theta = (float)(PI - atanf((float)(fabs(y)/fabs(x))));
                else
                    theta = (float)(PI + atanf((float)(fabs(y)/fabs(x))));
            }

            normalized_radius = phi;
            normalized_radius /= sensorcrop;

            normalized_radius = atanf(normalized_radius*0.75f); // 0.75 is a Guess.

            radius = maxradius * (float)normalized_radius;

            x = cosf(theta)*radius;
            y = sinf(theta)*radius;

            x += src_center_x;
            y += src_center_y;

            geomesh_setxy(gm, meshrow, meshcol, x, y);
        }
    }

    return WARPLIB_SUCCESS;
}


int geomesh_transform_defish(void *opaque, float fov)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float x, y;
    float src_center_x;
    float src_center_y;
    int   meshrow, meshcol;

    float theta, radius;
    float maxradius;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    if (fov > 0)
        maxradius = 0.5f * gm->srcheight * fov / (57.2958f * atanf(tanf(0.785398f * fov / 45.0f)));      //vertical height anchor
    else
    {
#if 0 // Repeatable
        maxradius = 0.5 * gm->srcheight;    //vertical height anchor
#else
        maxradius = sqrtf((gm->srcwidth *gm->srcwidth + gm->srcheight*gm->srcheight) / 4.0f); // Corner Anchor
#endif
    }


    src_center_x = gm->srcwidth / 2.0f;
    src_center_y = gm->srcheight / 2.0f;

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            // destination x,y
            geomesh_getxy(gm, meshrow, meshcol, &x, &y);
            x -= src_center_x;
            y -= src_center_y;


            if (x > 0.0)
            {
                if (y >= 0.0)
                    theta = atanf((float)(fabs(y) / fabs(x)));
                else
                    theta = -atanf((float)(fabs(y) / fabs(x)));
            }
            else if (x == 0.0)
            {
                if (y >= 0.0)
                    theta = (float)(HPI);
                else
                    theta = (float)(-HPI);
            }
            else // x< 0.0
            {
                if (y >= 0.0)
                    theta = (float)(PI - atanf((float)(fabs(y) / fabs(x))));
                else
                    theta = (float)(PI + atanf((float)(fabs(y) / fabs(x))));
            }


            radius = sqrtf(x * x + y * y);
			
            if (fov > 0)
                radius = maxradius * 57.2958f * atanf((radius / maxradius) * tanf(0.785398f * fov / 45.0f)) / fov;
            else
            {
				float val;

				//prevent wrap-around
				if ((radius / maxradius) * (0.785398f * -fov / 45.0f) >= 1.57) 
					radius = 1.57f * maxradius / (0.785398f * -fov / 45.0f);

                val = maxradius * tanf((radius / maxradius) * (0.785398f * -fov / 45.0f)) / tanf((0.785398f * -fov / 45.0f));
             /*   if (val > 10.0f * maxradius || val < 0)
                    radius = 10.0f * maxradius;
                else*/
                    radius = val; 
            }

			x = cosf(theta)*radius;
            y = sinf(theta)*radius;

            x += src_center_x;
            y += src_center_y;

            geomesh_setxy(gm, meshrow, meshcol, x, y);
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_transform_orthographic(void *opaque, float max_theta_degrees)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float x, y;
    float newx, newy;
    float radius, newradius;
    float max_theta_radians = (float)DEG2RAD(fabs(max_theta_degrees));
    float theta;
    float maxradius;
    float f;
    float src_center_x, src_center_y;
    int   meshrow, meshcol;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    maxradius = sqrtf((gm->destwidth * gm->destwidth + gm->destheight * gm->destheight) / 4.0f);
    f = maxradius / tanf(max_theta_radians);
    src_center_x = gm->srcwidth / 2.0f;
    src_center_y = gm->srcheight / 2.0f;

    if (max_theta_degrees == 0.0f)
        return WARPLIB_SUCCESS;

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            // destination x,y
            geomesh_getxy(gm, meshrow, meshcol, &x, &y);
            x -= src_center_x;
            y -= src_center_y;
            radius = sqrtf(x * x + y * y);
            theta = atanf(radius / f);
            // lens adjustment function
            newradius = radius;
            radius = f * sinf(theta);
            newx = x * newradius / radius + src_center_x;
            newy = y * newradius / radius + src_center_y;
            geomesh_setxy(gm, meshrow, meshcol, newx, newy);
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_transform_stereographic(void *opaque, float max_theta_degrees)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float x, y;
    float newx, newy;
    float radius, newradius, maxradius;
    float max_theta_radians = (float)(DEG2RAD(fabs(max_theta_degrees)));
    float theta;
    float f;
    float src_center_x, src_center_y;
    int   meshrow, meshcol;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    maxradius = sqrtf((gm->destwidth * gm->destwidth + gm->destheight * gm->destheight) / 4.0f);
    f = maxradius / tanf(max_theta_radians);
    src_center_x = gm->srcwidth / 2.0f;
    src_center_y = gm->srcheight / 2.0f;

    if (max_theta_degrees == 0.0f)
        return WARPLIB_SUCCESS;

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            // destination x,y
            geomesh_getxy(gm, meshrow, meshcol, &x, &y);
            x -= src_center_x;
            y -= src_center_y;
            radius = sqrtf(x * x + y * y);
            theta = atanf(radius / f);
            // lens adjustment function
            newradius = radius;
            radius = 2.0f * f * tanf(theta/2.0f);
            newx = x * newradius / radius + src_center_x;
            newy = y * newradius / radius + src_center_y;
            geomesh_setxy(gm, meshrow, meshcol, newx, newy);
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_transform_flip_horz(void *opaque)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float newx;
    float x;
    float src_center_x;
    int   meshrow, meshcol;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    src_center_x = gm->srcwidth / 2.0f;

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            x = geomesh_getx(gm, meshrow, meshcol) - src_center_x;
            newx = src_center_x - x;
            geomesh_setx(gm, meshrow, meshcol, newx);
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_transform_flip_vert(void *opaque)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float newy;
    float y;
    float src_center_y;
    int meshrow, meshcol;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);


    src_center_y = gm->srcheight / 2.0f;

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            y = geomesh_gety(gm, meshrow, meshcol) - src_center_y;
            newy = src_center_y - y;
            geomesh_sety(gm, meshrow, meshcol, newy);
        }
    }

    return WARPLIB_SUCCESS;
}

int geomesh_transform_horizontal_stretch_poly(void *opaque, float a, float b, float c)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float newx;
    float x, y, xn, yn;
    float src_center_y;
    int meshrow, meshcol;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    src_center_y = gm->srcheight / 2.0f;

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            geomesh_getxy(gm, meshrow, meshcol, &x, &y);
            xn = x / gm->srcwidth;
            yn = y / gm->srcheight - 0.5f;
            newx = x - gm->srcwidth * (2 * xn - 1) * (a * yn * yn + b * yn + c);
            geomesh_setx(gm, meshrow, meshcol, newx);
        }
    }

    return WARPLIB_SUCCESS;
}



void roll_spherical_axis(float xy_plane_angle, float z_axis_angle, float *new_plane_angle, float *new_axis_angle)
{
    float x,y,z;
    x = sinf(xy_plane_angle)*sinf(z_axis_angle);
    y = sinf(xy_plane_angle)*cosf(z_axis_angle);
    z = cosf(xy_plane_angle);

    *new_plane_angle = acosf(y);
    *new_axis_angle = atan2f(z, x);
}


float EstimateNormalizedRadius(float dphi, float k6, float k5, float k4, float k3, float k2, float k1, float accuracy)
{
	float r = 0, estphi, last_estphi = 0;
	float step = 0.1f;
	int count;

	last_estphi = k6*r*r*r*r*r*r + k5*r*r*r*r*r + k4*r*r*r*r + k3*r*r*r + k2*r*r + k1*r;
	r += step;

	for (count = 0; count < 100; count++)
	{
		estphi = k6*r*r*r*r*r*r + k5*r*r*r*r*r + k4*r*r*r*r + k3*r*r*r + k2*r*r + k1*r;

		if (estphi < dphi && estphi+accuracy > dphi)
			break;

		if (last_estphi < dphi && dphi < estphi)
		{
			r += step;
			step = -step * 0.75f; // 0.75 resolves way faster than 0.5
		}
		else if (last_estphi > dphi && dphi > estphi)
		{
			r += step;
			step = -step * 0.75f;
		}
		else if (last_estphi < dphi && estphi < last_estphi)
		{
			step = -step * 0.75f;
			r += step;
		}
		else if (last_estphi > dphi && estphi > last_estphi)
		{
			step = -step * 0.75f;
			r += step;
		}
		else
		{
			r += step;
		}
		last_estphi = estphi;
	}
	if (r < 0.0) r = 0.0;

	return r;
}

int geomesh_set_custom_lens(void *opaque, float *src_params, float *dst_params, int size)
{
	geomesh_t *gm = (geomesh_t *)opaque;
	
	GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

	memcpy(gm->lensCustomSRC, src_params, size <= sizeof(gm->lensCustomSRC) ? size : sizeof(sizeof(gm->lensCustomSRC)));
	memcpy(gm->lensCustomDST, dst_params, size <= sizeof(gm->lensCustomDST) ? size : sizeof(sizeof(gm->lensCustomDST)));

	return WARPLIB_SUCCESS;
}

int geomesh_transform_repoint_src_to_dst(void *opaque, float sensorcrop, float newphi, float newtheta, float newphi2, int srclens, int dstlens)
{
    geomesh_t *gm = (geomesh_t *)opaque;
    float x, y;
    float src_center_x;
    float src_center_y;
    int   meshrow, meshcol;

    float phi, dphi, theta, radius, r, normalized_radius;
    float maxradius;

    GEOMESH_CHECK(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    maxradius = sqrtf((gm->srcwidth * gm->srcwidth + gm->srcheight * gm->srcheight) / 4.0f);

    src_center_x = gm->srcwidth / 2.0f;
    src_center_y = gm->srcheight / 2.0f;


    if(srclens == EQUIRECT && dstlens == EQUIRECT)
    {
        newphi += (float)PI;
        newtheta += (float)HPI;
    }

    for (meshrow = 0; meshrow < gm->meshheight; meshrow++)
    {
        for (meshcol = 0; meshcol < gm->meshwidth; meshcol++)
        {
            // destination x,y
            geomesh_getxy(gm, meshrow, meshcol, &x, &y);
            x -= src_center_x;
            y -= src_center_y;

            radius = sqrtf(x * x + y * y);

            r = radius / maxradius;  // normalized_radius
            r *= sensorcrop;

            switch(dstlens)
            {
            case RECTILINEAR:
                phi = atanf(r * 1.65f);
                break;
            case FISHEYE:
                break;
            case HERO3BLACK:
				phi = (float)DEG2RAD(-12.047899f*r*r*r + 5.3339f*r*r + 80.560545f*r);  // HERO3
                break;
            case HERO3PLUSBLACK:
			case HERO4:
				if (r > 8.0) 
					phi = (float)DEG2RAD(179.0f); // 180.0 is a single point behind the virtual lens, doesn't look so good.
				else if (r > 4.0)
					//phi = (float)DEG2RAD(174.89264f*(2.0f - r*0.25) + 179.0f*(r*0.25 - 1.0f)); 
					phi = (float)DEG2RAD(175.17264f*(2.0f - r*0.25) + 179.0f*(r*0.25 - 1.0f));
				else if (r > 1.0)
					//phi = (float)DEG2RAD(-10.28871f*r*r + 84.878f*r); //HERO3+/4 lens to image sphere
					phi = (float)DEG2RAD(-10.28871f*r*r + 84.948f*r); //HERO3+/4 lens to image sphere
				else					
					phi = (float)DEG2RAD(r*r*r*r*7.5297980142f - r*r*r*17.983822059f + r*r*3.7166235179f + r*81.396558116f);
                break;
            case EQUIRECT:
                theta = (1.0f - ((x + src_center_x) / gm->destwidth)) * 2.0f * (float)PI;
                phi = ((y + src_center_y)/ gm->destheight) * (float)PI;

                theta += (float)HPI;

                if(theta > 2.0f * (float)PI)
                    theta -= 2.0f * (float)PI;
				break;
			case CUSTOM_LENS:
				phi = (float)DEG2RAD(gm->lensCustomDST[0]*r + gm->lensCustomDST[1]*r*r + gm->lensCustomDST[2]*r*r*r
					+ gm->lensCustomDST[3] * r*r*r*r + gm->lensCustomDST[4] * r*r*r*r*r /* + gm->lensCustomDST[5] * r*r*r*r*r*r*/);  //custom lens to image sphere
				break;

            }


            if(dstlens != EQUIRECT)
            {
                if(x > 0.0f)
                {
                    if(y >= 0.0f)
                        theta = atanf((float)(fabs(y)/fabs(x)));
                    else
                        theta = -atanf((float)(fabs(y)/fabs(x)));
                }
                else if(x == 0.0f)
                {
                    if(y >= 0.0f)
                        theta = (float)(HPI);
                    else
                        theta = (float)(-HPI);
                }
                else // x< 0.0
                {
                    if(y >= 0.0f)
                        theta = (float)PI - atanf((float)(fabs(y)/fabs(x)));
                    else
                        theta = (float)PI + atanf((float)(fabs(y)/fabs(x)));
                }
            }

            // repoint in spherical coordinates
            if(newtheta != 0.0f || newphi != 0.0f || newphi2 != 0.0f)
            {
                float xy_plane_angle = phi;
                float z_axis_angle = theta;

                float yz_plane_angle;
                float x_axis_angle;

                float xz_plane_angle;
                float y_axis_angle;

                roll_spherical_axis(xy_plane_angle, z_axis_angle, &yz_plane_angle, &x_axis_angle);
                x_axis_angle += newtheta;

                roll_spherical_axis(yz_plane_angle, x_axis_angle, &xz_plane_angle, &y_axis_angle);
                y_axis_angle += newphi;

                roll_spherical_axis(xz_plane_angle, y_axis_angle, &xy_plane_angle, &z_axis_angle);
                z_axis_angle += newphi2;

                phi = xy_plane_angle;
                theta = z_axis_angle;
            }


            switch(srclens)
            {
            case RECTILINEAR:
                normalized_radius = (float)(RAD2DEG(phi)/180.0f); //TODO:  Is in a HACK
                radius = maxradius * (float)normalized_radius;
                radius /= sensorcrop;

                x = cosf(theta)*radius;
                y = sinf(theta)*radius;

                x += src_center_x;
                y += src_center_y;
                break;
            case FISHEYE:
                normalized_radius = (float)RAD2DEG(phi)/180.0f; //TODO:  Is in a HACK
                radius = maxradius * (float)normalized_radius;
                radius /= sensorcrop;

                x = cosf(theta)*radius;
                y = sinf(theta)*radius;

                x += src_center_x;
                y += src_center_y;
                break;
            case HERO3BLACK:
                dphi = (float)RAD2DEG(phi);
                
				normalized_radius = EstimateNormalizedRadius(dphi, 0, 0, 0, -12.047899f, 5.3339f, 80.560545f, 0.001f); // rather using the wolfram alpha approximation
				//normalized_radius = (float)(4E-7*dphi*dphi*dphi - 1.4E-5*dphi*dphi + 0.0123*dphi);// HERO3


                radius = maxradius * (float)normalized_radius;
                radius /= sensorcrop;

                x = cosf(theta)*radius;
                y = sinf(theta)*radius;

                x += src_center_x;
                y += src_center_y;
                break;
            case HERO3PLUSBLACK:
            case HERO4:
                dphi = (float)RAD2DEG(phi);


				//phi = (float)DEG2RAD(-10.28871f*r*r + 84.878f*r);  //HERO3+/4 lens to image sphere
				phi = (float)DEG2RAD(r*r*r*r*7.5297980142f - r*r*r*17.983822059f + r*r*3.7166235179f + r*81.396558116f);

			//	normalized_radius = EstimateNormalizedRadius(dphi, 0, 0, 0, 0, -10.28871, 84.878, 0.001); // rather using the wolfram alpha approximation
                //normalized_radius = (float)(-100.0 * (sqrt(1801068721.0 - 10288710.0 * dphi) - 42439.0) / 1028871.0); // 2.4E-5*dphi*dphi + 0.0115*dphi;    //image sphere to HERO3+/4 lens

				normalized_radius = EstimateNormalizedRadius(dphi, 0, 0, 7.5297980142f, -17.983822059f, 3.7166235179f, 81.396558116f, 0.001f); // rather using the wolfram alpha approximation

                radius = maxradius * (float)normalized_radius;
                radius /= sensorcrop;

                x = cosf(theta)*radius;
                y = sinf(theta)*radius;

                x += src_center_x;
                y += src_center_y;
                break;
            case EQUIRECT:
                {
                    float hypotenuse;
                    float u,v;
                    float xx,yy,zz;

                    //phi-theta to direction
                    xx = sinf(phi)*sinf(theta);
                    yy = sinf(phi)*cosf(theta);
                    zz = cosf(phi);

                    // Calc Equirect
#if defined(_MSC_VER)
                    hypotenuse = _hypotf(yy, zz);
#else
                    hypotenuse = hypotf(yy, zz);
#endif
                    u = -atan2f(zz, yy)/(float)TWOPI + 0.5f;
                    v = atan2f(xx, hypotenuse)/(float)PI + 0.5f;

                    x = u * gm->srcwidth + gm->srcwidth/4; if(x > gm->srcwidth) x -= gm->srcwidth;
                    y = v * gm->srcheight;
                }
				break;
			case CUSTOM_LENS:

				dphi = (float)RAD2DEG(phi);

				normalized_radius = EstimateNormalizedRadius(dphi, 0.0/*gm->lensCustomSRC[5]*/, gm->lensCustomSRC[4], gm->lensCustomSRC[3], gm->lensCustomSRC[2], gm->lensCustomSRC[1], gm->lensCustomSRC[0], 0.001f); // rather using the wolfram alpha approximation
				//normalized_radius = (float)(-100.0 * (sqrt(1801068721.0 - 10288710.0 * dphi) - 42439.0) / 1028871.0); // 2.4E-5*dphi*dphi + 0.0115*dphi;    //image sphere to HERO3+/4 lens

				radius = maxradius * (float)normalized_radius;
				radius /= sensorcrop;

				x = cosf(theta)*radius;
				y = sinf(theta)*radius;

				x += src_center_x;
				y += src_center_y;

				break;
            }

            geomesh_setxy(gm, meshrow, meshcol, x, y);
        }
	}

	if(dstlens == CUSTOM_LENS)
		geomesh_transform_pan(gm, gm->lensCustomSRC[5] * gm->srcwidth, gm->lensCustomDST[5] * gm->srcheight);

    return WARPLIB_SUCCESS;
}
