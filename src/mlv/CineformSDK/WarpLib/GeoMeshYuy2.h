/*! @file GeoMeshYuy2.h

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
#ifndef GEOMESH_YUY2_H
#define GEOMESH_YUY2_H

#include "GeoMesh.h"

int geomesh_cache_init_bilinear_yuy2(void *opaque);

#ifdef __cplusplus

class GeoMeshYuy2 : public GeoMesh
{
public:

    GeoMeshYuy2()
    {
    }

    GeoMeshYuy2(int meshrows, int meshcols)
        : GeoMesh(meshrows, meshcols)
    {
    }

    virtual ~GeoMeshYuy2()
    {
    }

    int cache_init_bilinear(void);
    int apply_bilinear(unsigned char *src, unsigned char *dest, int row0, int row1);

};

#endif /* __cplusplus */

#endif /* GEOMESH_YUY2_H */

