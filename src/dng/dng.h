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

#ifndef _dng_h
#define _dng_h

#include <sys/types.h>
#include "../mlv/mlv_object.h"

/* raw state definitions */
#define UNCOMPRESSED_RAW 0
#define COMPRESSED_RAW 1
#define UNCOMPRESSED_ORIG 2
#define COMPRESSED_ORIG 3

/* dngObject struct consists of DNG header and image buffers and their sizes */
typedef struct
{
    double fps_float;               // User defined fps
    int32_t par[4];                 // User defined verical and horizontal stretch (AR)

    int raw_input_state;            // 0 - uncompressed, 1 - losless
    int raw_output_state;           // 0 - uncompressed, 1 - losless, 2 - pass uncompressed, 3 - pass losless

    size_t header_size;             // dng header size
    size_t image_size;              // raw image buffer size
    size_t image_size_unpacked;     // bit unpacked working buffer size

    uint8_t * header_buf;           // pointer to header buffer
    uint16_t * image_buf;           // pointer to image buffer
    uint16_t * image_buf_unpacked;  // pointer to bit packed image buffer

} dngObject_t;

/* routines to unpack, pack, decompress or compress raw data */
void dng_unpack_image_bits(uint16_t * input_buffer, uint16_t * output_buffer, int width, int height, uint32_t bpp);
void dng_pack_image_bits(uint16_t * input_buffer, uint16_t * output_buffer, int width, int height, uint32_t bpp, int big_endian);
int dng_compress_image(uint16_t * output_buffer, uint16_t * input_buffer, size_t * output_buffer_size, int width, int height, uint32_t bpp);
int dng_decompress_image(uint16_t * output_buffer, uint16_t * input_buffer, size_t input_buffer_size, int width, int height, uint32_t bpp);

/* routines to initialize, save and free DNG exporting struct */
dngObject_t * initDngObject(mlvObject_t * mlv_data, int raw_state, double fps, int32_t par[4]);
int saveDngFrame(mlvObject_t * mlv_data, dngObject_t * dng_data, uint32_t frame_index, char * dng_filename);
void freeDngObject(dngObject_t * dng_data);

#endif
