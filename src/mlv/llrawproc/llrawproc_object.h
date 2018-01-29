/*
 * Copyright (C) 2014 David Milligan
 * Copyright (C) 2017 Magic Lantern Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#ifndef _llrawproc_object_h
#define _llrawproc_object_h

#include <sys/types.h>
#include "pixelproc.h"
#include "stripes.h"
#include "../mlv.h"

/* Low level raw processing object */
typedef struct
{
    /* flags */ 
    int fix_raw;          // apply raw fixes or not, 0=do not apply, 1=apply
    int vertical_stripes; // fix vertical stripes, 0 - do not fix", 1 - fix, 2 - compute stripes for every frame
    int compute_stripes;  // 0 = do not compute stripes, 1 = compute stripes
    int focus_pixels;     // fix focus pixels, 0 - do not fix, 1 - fix, 2 - generates focus pixel map for crop_rec mode
    int fpi_method;       // focus pixel interpolation method: 0 - mlvfs, 1 - raw2dng
    int fpm_status;       // focus pixel map status: 0 = not loaded, 1 = loaded, 2 = not exist
    int bad_pixels;       // fix bad pixels, 0 - do not fix, 1 - fix, 2 - force searching for every frame
    int bps_method;       // bad pixel search method: 0 - normal, 1 - aggresive
    int bpi_method;       // bad pixel interpolation method: 0 - mlvfs, 1 - raw2dng
    int bpm_status;       // bad pixel map status: 0 = not loaded, 1 = loaded, 2 = not exist, 3 = no bad pixels found
    int chroma_smooth;    // chroma smooth, 2 - cs2x2, 3 cs3x3, 5 - cs5x5
    int pattern_noise;    // fix pattern noise (0, 1)
    int deflicker_target; // deflicker value
    int first_time;       // controls some events which should occur only once per object instance
    int dual_iso;         // use dualiso processing, 0 - do not use, 1 - full 20 bit processing (high quality, slow), 2 - preview mode (low quality)
    int is_dual_iso;      // flag indicateing that this raw data is really dual_iso
    int diso_averaging;   // dual iso interpolation method, 0 - amaze-edge, 1 - mean23
    int diso_alias_map;   // flag for Alias Map switchin on/off
    int diso_frblending;  // flag for Fullres Blending switching on/off
    int dark_frame;       // flag for Dark Frame subtraction mode 0 = off, 1 = ext, 2 = int

    /* external dark frame file name */
    char * dark_frame_filename;
    /* external dark frame block header */
    mlv_dark_hdr_t dark_frame_hdr;
    /* external dark frame buffer pointer and its size */
    uint16_t * dark_frame_data;
    uint32_t dark_frame_size;

    /* LUTs */
    int * raw2ev;
    int * ev2raw;

    /* pixel maps */
    pixel_map focus_pixel_map;
    pixel_map bad_pixel_map;

    /* stripe corrections */
    stripes_correction stripe_corrections;

} llrawprocObject_t;

#endif
