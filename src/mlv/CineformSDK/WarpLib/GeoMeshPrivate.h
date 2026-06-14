/*! @file GeoMeshPrivate.h

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
#ifndef GEOMESH_PRIV_H
#define GEOMESH_PRIV_H

//
// private interface for functions internal to the library
//

// this struct is what is given to users as the opaque pointer

typedef struct geomesh_tag
{
    unsigned int srcformat;   // source image format
    int          srcwidth;    // pixels
    int          srcheight;   // pixels
    int          srcstride;   // bytes
    int          srcbpp;      // bytes per pixel (implied from format)
    int          srcsubsampled;      // 0 - not, 1 - 4:2:2
    int          srcchannels; // channels (implied from format)
	int			  srcsigned;  // signed data pixel format
    unsigned int destformat;  // dest image format
    int          destwidth;   // pixels
    int          destheight;  // pixels
    int          deststride;  // bytes
    int          destbpp;     // bytes per pixel (implied from format)
    int          destchannels;// channels (implied from format)
    int          destsubsampled;      // 0 - not, 1 - 4:2:2
	int			 destsigned;  // signed data pixel format
    int          meshwidth;   // number of mesh points in x direction
    int          meshheight;  // number of mesh points in y direction
    int          separable;   // only some transforms result in a separable mesh
    int          backgroundfill;   // 0 - black, 1 - extend edges
	float		 lensCustomSRC[6];  // Custom lens curve
	float		 lensCustomDST[6];  // Custom lens curve

    float       *meshx;       // store mesh node values
    float       *meshy;
    int         *cache;       // store image-sized acceleration cache
    // internal consistency metadata
    char         signature[8];
    char         mesh_allocated;
    char         mesh_initialized;
    char         num_elements_allocated;
    char         cache_initialized;
    
    float        xstep; // step to bridge the discrepancy between the destination width and the mesh width
    float        ystep; // step to bridge the discrepancy between the destination height and the mesh height

} geomesh_t;

int geomesh_alloc_mesh(geomesh_t *gm);
int geomesh_dealloc_mesh(geomesh_t *gm);
int geomesh_alloc_cache(geomesh_t *gm);
int geomesh_dealloc_cache(geomesh_t *gm);

// debugging facilities

#define GEOMESH_SIGNATURE "GeoMesh"

#define GEOMESH_CHECK_OBJ_EXISTS         0x01
#define GEOMESH_CHECK_MESH_EXISTS        0x02
#define GEOMESH_CHECK_MESH_INITIALIZED   0x04
#define GEOMESH_CHECK_CACHE_EXISTS       0x08
#define GEOMESH_CHECK_CACHE_INITIALIZED  0x10

#ifdef DEBUG
#define GEOMESH_CHECK(gm,t)    geomesh_check(gm, t)
#define CHECK_OBJ_EXISTS(gm)   geomesh_check(gm, GEOMESH_CHECK_OBJ_EXISTS)
#define CHECK_MESH_EXISTS(gm)  geomesh_check(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS)
#define CHECK_MESH_INIT(gm)    geomesh_check(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED)
#define CHECK_CACHE_EXISTS(gm) geomesh_check(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_CACHE_EXISTS)
#define CHECK_CACHE_INIT(gm)   geomesh_check(gm, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_CACHE_EXISTS | GEOMESH_CHECK_CACHE_INITIALIZED)
#else
#define GEOMESH_CHECK(gm,t)    (void)0
#define CHECK_OBJ(gm)          (void)0
#define CHECK_MESH_EXISTS(gm)  (void)0
#define CHECK_MESH_INIT(gm)    (void)0
#define CHECK_CACHE_EXISTS(gm) (void)0
#define CHECK_CACHE_INIT(gm)   (void)0
#endif

int geomesh_check(void *opaque, unsigned int check_type);

#endif /* GEOMESH_PRIV_H */

