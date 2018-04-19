/*
 * Copyright (C) 2014 David Milligan
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

#ifndef _dualiso_h
#define _dualiso_h

#include <sys/types.h>
#include "../raw.h"

void scale_bits_for_diso(struct raw_info * raw_info, uint16_t * image_data, int lossless_bpp);
int diso_get_preview(uint16_t * image_data, uint16_t width, uint16_t height, int32_t black, int32_t white, int diso_check);
int diso_get_full20bit(struct raw_info raw_info, uint16_t * image_data, int interp_method, int use_alias_map, int use_fullres, int chroma_smooth_method);

#endif
