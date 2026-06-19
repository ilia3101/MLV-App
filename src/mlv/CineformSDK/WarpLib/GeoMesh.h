/*! @file GeoMesh.h

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
#ifndef GEOMESH_H
#define GEOMESH_H

#include <stdio.h>
#include <stdint.h>

// Creates a sparse 2D mesh of points. Useful for describing geometric
// transforms, where the source pixels for resampling are found by
// interpolating the sparse points in the mesh. The mesh is indexed by
// (row,col) coordinates in the range [0.0 .. destination height] and [0.0 ..
// destination width] where 0.0 represents the left column and top row.

// Because the mesh nodes values contain the x,y coordinates in the source
// image, transforms on the mesh should be applied in reverse order (last
// transform is specified first). The mental model for this is that the mesh
// itself is destination oriented, so the way to stack transforms is to
// formualte each transform in the reverse order (i.e., from the perspective
// that the node values contain source locations for the destination location),
// and then to take each node value as a destination for a previous transform
// and to apply that transform to the node value.

// An ease-of-use interface layer could be written that allows users to submit
// the transforms in forward order, then enumerates these in reverse order to
// create the mesh, but that little enhancement will be left to another day.
// Function return values

// return codes
#define WARPLIB_SUCCESS                         0x00000000
#define WARPLIB_ERROR                           0x00000001
#define WARPLIB_ERROR_OBJECT_UNALLOCATED        0x00000002
#define WARPLIB_ERROR_OBJECT_UNINITIALIZED      0x00000004
#define WARPLIB_ERROR_MESH_UNALLOCATED          0x00000008
#define WARPLIB_ERROR_MESH_UNINITIALIZED        0x00000010
#define WARPLIB_ERROR_CACHE_UNALLOCATED         0x00000020
#define WARPLIB_ERROR_CACHE_UNINITIALIZED       0x00000040
#define WARPLIB_ERROR_UNSUPPORTED_FORMAT        0x00000080
#define WARPLIB_ERROR_UNSUPPORTED_CONVERSION    0x00000100
#define WARPLIB_NOP                             0x00000200

// supported image formats
#define WARPLIB_FORMAT_2vuy            0x32767579
#define WARPLIB_FORMAT_YUY2            0x59555932
#define WARPLIB_FORMAT_422YpCbCr8      2
#define WARPLIB_FORMAT_32BGRA          3
#define WARPLIB_FORMAT_64ARGB          4
#define WARPLIB_FORMAT_WP13			   0x57503133
#define WARPLIB_FORMAT_W13A			   0x57313341
#define WARPLIB_FORMAT_RG48            0x52473438
//#define WARPLIB_FORMAT_YU64            0x59553634
//#define WARPLIB_FORMAT_DPX0            0x44505830

// do I need to support these formats?
//#define WARPLIB_FORMAT_422YpCbCr10     8
//#define WARPLIB_FORMAT_4444YpCbCrA32   9

// used in geomesh_calculate_scale()
// BEST_FIT finds the largest center-biased rectange of the
//          same aspect ratio as the frame
// PRESERVE_VERTICAL finds the scale factor to preserve 
//          information along the middle column of the frame
// PRESERVE_HORIZONTAL finds the scale factor to preserve 
//          information along the middle row of the frame
#define WARPLIB_ALGORITHM_BEST_FIT            0
#define WARPLIB_ALGORITHM_PRESERVE_VERTICAL   1
#define WARPLIB_ALGORITHM_PRESERVE_HORIZONTAL 2
#define WARPLIB_ALGORITHM_PRESERVE_EVERYTHING 3

// C interface
//
// This library is responsible for all memory allocation / deletion.
// The user calls create() and gets an opaque pointer (that may itself
// point to other heap memory blocks so the caller should not delete
// this pointer directly).
//
// Typical usage:
// void *mesh;
// mesh = geomesh_create(40, 30);
// geomesh_init(mesh, 1920, 1080, 1020, 3, 1280, 720, 3, 1280);
// geomesh_scale(mesh, 1280.0 / 1920.0, 720.0 / 1080.0);
// geomesh_fisheye(mesh, -45.0);
// geomesh_cache_init_bilinear(mesh);
// geomesh_apply_bilinear(img_src, img_dest);
// geomesh_destroy(mesh);


// Interface to the codec library for use with either C or C++
#ifdef __cplusplus
extern "C" {
#endif

// create a geomesh "object"
void * geomesh_create(int meshwidth, int meshheight);

// output displacement map
int geomesh_generate_displacement_map(void *opaque, int w, int h, float *displacementMap);

// create a geomesh "object"
void * geomesh_clone(void *opaque);

// destroy a geomesh object - this is the only way a caller should release an object
void geomesh_destroy(void *opaque);

// copy mesh details from one object to another
int geomesh_copy(void *opaque_src, void *opaque_dest);

// resize the underlying mesh
int geomesh_resize(void *opaque, int meshwidth, int meshheight);

// for debugging - write some mesh info to the specified file
void geomesh_dump(void *opaque, FILE *fp);

// initialize the mesh - requires details about the source and dest image:
//   width and height in units of pixels
//   row stride in units of bytes
//   format (one of WARPLIB_FORMAT_*)
int geomesh_init(void *opaque, int srcwidth, int srcheight, int srcstride, int srcformat,
                               int destwidth, int destheight, int deststride, int destformat,
							   int backgroundfill);

// re-initialize (same as init but don't have to specify any parameters)
int geomesh_reinit(void *opaque);

int geomesh_get_src_info(void *opaque, int *width, int *height, int *stride, int *bpp);
int geomesh_get_dest_info(void *opaque, int *width, int *height, int *stride, int *bpp);

// get and set x and y mesh values
float geomesh_getx(void *opaque, int meshrow, int meshcol);
void  geomesh_setx(void *opaque, int meshrow, int meshcol, float x);
float geomesh_gety(void *opaque, int meshrow, int meshcol);
void  geomesh_sety(void *opaque, int meshrow, int meshcol, float y);
void  geomesh_getxy(void *opaque, int meshrow, int meshcol, float *x, float *y);
void  geomesh_setxy(void *opaque, int meshrow, int meshcol, float x, float y);

// initialize the geomesh cache - this is required before you apply the mesh to an image
int geomesh_cache_init_bilinear(void *opaque);
int geomesh_cache_init_bilinear_range(void *opaque, int start, int stop);
int geomesh_cache_init_bilinear_range_vertical(void *opaque, int start, int stop);
int geomesh_cache_init_bilinear_2vuy(void *opaque);

//
// transforms
//

int geomesh_transform_scale(void *opaque, float rowscale, float colscale);
int geomesh_transform_pan(void *opaque, float left, float top);
int geomesh_transform_fisheye(void *opaque, float max_theta_degrees);
int geomesh_transform_gopro_to_rectilinear(void *opaque, float sensorcrop);
int geomesh_transform_defish(void *opaque, float fov);
int geomesh_transform_rotate(void *opaque, float angle_degrees);
int geomesh_transform_orthographic(void *opaque, float max_angle_degrees);
int geomesh_transform_stereographic(void *opaque, float max_angle_degrees);
int geomesh_transform_flip_horz(void *opaque);
int geomesh_transform_flip_vert(void *opaque);
int geomesh_transform_horizontal_stretch_poly(void *opaque, float a, float b, float c);

#define RECTILINEAR		0
#define FISHEYE			1
#define HERO3BLACK		2
#define HERO3PLUSBLACK	3
#define HERO4			4
#define LENS_UNUSED		16	
#define EQUIRECT		32
#define CUSTOM_LENS		33

int geomesh_transform_repoint_src_to_dst(void *opaque, float sensorcrop, float phi, float theta, float phi2, int srclens, int dstlens);

// determine the scale factor such that we preserve the largest window of data
int geomesh_calculate_scale(void *opaque, int algorithm, float *scale);

// returns proper fisheye and scaling factor to make a GoPro camera view rectilinear
int geomesh_fisheye_gopro_calculate(int width, int height, int product, int model, int lens_type, int fov, float *angle);
int geomesh_fisheye_gopro_adjustmesh(void *opaque, int *correction_mode, int scaling_algorithm, int width, int height, int product, int model, int lens_type, int fov, int decode_scale);

int geomesh_equirect_gopro_adjustmesh(void *opaque, int *correction_mode, int scaling_algorithm, int width, int height, int product, int model, int lens_type, int fov);

int geomesh_blur_vertical_range(void *opaque, int colStart, int colStop,  uint8_t *output, 	int pitch);   
int geomesh_set_custom_lens(void *opaque, float *src_params, float *dst_params, int size);

int geomesh_apply_bilinear(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);
int geomesh_apply_bilinear_separable(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);

int geomesh_apply_bilinear_yuy2(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);
int geomesh_apply_bilinear_2vuy(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);
int geomesh_apply_bilinear_422YpCbCr8(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);
int geomesh_apply_bilinear_32BGRA(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);
int geomesh_apply_bilinear_64ARGB(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);
int geomesh_apply_bilinear_RG48(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);
int geomesh_apply_bilinear_YU64(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);
int geomesh_apply_bilinear_WP13(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);
int geomesh_apply_bilinear_W13A(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);
//int geomesh_apply_bilinear_DPX0(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);

int geomesh_apply_bilinear_2vuy_yuy2(void *opaque, unsigned char *src, unsigned char *dest, int row0, int row1);


#ifdef __cplusplus
}
#endif



#ifdef __cplusplus

// C++ interface
//
// GeoMesh mesh(rows, cols);
// mesh.resize(newrows, newcols); // optional, new mesh size, erases all previous settings
// mesh.init_bilinear(srcw, srch, srcstride, 3, dstw, dsth, dststride, 3);
// mesh.pan(x,y,h,w); // crops input
// mesh.scale(xscale, yscale); // linearly scales: zoom in if > 1
// mesh.rotate(degrees);
// mesh.fisheye(-45.0f); // remove fisheye lens distortion
// mesh.fisheye(45.0f); // add fisheye lens distortion
// mesh.apply_bilinear(img_src, img_dst, 0, h);

class Mesh
{
public:

    Mesh();
    Mesh(int meshwidth, int meshheight);
    virtual ~Mesh();

    int init(int srcwidth, int srcheight, int srcstride, int srcbpp, int destwidth, int destheight, int deststride, int destbpp);
    void resize(int meshwidth, int meshheight);

    int cache_init_bilinear(void);

    // copy from src mesh to this one
    void transfer(Mesh &src);

    // getters and setters
    void get_src_info(int &width, int &height, int &stride, int &bpp);
    void get_dest_info(int &width, int &height, int &stride, int &bpp);

    float getx (int meshrow, int meshcol);
    float gety (int meshrow, int meshcol);
    void  getxy(int meshrow, int meshcol, float &x, float &y);
    void  setx (int meshrow, int meshcol, float x);
    void  sety (int meshrow, int meshcol, float y);
    void  setxy(int meshrow, int meshcol, float x, float y);

    int interp_bilinear(float row, float col, float &x, float &y);

    void dump(FILE *fp = stdout);

protected:

    void *opaque;
};

class GeoMesh : public Mesh
{
public:
    GeoMesh();
    GeoMesh(int meshwidth, int meshheight);
    virtual ~GeoMesh();

    int scale(float rowscale, float colscale);
    int pan(float left, float top);
    int rotate(float angle_degrees);
    int fisheye(float max_theta_degrees);
    int flip_horz(void);
    int flip_vert(void);
    int orthographic(float max_theta_degrees);
    int stereographic(float max_theta_degrees);
};

#endif /* __cplusplus */


#endif /* GEOMESH_H */
