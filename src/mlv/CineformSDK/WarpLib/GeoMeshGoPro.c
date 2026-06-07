/*! @file GeoMeshGoPro.c

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

#include "GeoMesh.h"
#include "GeoMeshPrivate.h"

int geomesh_fisheye_gopro_calculate(int width, int height, int product, int model, int lens_type, int fov, float *angle)
{
    // default to doing nothing (fail-safe if we get confused)
    *angle = 0.0f;

    if (product < 0 || (product == 3 && model < 0) || (product != 1 && fov < 0))
        return WARPLIB_ERROR;

    // these are open-air (no case) values
    switch(product)
    {
        case 1: // Hero 1
            // Note: we can't recognize a Hero1 camera uniquely in video modes (only still modes)
            if (height == 1944 && width == 2592) // 5mpix still/timelapse
                *angle = 54.0f;
            break;
        case 2: // Hero 2
            switch (fov)
            {
                case 0: // wide
                    if (height == 2880 && width == 3840) // 11mpix still/timelapse
                        *angle = 54.0f;
                    else if (height == 1944 && width == 2592) // 5mpix still/timelapse
                        *angle = 53.4f;
                    else if (height == 1080)
                        *angle = 49.0f;
                    else if (height == 960)
                        *angle = 50.0f;
                    else if (height == 720)
                        *angle = 50.0f;
                    else if (height == 480)
                        *angle = 44.0f;
                    break;
                case 1: // medium
                    if (height == 2400 && width == 3200)      // 8mpix still/timelapse
                        *angle = 48.0f;
                    else if (height == 1944 && width == 2592) // 5mpix still/timelapse
                        *angle = 41.0f;
                    else if (height == 1080)
                        *angle = 43.0f;
                    break;
                case 2: // narrow
                    if (height == 1080)
                        *angle = 30.0f;
                    break;
            }
            break;
        case 3: // Hero 3
            {
                switch(model)
                {
                    case 1: // H3 white
                    case 9: // H3+ white (white update issued around the 3+ timeframe)
                        {
                            switch(fov)
                            {
                                case 0: // wide
                                    if (height == 1944 && width == 2592) // 5mpix still/timelapse
                                        *angle = 52.0f;
                                    else if (height == 1080)
                                        *angle = 41.0f;
                                    else if (height == 960)
                                        *angle = 52.0f;
                                    else if (height == 720 || height == 480)
                                        *angle = 49.5f;
                                    break;
                                case 1: // medium
                                    // no modes
                                    break;
                                case 2: // narrow
                                    // no modes
                                    break;
                                default:
                                    return WARPLIB_ERROR;
                                    break;
                            }
                        }
                        break;
                    case 2: // H3 silver
                        {
                            switch(fov)
                            {
                                case 0: // wide
                                    if (height == 2880 && width == 3840)      // 11mpix still/timelapse
                                        *angle = 49.0f;
                                    else if (height == 1944 && width == 2592) // 5mpix still/timelapse
                                        *angle = 49.0f;
                                    else if (height == 1080)
                                        *angle = 49.0f;
                                    else if (height == 960)
                                        *angle = 50.0f;
                                    else if (height == 720)
                                        *angle = 50.0f;
                                    else if (height == 480)
                                        *angle = 44.0f;
                                    break;
                                case 1: // medium
                                    if (height == 2400 && width == 3200) // 8mpix still/timelapse
                                        *angle = 45.0f;
                                    else if (height == 1944 && width == 2592) // 5mpix still/timelapse
                                        *angle = 40.0f;
                                    else
                                        *angle = 43.0f;
                                    break;
                                case 2: // narrow
                                    // only one mode (1080)
                                    *angle = 30.0f;
                                    break;
                                default:
                                    return WARPLIB_ERROR;
                                    break;
                            }
                        }
                        break;
                    case 3: // H3 black
                        {
                            switch(fov)
                            {
                                case 0: // wide
                                    if (height == 3000 && width == 4000)      // 12mpix still/timelapse
                                        *angle = 52.0f;
                                    else if (height == 2250 && width == 3000) // 7mpix still/timelapse
                                        *angle = 50.0f;
                                    else if (height == 2160 && width == 4096)
                                        *angle = 50.0f;
                                    else if (height == 2160 && width == 3840)
                                        *angle = 48.0f;
                                    else if (height == 1524)
                                        *angle = 46.0f;
                                    else if (height == 1440 && width == 2716)
                                        *angle = 50.5f;
                                    else if (height == 1440 && width == 1920)
                                        *angle = 51.0f;
                                    else if (height >= 1080)
                                        *angle = 48.0f;
                                    else if (height == 960)
                                        *angle = 51.0f;
                                    else if (height == 720)
                                        *angle = 48.0f;
                                    else if (height == 480)
                                        *angle = 48.0f;
                                    break;
                                case 1: // medium
                                    if (height == 2250 && width == 3000)      // 7mpix still/timelapse
                                        *angle = 42.0f;
                                    else if (height == 1920 && width == 2560) // 5mpix still/timelapse
                                        *angle = 42.0f;
                                    else if (height == 1080)
                                        *angle = 42.0f;
                                    else if (height == 720)
                                        *angle = 43.0f;
                                    break;
                                case 2: // narrow (720 and 1080)
                                    *angle = 33.0f;
                                    break;
                                default:
                                    return WARPLIB_ERROR;
                                    break;
                            }
                        }
                        break;
                    case 10: // H3+ Silver
                        {
                            switch(fov)
                            {
                                case 0: // wide
                                    if (height == 2760 && width == 3680)      // 10mpix still/timelapse
                                        *angle = 49.0f;
                                    else if (height == 2304 && width == 3072) // 7mpix still/timelapse
                                        *angle = 48.0f;
                                    else if (height == 1944 && width == 2592) // 5mpix still/timelapse
                                        *angle = 46.0f;
                                    else if (height == 1080)
                                        *angle = 49.0f;
                                    else if (height == 960)
                                        *angle = 49.0f;
                                    else if (height == 720)
                                        *angle = 50.0f;
                                    else if (height == 480)
                                        *angle = 48.0f;
                                    break;
                                case 1: // medium
                                    if (height == 2400 && width == 3200) // 8mpix still/timelapse
                                        *angle = 45.0f;
                                    else if (height == 1968 && width == 2624) // 5mpix still/timelapse
                                        *angle = 41.0f;
                                    else
                                        *angle = 41.0f;
                                    break;
                                case 2: // narrow
                                    // only one mode (1080)
                                    *angle = 30.0f;
                                    break;
                                default:
                                    return WARPLIB_ERROR;
                                    break;
                            }
                        }
                        break;
                    case 11: // H3+ black
                        {
                            switch(fov)
                            {
                                case 0: // wide
                                    if (height == 3000 && width == 4000)      // 12mpix still/timelapse
                                        *angle = 51.0f;
                                    else if (height == 2250 && width == 3000) // 7mpix still/timelapse
                                        *angle = 51.0f;
                                    else if (height == 2160 && width == 4096)
                                        *angle = 49.0f;
                                    else if (height == 2160 && width == 3840)
                                        *angle = 47.5f;
                                    else if (height == 1524)
                                        *angle = 47.5f;
                                    else if (height == 1440 && width == 2704)
                                        *angle = 49.0f;
                                    else if (height == 1440 && width == 2716)
                                        *angle = 47.5f;
                                    else if (height == 1440 && width == 1920)
                                        *angle = 51.0f;
                                    else if (height >= 1080)
                                        *angle = 48.0f;
                                    else if (height == 960)
                                        *angle = 51.5f;
                                    else if (height == 720)
                                        *angle = 47.4f;
                                    else if (height == 480)
                                        *angle = 48.2f;
                                    break;
                                case 1: // medium
                                    if (height == 2250 && width == 3000)      // 7mpix still/timelapse
                                        *angle = 44.0f;
                                    else if (height == 1920 && width == 2560) // 5mpix still/timelapse
                                        *angle = 43.0f;
                                    else if (height == 1524)
                                        *angle = 38.0f;
                                    else if (height == 1440)
                                        *angle = 38.0f;
                                    else if (height == 1080)
                                        *angle = 41.0f;
                                    else if (height == 720)
                                        *angle = 41.0f;
                                    break;
                                case 2: // narrow (720 and 1080)
                                    *angle = 31.0f;
                                    break;
                                case 3: // superview (720 and 1080)
                                    if (height == 1080)
                                        *angle = 49.0f;
                                    else if (height == 720)
                                        *angle = 49.2f;
                                    break;
                                default:
                                    return WARPLIB_ERROR;
                                    break;
                            }
                        }
                        break;
                    case 16: // HERO 4 Session
                    case 20: // HERO
                    case 21: // Hero+ LCD
                    case 22: // Hero+ WiFi
                        {
                            switch(fov)
                            {
                                case 0: // wide
                                    if (height == 2448 && width == 3264)      // Hero+ WiFi still
                                        *angle = 51.0f;
                                    else if (height == 1944 && width == 2592) // 5mpix still/timelapse
                                        *angle = 51.0f;
                                    else if (height == 1080)
                                        *angle = 48.7f;
                                    else if (height == 1440)
                                        *angle = 51.0f; 
                                    else if (height == 960)
                                        *angle = 52.0f;
                                    else if (height == 720 || height == 480)
                                        *angle = 49.0f;
                                    break;
                                case 1: // medium
                                    if (height == 2448 && width == 3264)      // Hero+ WiFi still
                                        *angle = 50.0f;
                                    else if (height == 2040 && width == 2720) // 5mpix still/timelapse
                                        *angle = 35.6f;
                                    else if (height == 1080)
                                        *angle = 38.5f;
                                    else if (height == 720)
                                        *angle = 38.5f;
                                    break;
                                case 2: // narrow
                                    if (height == 1944 && width == 2592)
                                        *angle = 51.0f;
                                    break;
                                case 3: // superview
                                    if (height == 720)
                                        *angle = 46.0f;
                                    break;
                                default:
                                    return WARPLIB_ERROR;
                                    break;
                            }
                        }
                        break;
                    default:
                        return WARPLIB_ERROR;
                        break;
                }
            }
            break;
        case 4: // Hero 4
            {
                switch(model)
                {
                    case 1: // Hero4 Silver 
                        {
                            switch(fov)
                            {
                                case 0: // wide
                                    if (height == 3000 && width == 4000)      // 12mpix still/timelapse
                                        *angle = 51.5f;
                                    else if (height == 2250 && width == 3000) // 7mpix still/timelapse
                                        *angle = 51.5f;
                                    else if (height == 2160 && width == 4096)
                                        *angle = 50.0f;
                                    else if (height == 2160 && width == 3840)
                                        *angle = 48.0f;
                                    else if (height == 1524)
                                        *angle = 46.0f;
                                    else if (height == 1440 && width == 2716)
                                        *angle = 50.5f;
                                    else if (height == 1440 && width == 1920)
                                        *angle = 51.0f;
                                    else if (height >= 1080)
                                        *angle = 48.0f;
                                    else if (height == 960)
                                        *angle = 51.0f;
                                    else if (height == 720)
                                        *angle = 48.0f;
                                    else if (height == 480)
                                        *angle = 48.0f;
                                    break;
                                case 1: // medium
                                    if (height == 3000 && width == 4000)      // 12mpix still/timelapse WHY DOES MAC THINK THIS IS MED FOV??
                                        *angle = 51.5f;
                                    else if (width == 2704)
                                        *angle = 38.0f;
                                    else if (height == 2250 && width == 3000)      // 7mpix still/timelapse
                                        *angle = 42.75f;
                                    else if (height == 1920 && width == 2560) // 5mpix still/timelapse
                                        *angle = 42.75f;
                                    else if (height == 1080)
                                        *angle = 41.0f;
                                    else if (height == 720)
                                        *angle = 41.0f;
                                    break;
                                case 2: // narrow (720 and 1080)
                                    *angle = 33.0f;
                                    break;
                                case 3: // superview
                                    if (height == 1080)
                                        *angle = 49.0f;
                                    else if (height == 720)
                                        *angle = 49.0f;
                                    break;
                            }
                        }
                        break;
                    case 2: // Hero4 Black 
                        {
                            switch(fov)
                            {
                                case 0: // wide
                                    if (height == 3000 && width == 4000)
                                        *angle = 51.75f;
                                    else if (height == 2250 && width == 3000)
                                        *angle = 51.75f;
                                    else if (height == 1920 && width == 2560)
                                        *angle = 51.75f;
                                    else if (height == 2160)
                                        *angle = 49.0f;
                                    else if (height == 2028 || height == 2032) // some confusion about height interpretation
                                        *angle = 51.5f;
                                    else if (height == 1520)
                                        *angle = 48.0f;
                                    else if (height == 1440)
                                        *angle = 51.0f;
                                    else if (height == 1080)
                                        *angle = 48.0f;
                                    else if (height == 960)
                                        *angle = 51.3f;
                                    else if (height == 720)
                                        *angle = 48.0f;
                                    else if (height == 480)
                                        *angle = 48.0f;
                                    break;
                                case 1: // medium
                                    if (height == 2400 && width == 3200) // 8mpix still/timelapse
                                        *angle = 45.0f;
                                    else if (height == 1944 && width == 2592) // 5mpix still/timelapse
                                        *angle = 40.0f;
                                    else if (height == 2160)
                                        *angle = 48.0f;
                                    else if (height == 1520)
                                        *angle = 38.5f;
                                    else if (height == 1080)
                                        *angle = 41.0f;
                                    else if (height == 720)
                                        *angle = 38.5f;
                                    else if (height == 480)
                                        *angle = 38.5f;
                                    else
                                        *angle = 43.0f;
                                    break;
                                case 2: // narrow
                                    // only one mode (1080)
                                    *angle = 30.0f;
                                    break;
                                case 3: // superview
                                    if (height == 2160)
                                        *angle = 48.0f;
                                    else if (height == 1520)
                                        *angle = 49.0f;
                                    else if (height == 1440)
                                        *angle = 51.0f;
                                    else if (height == 1080)
                                        *angle = 48.0f;
                                    else if (height == 960)
                                        *angle = 51.3f;
                                    else if (height == 720)
                                        *angle = 50.0f;
                                    else if (height == 480)
                                        *angle = 48.0f;
                                    break;
                                default:
                                    return WARPLIB_ERROR;
                                    break;
                            }
                        }
                        break;
                    default:
                        return WARPLIB_ERROR;
                        break;
                }
            }
            break;
        default:
            return WARPLIB_ERROR;
            break;
    }

    return WARPLIB_SUCCESS;
}

int geomesh_fisheye_gopro_adjustmesh(void *opaque, int *correction_mode, int scaling_algorithm, int width, int height, int product, int model, int lens_type, int fov, int decode_scale)
{
    int status = WARPLIB_SUCCESS;
    float angle = 0.0f;
    float scale = 1.0f;
    int fullw, fullh;
    geomesh_t *mesh = (geomesh_t *)opaque;
    //geomesh_t *temp;

    GEOMESH_CHECK(mesh, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);

    if (*correction_mode != 2)
        return WARPLIB_ERROR_UNSUPPORTED_CONVERSION;


    fullw = width;
    fullh = height;

    if(decode_scale == 2)
        fullw*=2, fullh*=2;
    if(decode_scale == 3)
        fullw*=4, fullh*=4;


    if (fov == 3) // superwide mode, needs its own custom mesh warp
    {
        status = geomesh_fisheye_gopro_calculate(fullw, fullh, product, model, lens_type, fov, &angle);
        if (angle < -0.1f || angle > 0.1f)
        {
/*            temp = (geomesh_t *)geomesh_clone(mesh);
            status |= geomesh_transform_scale(temp, 1.33f, 1.0f);
            status |= geomesh_transform_fisheye(temp, -angle);
            status |= geomesh_transform_scale(temp, 0.5f, 0.5f);
            status |= geomesh_calculate_scale(temp, scaling_algorithm, &scale);
            geomesh_destroy(temp);
            scale /= 2.0f;
*/
            status |= geomesh_transform_scale(mesh, 1.33f, 1.0f);
            status |= geomesh_transform_fisheye(mesh, -angle);
            status |= geomesh_transform_scale(mesh, scale, scale);
            status |= geomesh_transform_horizontal_stretch_poly(mesh, 0.21f, 0.0f, 0.0f);
        }
    }
    else // typical fisheye correction
    {
        status = geomesh_fisheye_gopro_calculate(fullw, fullh, product, model, lens_type, fov, &angle);
        if (angle < -0.1f || angle > 0.1f)
        {
       /*     temp = (geomesh_t *)geomesh_clone(mesh);
            status |= geomesh_reinit(temp);
            status |= geomesh_transform_scale(temp, 0.5f, 0.5f);
            status |= geomesh_transform_fisheye(temp, -angle);
            status |= geomesh_calculate_scale(temp, scaling_algorithm, &scale);
            geomesh_destroy(temp);
            scale /= 2.0f; */

         //   status |= geomesh_transform_scale(mesh, scale, scale);
            status |= geomesh_transform_fisheye(mesh, -angle);
        }
    }

#if 0
    FILE *fp = fopen("c:\\Temp\\debug_lens.txt", "a");
    fprintf(fp, "mode=%d algo=%d width=%d height=%d product=%d model=%d lens_type=%d fov=%d angle=%6.2f status=%d\n", *correction_mode, scaling_algorithm, width, height, product, model, lens_type, fov, angle, status);
    fclose(fp);
#endif
    return status;
}

/*  // should have been removed
int geomesh_equirect_gopro_adjustmesh(void *opaque, int *correction_mode, int scaling_algorithm, int width, int height, int product, int model, int lens_type, int fov)
{
    int status = WARPLIB_SUCCESS;
    geomesh_t *mesh = (geomesh_t *)opaque;
    
    GEOMESH_CHECK(mesh, GEOMESH_CHECK_OBJ_EXISTS | GEOMESH_CHECK_MESH_EXISTS | GEOMESH_CHECK_MESH_INITIALIZED);
    
    status |= geomesh_transform_equirect(mesh, 0.90);
    
#if 0
    FILE *fp = fopen("c:\\Temp\\debug_lens.txt", "a");
    fprintf(fp, "mode=%d algo=%d width=%d height=%d product=%d model=%d lens_type=%d fov=%d angle=%6.2f status=%d\n", *correction_mode, scaling_algorithm, width, height, product, model, lens_type, fov, angle, status);
    fclose(fp);
#endif
    return status;
}
*/

