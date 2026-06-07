/*! @file GeoMeshInterp.c

*  @brief Mesh tools
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

#include "GeoMesh.h"
#include "GeoMeshPrivate.h"
#include <math.h>

//
// mesh interpolation
//

int geomesh_interp_bilinear(void *opaque, float row, float col, float *x, float *y)
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

    if (meshrow0 < 0)
    {
        meshrow0 = 0;
        ylever = 0.0;
    }

    if (meshrow0 >= gm->meshheight - 1)
    {
        meshrow0 = gm->meshheight - 2;
        ylever = 1.0;
    }

    if (meshcol0 < 0)
    {
        meshcol0 = 0;
        xlever = 0.0;
    }

    if (meshcol0 >= gm->meshwidth - 1)
    {
        meshcol0 = gm->meshwidth - 2;
        xlever = 1.0;
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

	if((fabs(x11 - x00)*2.0 > (float)gm->srcwidth) 
    || (fabs(x11 - x10)*2.0 > (float)gm->srcwidth) 
    || (fabs(x11 - x01)*2.0 > (float)gm->srcwidth) 
    || (fabs(x01 - x10)*2.0 > (float)gm->srcwidth)
    || (fabs(x01 - x00)*2.0 > (float)gm->srcwidth)
    || (fabs(x10 - x00)*2.0 > (float)gm->srcwidth)  	) // mesh entry stradles the horinzontal image edge  
	{
		float x00l = x00;
		float x00h = x00;
		float x01l = x01;
		float x01h = x01;
		float x10l = x10;
		float x10h = x10;
		float x11l = x11;
		float x11h = x11;
		float xxl, xxh;

		if(x00 < (float)(gm->srcwidth>>1))
			x00h = (float)gm->srcwidth + x00;
		else
			x00l = -((float)gm->srcwidth - x00);

		if(x01 < (float)(gm->srcwidth>>1))
			x01h = (float)gm->srcwidth + x01;
		else
			x01l = -((float)gm->srcwidth - x01);

		if(x10 < (float)(gm->srcwidth>>1))
			x10h = (float)gm->srcwidth + x10;
		else
			x10l = -((float)gm->srcwidth - x10);

		if(x11 < (float)(gm->srcwidth>>1))
			x11h = (float)gm->srcwidth + x11;
		else
			x11l = -((float)gm->srcwidth - x11);


		xxl = x00l * w00 + x01l * w01 + x10l * w10 + x11l * w11;
		xxh = x00h * w00 + x01h * w01 + x10h * w10 + x11h * w11;

	/*	if(x00*2.0 > (float)gm->destwidth)  //////////////HACK////////////////////////////////////////////TODO
			x00 = (float)gm->destwidth - x00;
		if(x01*2.0 > (float)gm->destwidth)
			x01 = (float)gm->destwidth - x01;
		if(x10*2.0 > (float)gm->destwidth)
			x10 = (float)gm->destwidth - x10;
		if(x11*2.0 > (float)gm->destwidth)
			x11 = (float)gm->destwidth - x11;
*/

		if(xxl >= 0.0)
			*x = xxl;
		else if(xxh <= (float)gm->srcwidth-1.0)
			*x = xxh;
		else
		{
			if((-xxl) > (xxh - (gm->srcwidth-1.0)))
				*x = gm->srcwidth-1.0f;
			else
				*x = 0.0;
		}

		//need to recalc  xlever/ylever
/*		if((fabs(y11 - y00)*2.0 > (float)gm->destwidth) 
		|| (fabs(y11 - y10)*2.0 > (float)gm->destwidth) 
		|| (fabs(y11 - y01)*2.0 > (float)gm->destwidth) 
		|| (fabs(y01 - y10)*2.0 > (float)gm->destwidth)
		|| (fabs(y01 - y00)*2.0 > (float)gm->destwidth)
		|| (fabs(y10 - y00)*2.0 > (float)gm->destwidth)  )
		{
			goto verticalcalc;
		}
		else */
		{
			*y = y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11;
		}
	}
/*	else if((fabs(y11 - y00)*2.0 > (float)gm->destheight) 
    || (fabs(y11 - y10)*2.0 > (float)gm->destheight) 
    || (fabs(y11 - y01)*2.0 > (float)gm->destheight) 
    || (fabs(y01 - y10)*2.0 > (float)gm->destheight)
    || (fabs(y01 - y00)*2.0 > (float)gm->destheight)
    || (fabs(y10 - y00)*2.0 > (float)gm->destheight)  	) // mesh entry stradles the vertical image edge  
	{
		float y00l;
		float y00h;
		float y01l;
		float y01h;
		float y10l;
		float y10h;
		float y11l;
		float y11h;
		float yyl, yyh;

		
		*x = x00 * w00 + x01 * w01 + x10 * w10 + x11 * w11;

verticalcalc:
		y00l = y00;
		y00h = y00;
		y01l = y01;
		y01h = y01;
		y10l = y10;
		y10h = y10;
		y11l = y11;
		y11h = y11;

		if(y00 < (float)(gm->destheight>>1))
			y00h = (float)gm->destheight + y00;
		else
			y00l = -((float)gm->destheight - y00);

		if(y01 < (float)(gm->destheight>>1))
			y01h = (float)gm->destheight + y01;
		else
			y01l = -((float)gm->destheight - y01);

		if(y10 < (float)(gm->destheight>>1))
			y10h = (float)gm->destheight + y10;
		else
			y10l = -((float)gm->destheight - y10);

		if(y11 < (float)(gm->destheight>>1))
			y11h = (float)gm->destheight + y11;
		else
			y11l = -((float)gm->destheight - y11);


		yyl = y00l * w00 + y01l * w01 + y10l * w10 + y11l * w11;
		yyh = y00h * w00 + y01h * w01 + y10h * w10 + y11h * w11;

		if(yyl >= 0.0)
			*y = yyl;
		else if(yyh <= (float)gm->destheight-1.0)
			*y = yyh;
		else
		{
			if((-yyl) > (yyh - (gm->destheight-1.0)))
				*y = gm->destheight-1.0;
			else
				*y = 0.0;
		}
	}*/
	else
	{
		*x = x00 * w00 + x01 * w01 + x10 * w10 + x11 * w11;
		*y = y00 * w00 + y01 * w01 + y10 * w10 + y11 * w11;
	}

    return WARPLIB_SUCCESS;
}



