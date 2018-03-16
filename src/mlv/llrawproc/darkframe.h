/*
 * Copyright (C) 2018 bouncyball
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

#ifndef _darkframe_h
#define _darkframe_h

#include "../mlv_object.h"

/* from video_mlv.c */
extern int openMlvClip(mlvObject_t * video, char * mlvPath, int open_mode, char * error_message);
/* from dng.c */
extern void dng_unpack_image_bits(uint16_t * input_buffer, uint16_t * output_buffer, int width, int height, uint32_t bpp);

void df_init_filename(mlvObject_t * video, char * df_filename);
void df_free_filename(mlvObject_t * video);

enum { DF_OFF, DF_EXT, DF_INT };
int df_validate(mlvObject_t * video, char * df_filename, char * error_message);
int df_init(mlvObject_t * video);
void df_free(mlvObject_t * video);

void df_subtract(mlvObject_t * video, uint16_t * raw_image_buff, size_t raw_image_size);

#endif
