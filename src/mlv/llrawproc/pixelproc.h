/*
 * Copyright (C) 2017 bouncyball
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

#ifndef _pixelproc_h
#define _pixelproc_h

/* pixel map type */
enum { PIX_FOCUS, PIX_BAD };

/* pixel struct */
typedef struct {
    int x;
    int y;
} pixel_xy;

/* pixel map struct */
typedef struct {
    int type;
    size_t count;
    size_t capacity;
    pixel_xy * pixels;
} pixel_map;

/* initialize LUTs */
int * get_raw2ev(int black);
int * get_ev2raw(int black);
/* free LUTs */
void free_luts(int * raw2ev, int * ev2raw);

/* do chroma smoothing with methods: 2x2, 3x3 and 5x5 */
void chroma_smooth(int method, uint16_t * image_data, int width, int height, int black, int white, int * raw2ev, int * ev2raw);

/* fix focus raw pixels */
void fix_focus_pixels(pixel_map * focus_pixel_map,
                      int * fpm_status,
                      uint16_t * image_data,
                      uint32_t camera_id,
                      uint16_t width,
                      uint16_t height,
                      uint16_t pan_x,
                      uint16_t pan_y,
                      int32_t raw_width,
                      int32_t raw_height,
                      int crop_rec,
                      int unified_mode,
                      int average_method,
                      int dual_iso,
                      int * raw2ev,
                      int * ev2raw);

/* fix all kind of bad raw pixels */
void fix_bad_pixels(pixel_map * bad_pixel_map,
                    int * bpm_status,
                    uint16_t * image_data,
                    uint32_t camera_id,
                    uint16_t width,
                    uint16_t height,
                    uint16_t pan_x,
                    uint16_t pan_y,
                    int32_t raw_width,
                    int32_t raw_height,
                    int32_t black_level,
                    int force,
                    int aggressive,
                    int average_method,
                    int dual_iso,
                    int * raw2ev,
                    int * ev2raw);

void reset_fpm_status(pixel_map * focus_pixel_map, int * fpm_status);
void reset_bpm_status(pixel_map * bad_pixel_map, int * bpm_status);

/* free bufers used for raw processing */
void free_pixel_maps(pixel_map * focus_pixel_map, pixel_map * bad_pixel_map);

#endif
