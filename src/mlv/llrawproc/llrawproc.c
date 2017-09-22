/*
 * Copyright (C) 2017 Bouncyball
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "llrawproc.h"
#include "pixelproc.h"
#include "stripes.h"
#include "patternnoise.h"
#include "hist.h"

#include "../mlv_object.h"

static void deflicker(mlvObject_t * video, uint16_t * raw_image_buff, size_t raw_image_size)
{
    uint16_t black = video->llrawproc->mlv_black_level;
    uint16_t white = (1 << video->RAWI.raw_info.bits_per_pixel) + 1;

    struct histogram * hist = hist_create(white);
    hist_add(hist, raw_image_buff + 1, (uint32_t)((raw_image_size - 1) / 2), 1);
    uint16_t median = hist_median(hist);
    double correction = log2((double) (video->llrawproc->deflicker_target - black) / (median - black));
    video->RAWI.raw_info.exposure_bias[0] = correction * 10000;
    video->RAWI.raw_info.exposure_bias[1] = 10000;
}

/* Initialises processing thing with memory */
llrawprocObject_t * initLLRawProcObject()
{
    llrawprocObject_t * llrawproc = calloc(1, sizeof(llrawprocObject_t));

    /* set defaults */
    llrawproc->vertical_stripes = 1;
    llrawproc->focus_pixels = 1;
    llrawproc->fpi_method = 1;
    llrawproc->bad_pixels = 1;
    llrawproc->bpi_method = 1;
    llrawproc->chroma_smooth = 0;
    llrawproc->pattern_noise = 0;
    llrawproc->deflicker_target = 0;
    llrawproc->dual_iso = 0;
    llrawproc->fpm_status = 0;
    llrawproc->bpm_status = 0;
    llrawproc->first_time = 1;

    llrawproc->raw2ev = NULL;
    llrawproc->ev2raw = NULL;

    llrawproc->focus_pixel_map.type = PIX_FOCUS;
    llrawproc->focus_pixel_map.pixels = NULL;
    llrawproc->bad_pixel_map.type = PIX_BAD;
    llrawproc->bad_pixel_map.pixels = NULL;

    return llrawproc;
}

void freeLLRawProcObject(llrawprocObject_t * llrawproc)
{
    free_luts(llrawproc->raw2ev, llrawproc->ev2raw);
    free_pixel_maps(&llrawproc->focus_pixel_map, &llrawproc->bad_pixel_map);
    free(llrawproc);
}

/* all low level raw processing takes place here */
void applyLLRawProcObject(mlvObject_t * video, uint16_t * raw_image_buff, size_t raw_image_size)
{
    /* on fix_raw=0 skip raw processing alltogether */
    if(!video->llrawproc->fix_raw) return;

    /* initialise LUTs */
    if(video->llrawproc->first_time)
    {
        video->llrawproc->raw2ev = get_raw2ev(video->llrawproc->mlv_black_level, video->RAWI.raw_info.bits_per_pixel);
        video->llrawproc->ev2raw = get_ev2raw(video->llrawproc->mlv_black_level);
    }

    /* deflicker RAW data */
    if (video->llrawproc->deflicker_target)
    {
#ifndef STDOUT_SILENT
        if (video->llrawproc->first_time)
        {
            printf("\nPer-frame exposure compensation: 'ON'\nDeflicker target: '%d'\n", video->llrawproc->deflicker_target);
        }
#endif
        deflicker(video, raw_image_buff, raw_image_size);
    }

    /* fix pattern noise */
    if (video->llrawproc->pattern_noise)
    {
#ifndef STDOUT_SILENT
        if (video->llrawproc->first_time)
        {
            printf("\nFixing pattern noise... ");
        }
#endif
        fix_pattern_noise((int16_t *)raw_image_buff, video->RAWI.xRes, video->RAWI.yRes, video->llrawproc->mlv_white_level, 0);
#ifndef STDOUT_SILENT
        if (video->llrawproc->first_time)
        {
            printf("Done\n");
        }
#endif
    }

    /* fix focus pixels */
    if (video->llrawproc->focus_pixels)
    {
        fix_focus_pixels(&video->llrawproc->focus_pixel_map,
                         &video->llrawproc->fpm_status,
                         raw_image_buff,
                         video->IDNT.cameraModel,
                         video->RAWI.xRes,
                         video->RAWI.yRes,
                         video->VIDF.panPosX,
                         video->VIDF.panPosY,
                         video->RAWI.raw_info.width,
                         video->RAWI.raw_info.height,
                         video->llrawproc->fpi_method,
                         video->llrawproc->dual_iso,
                         video->llrawproc->raw2ev,
                         video->llrawproc->ev2raw);
    }

    /* fix bad pixels */
    if (video->llrawproc->bad_pixels)
    {
        fix_bad_pixels(&video->llrawproc->bad_pixel_map,
                       &video->llrawproc->bpm_status,
                       raw_image_buff,
                       video->IDNT.cameraModel,
                       video->RAWI.xRes,
                       video->RAWI.yRes,
                       video->VIDF.panPosX,
                       video->VIDF.panPosY,
                       video->RAWI.raw_info.width,
                       video->RAWI.raw_info.height,
                       video->llrawproc->mlv_black_level,
                       (video->llrawproc->bad_pixels == 2),
                       video->llrawproc->bpi_method,
                       video->llrawproc->dual_iso,
                       video->llrawproc->raw2ev,
                       video->llrawproc->ev2raw);
    }

    /* do chroma smoothing */
    if (video->llrawproc->chroma_smooth)
    {
#ifndef STDOUT_SILENT
        if (video->llrawproc->first_time)
        {
            printf("\nUsing chroma smooth method: '%dx%d'\n", video->llrawproc->chroma_smooth, video->llrawproc->chroma_smooth);
        }
#endif
        chroma_smooth(video->llrawproc->chroma_smooth,
                      raw_image_buff,
                      video->RAWI.xRes,
                      video->RAWI.yRes,
                      video->llrawproc->mlv_black_level,
                      video->llrawproc->mlv_white_level,
                      video->llrawproc->raw2ev,
                      video->llrawproc->ev2raw);
    }

    /* fix vertical stripes */
    if (video->llrawproc->vertical_stripes)
    {
        fix_vertical_stripes(&video->llrawproc->stripe_corrections,
                             raw_image_buff,
                             raw_image_size / 2,
                             video->llrawproc->mlv_black_level,
                             video->llrawproc->mlv_white_level,
                             video->RAWI.raw_info.frame_size,
                             video->RAWI.xRes,
                             video->RAWI.yRes,
                             video->llrawproc->vertical_stripes,
                             video->llrawproc->first_time);
    }

    video->llrawproc->first_time = 0;
}
