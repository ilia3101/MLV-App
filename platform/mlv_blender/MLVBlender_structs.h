#ifndef _MLVBlender_structs_h_
#define _MLVBlender_structs_h_

#include "../../src/mlv_include.h"

typedef struct
{
    /* Simple MLV info */
    mlvObject_t * mlv;
    char * file_name;
    uint64_t num_frames;
    int width, height;

    /* Info for blending */
    int offset_x;
    int offset_y;

    int crop_left, crop_right;
    int crop_top, crop_bottom;

    int feather_left, feather_right;
    int feather_top, feather_bottom;

    int visible;

    int difference_blending;

    float exposure;

} MLVBlender_mlv_t;

typedef struct {

    int mode; /* 0 = editing, 1 = exporting */

    int num_mlvs;
    MLVBlender_mlv_t * mlvs;

    /* Onlly in use when exporting */
    mlvObject_t * exporter;

    /* Blended output image, current */
    float * blended_output;
    int output_width;
    int output_height;

} MLVBlender_t;

#endif