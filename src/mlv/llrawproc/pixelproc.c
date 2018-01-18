/*
 * Copyright (C) 2014 The Magic Lantern Team
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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "../raw.h"
#include "opt_med.h"
#include "wirth.h"
#include "pixelproc.h"

#define EV_RESOLUTION 65536

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define COERCE(x,lo,hi) MAX(MIN((x),(hi)),(lo))
#define ABS(a) ((a) > 0 ? (a) : -(a))

#define CHROMA_SMOOTH_2X2
#include "chroma_smooth.c"
#undef CHROMA_SMOOTH_2X2

#define CHROMA_SMOOTH_3X3
#include "chroma_smooth.c"
#undef CHROMA_SMOOTH_3X3

#define CHROMA_SMOOTH_5X5
#include "chroma_smooth.c"
#undef CHROMA_SMOOTH_5X5

#ifdef __WIN32
#define FMT_SIZE "%u"
#else
#define FMT_SIZE "%zu"
#endif

int * get_raw2ev(int black)
{
    int * raw2ev = (int *)malloc(EV_RESOLUTION*sizeof(int));
    
    memset(raw2ev, 0, EV_RESOLUTION * sizeof(int));
    int i;
    for (i = 0; i < EV_RESOLUTION; i++)
    {
        raw2ev[i] = log2(MAX(1, i - black)) * EV_RESOLUTION;
    }

    return raw2ev;
}

int * get_ev2raw(int black)
{
    int * _ev2raw = (int *)malloc(24*EV_RESOLUTION*sizeof(int));
    int* ev2raw = _ev2raw + 10*EV_RESOLUTION;

    int i;
    for (i = -10*EV_RESOLUTION; i < 14*EV_RESOLUTION; i++)
    {
        ev2raw[i] = black + pow(2, (float)i / EV_RESOLUTION);
    }

    return ev2raw;
}

void free_luts(int * raw2ev, int * ev2raw)
{
    if(raw2ev)
    {
        free(raw2ev);
        raw2ev = NULL;
    }

    if(ev2raw)
    {
        free(ev2raw - 10*EV_RESOLUTION);
        ev2raw = NULL;
    }
}

void chroma_smooth(int method, uint16_t * image_data, int width, int height, int black, int white, int * raw2ev, int * ev2raw)
{
    if(raw2ev == NULL) return;
    
    uint16_t * buf = (uint16_t *)malloc(width*height*sizeof(uint16_t));
    if (!buf)
    {
        return;
    }
    memcpy(buf, image_data, width*height*sizeof(uint16_t));
    
    switch (method) {
        case 2:
            chroma_smooth_2x2(width, height, buf, image_data, raw2ev, ev2raw, black, white);
            break;
        case 3:
            chroma_smooth_3x3(width, height, buf, image_data, raw2ev, ev2raw, black, white);
            break;
        case 5:
            chroma_smooth_5x5(width, height, buf, image_data, raw2ev, ev2raw, black, white);
            break;
            
        default:
#ifndef STDOUT_SILENT
            err_printf("Unsupported chroma smooth method\n");
#endif
            break;
    }
    
    free(buf);
}

/* find color of the raw pixel */
static inline int FC(int row, int col)
{
    if ((row%2) == 0 && (col%2) == 0)
    {
        return 0;  /* red */
    }
    else if ((row%2) == 1 && (col%2) == 1)
    {
        return 2;  /* blue */
    }
    else
    {
        return 1;  /* green */
    }
}

/* interpolation method from raw2dng */
static inline void interpolate_pixel(uint16_t * image_data, int x, int y, int w, int h)
{
    int neighbours[100];
    int k = 0;
    int fc0 = FC(x, y);

    /* examine the neighbours of the cold pixel */
    for (int i = -4; i <= 4; i++)
    {
        for (int j = -4; j <= 4; j++)
        {
            /* exclude the cold pixel itself from the examination */
            if (i == 0 && j == 0)
            {
                continue;
            }

            /* exclude out-of-range coords */
            if (x+j < 0 || x+j >= w || y+i < 0 || y+i >= h)
            {
                continue;
            }
            
            /* examine only the neighbours of the same color */
            if (FC(x+j, y+i) != fc0)
            {
                continue;
            }
            
            neighbours[k++] = -image_data[x+j+(y+i)*w];
        }
    }

    /* replace the cold pixel with the median of the neighbours */
    image_data[x + y*w] = -median_int_wirth(neighbours, k);
}

static inline void interpolate_horizontal(uint16_t * image_data, int i, int * raw2ev, int * ev2raw)
{
    int gh1 = image_data[i + 3];
    int gh2 = image_data[i + 1];
    int gh3 = image_data[i - 1];
    int gh4 = image_data[i - 3];
    int dh1 = ABS(raw2ev[gh1] - raw2ev[gh2]);
    int dh2 = ABS(raw2ev[gh3] - raw2ev[gh4]);
    int sum = dh1 + dh2;
    if (sum == 0)
    {
        image_data[i] = image_data[i + 2];
    }
    else
    {
        int ch1 = ((sum - dh1) << 8) / sum;
        int ch2 = ((sum - dh2) << 8) / sum;
        
        int ev_corr = ((raw2ev[image_data[i + 2]] * ch1) >> 8) + ((raw2ev[image_data[i - 2]] * ch2) >> 8);
        image_data[i] = ev2raw[COERCE(ev_corr, 0, 14*EV_RESOLUTION-1)];
    }
}

static inline void interpolate_vertical(uint16_t * image_data, int i, int w, int * raw2ev, int * ev2raw)
{
    int gv1 = image_data[i + w * 3];
    int gv2 = image_data[i + w];
    int gv3 = image_data[i - w];
    int gv4 = image_data[i - w * 3];
    int dv1 = ABS(raw2ev[gv1] - raw2ev[gv2]);
    int dv2 = ABS(raw2ev[gv3] - raw2ev[gv4]);
    int sum = dv1 + dv2;
    if (sum == 0)
    {
        image_data[i] = image_data[i + w * 2];
    }
    else
    {
        int cv1 = ((sum - dv1) << 8) / sum;
        int cv2 = ((sum - dv2) << 8) / sum;
        
        int ev_corr = ((raw2ev[image_data[i + w * 2]] * cv1) >> 8) + ((raw2ev[image_data[i - w * 2]] * cv2) >> 8);
        image_data[i] = ev2raw[COERCE(ev_corr, 0, 14*EV_RESOLUTION-1)];
    }
}

static inline void interpolate_around(uint16_t * image_data, int i, int w, int * raw2ev, int * ev2raw)
{
    int gv1 = image_data[i + w * 3];
    int gv2 = image_data[i + w];
    int gv3 = image_data[i - w];
    int gv4 = image_data[i - w * 3];
    int gh1 = image_data[i + 3];
    int gh2 = image_data[i + 1];
    int gh3 = image_data[i - 1];
    int gh4 = image_data[i - 3];
    int dv1 = ABS(raw2ev[gv1] - raw2ev[gv2]);
    int dv2 = ABS(raw2ev[gv3] - raw2ev[gv4]);
    int dh1 = ABS(raw2ev[gh1] - raw2ev[gh2]);
    int dh2 = ABS(raw2ev[gh3] - raw2ev[gh4]);
    int sum = dh1 + dh2 + dv1 + dv2;
    
    if (sum == 0)
    {
        image_data[i] = image_data[i + 2];
    }
    else
    {
        int cv1 = ((sum - dv1) << 8) / (3 * sum);
        int cv2 = ((sum - dv2) << 8) / (3 * sum);
        int ch1 = ((sum - dh1) << 8) / (3 * sum);
        int ch2 = ((sum - dh2) << 8) / (3 * sum);
        
        int ev_corr =
        ((raw2ev[image_data[i + w * 2]] * cv1) >> 8) +
        ((raw2ev[image_data[i - w * 2]] * cv2) >> 8) +
        ((raw2ev[image_data[i + 2]] * ch1) >> 8) +
        ((raw2ev[image_data[i - 2]] * ch2) >> 8);
        
        image_data[i] = ev2raw[COERCE(ev_corr, 0, 14*EV_RESOLUTION-1)];
    }
}

/* following code is for bad/focus pixel processing **********************************************/
enum pattern { PATTERN_NONE = 0,
               PATTERN_EOSM = 331,
               PATTERN_650D = 301,
               PATTERN_700D = 326,
               PATTERN_100D = 346
             };

enum video_mode { MV_NONE, MV_720,   MV_1080,   MV_1080CROP,   MV_ZOOM,   MV_CROPREC,
                           MV_720_U, MV_1080_U, MV_1080CROP_U, MV_ZOOM_U, MV_CROPREC_U };

static int add_pixel_to_map(pixel_map * map, int x, int y)
{
    if(!map->capacity)
    {
        map->capacity = 50;
        if(!map->type) map->capacity = 25000;
        map->pixels = malloc(sizeof(pixel_xy) * map->capacity);
        if(!map->pixels) goto malloc_error;
    }
    else if(map->count >= map->capacity)
    {
        map->capacity *= 2;
        pixel_xy * pixels = realloc(map->pixels, sizeof(pixel_xy) * map->capacity);
        if(!pixels) goto malloc_error;
        map->pixels = pixels;
    }
    
    map->pixels[map->count].x = x;
    map->pixels[map->count].y = y;
    map->count++;
    return 1;

malloc_error:
#ifndef STDOUT_SILENT
    err_printf("malloc error\n");
#endif
    map->count = 0;
    map->capacity = 0;
    if(map->pixels) free(map->pixels);
    map->pixels = NULL;
    return 0;
}

static int load_pixel_map(pixel_map * map, uint32_t camera_id, int raw_width, int raw_height)
{
    const char * file_ext = ".fpm";
#ifndef STDOUT_SILENT
    const char * map_type = "focus";
#endif
    if(map->type)
    {
        file_ext = ".bpm";
#ifndef STDOUT_SILENT
        map_type = "bad";
#endif
    }

    char file_name[1024];
    sprintf(file_name, "%x_%ix%i%s", camera_id, raw_width, raw_height, file_ext);
    FILE* f = fopen(file_name, "r");
    if(!f) return 0;
    
    uint32_t cam_id = 0x0;
    if(fscanf(f, "#FPM%*[ ]%X%*[^\n]", &cam_id) != 1)
    {
        rewind(f);
    }

    /* if .fpm has header compare cameraID from this header to cameraID from MLV, if different then return 0 */
    if(cam_id != 0 && cam_id != camera_id) return 0;

    int x, y;
    while (fscanf(f, "%d%*[ \t]%d%*[^\n]", &x, &y) != EOF)
    {
        if(!add_pixel_to_map(map, x, y))
        {
            fclose(f);
            return 0; //malloc error
        }
    }

#ifndef STDOUT_SILENT
    printf("\nUsing %s pixel map: '%s'\n"FMT_SIZE" pixels loaded\n", map_type, file_name, map->count);
#endif

    fclose(f);
    return 1;
}

/* normal mode pattern generators ****************************************************************/

/* generate the focus pixel pattern for mv720 video mode */
static void fpm_mv720(pixel_map * map, int pattern, int32_t raw_width)
{
    int shift = 0;
    int fp_start = 290;
    int fp_end = 465;
    int x_rep = 8;
    int y_rep = 12;

    if(pattern == PATTERN_100D)
    {
        fp_start = 86;
        fp_end = 669;
    }

    for(int y = fp_start; y <= fp_end; y++)
    {
        if(((y + 3) % y_rep) == 0) shift = 7;
        else if(((y + 4) % y_rep) == 0) shift = 6;
        else if(((y + 9) % y_rep) == 0) shift = 3;
        else if(((y + 10) % y_rep) == 0) shift = 2;
        else continue;

        for(int x = 72; x < raw_width; x++)
        {
            if(((x + shift) % x_rep) == 0)
            {
                if(!add_pixel_to_map(map, x, y)) return; //malloc error
            }
        }
    }
}

/* generate the focus pixel pattern for mv1080 video mode */
static void fpm_mv1080(pixel_map * map, int pattern, int32_t raw_width)
{
    int shift = 0;
    int fp_start = 459;
    int fp_end = 755;
    int x_rep = 8;
    int y_rep = 10;

    if(pattern == PATTERN_100D)
    {
        fp_start = 119;
        fp_end = 1095;
    }

    for(int y = fp_start; y <= fp_end; y++)
    {
        if(((y + 0) % y_rep) == 0) shift=0;
        else if(((y + 1) % y_rep) == 0) shift = 1;
        else if(((y + 5) % y_rep) == 0) shift = 5;
        else if(((y + 6) % y_rep) == 0) shift = 4;
        else continue;

        for(int x = 72; x < raw_width; x++)
        {
            if(((x + shift) % x_rep) == 0)
            {
                if(!add_pixel_to_map(map, x, y)) return; //malloc error
            }
        }
    }
}

/* generate the focus pixel pattern for mv1080crop video mode */
static void fpm_mv1080crop(pixel_map * map, int pattern, int32_t raw_width)
{
    int shift = 0;
    int fp_start = 121;
    int fp_end = 1013;
    int x_rep = 24;
    int y_rep = 60;

    if(pattern == PATTERN_100D)
    {
        fp_start = 29;
        fp_end = 1057;
        x_rep = 12;
        y_rep = 6;
    }

    for(int y = fp_start; y <= fp_end; y++)
    {
        switch(pattern)
        {
            case PATTERN_EOSM:
            case PATTERN_650D:
            case PATTERN_700D:
                if(((y + 7) % y_rep) == 0 ) shift = 19;
                else if(((y + 11) % y_rep) == 0 ) shift = 13;
                else if(((y + 12) % y_rep) == 0 ) shift = 18;
                else if(((y + 14) % y_rep) == 0 ) shift = 12;
                else if(((y + 26) % y_rep) == 0 ) shift = 0;
                else if(((y + 29) % y_rep) == 0 ) shift = 1;
                else if(((y + 37) % y_rep) == 0 ) shift = 7;
                else if(((y + 41) % y_rep) == 0 ) shift = 13;
                else if(((y + 42) % y_rep) == 0 ) shift = 6;
                else if(((y + 44) % y_rep) == 0 ) shift = 12;
                else if(((y + 56) % y_rep) == 0 ) shift = 0;
                else if(((y + 59) % y_rep) == 0 ) shift = 1;
                else continue;
                break;

            case PATTERN_100D:
                if(((y + 2) % y_rep) == 0 ) shift = 0;
                else if(((y + 5) % y_rep) == 0 ) shift = 1;
                else if(((y + 6) % y_rep) == 0 ) shift = 6;
                else if(((y + 7) % y_rep) == 0 ) shift = 7;
                else continue;
                break;
        }

        for(int x = 72; x < raw_width; x++)
        {
            if(((x + shift) % x_rep) == 0)
            {
                if(!add_pixel_to_map(map, x, y)) return; //malloc error
            }
        }
    }
}

/* generate the focus pixel pattern for zoom video mode */
static void fpm_zoom(pixel_map * map, int pattern, int32_t raw_width)
{
    int shift = 0;
    int fp_start = 31;
    int fp_end = 1103;
    int x_rep = 24;
    int y_rep = 60;

    if(pattern == PATTERN_100D)
    {
        fp_start = 28;
        fp_end = 1105;
        x_rep = 12;
        y_rep = 6;
    }

    for(int y = fp_start; y <= fp_end; y++)
    {
        switch(pattern)
        {
            case PATTERN_EOSM:
            case PATTERN_650D:
            case PATTERN_700D:
                if(((y + 7) % y_rep) == 0) shift = 19;
                else if(((y + 11) % y_rep) == 0) shift = 13;
                else if(((y + 12) % y_rep) == 0) shift = 18;
                else if(((y + 14) % y_rep) == 0) shift = 12;
                else if(((y + 26) % y_rep) == 0) shift = 0;
                else if(((y + 29) % y_rep) == 0) shift = 1;
                else if(((y + 37) % y_rep) == 0) shift = 7;
                else if(((y + 41) % y_rep) == 0) shift = 13;
                else if(((y + 42) % y_rep) == 0) shift = 6;
                else if(((y + 44) % y_rep) == 0) shift = 12;
                else if(((y + 56) % y_rep) == 0) shift = 0;
                else if(((y + 59) % y_rep) == 0) shift = 1;
                else continue;
                break;

            case PATTERN_100D:
                if(((y + 2) % y_rep) == 0) shift = 0;
                else if(((y + 5) % y_rep) == 0) shift = 1;
                else if(((y + 6) % y_rep) == 0) shift = 6;
                else if(((y + 7) % y_rep) == 0) shift = 7;
                else continue;
                break;
        }

        for(int x = 72; x < raw_width; x++)
        {
            if(((x + shift) % x_rep) == 0)
            {
                if(!add_pixel_to_map(map, x, y)) return; //malloc error
            }
        }
    }
}

/* generate the focus pixel pattern for crop_rec video mode (crop_rec module) */
static void fpm_crop_rec(pixel_map * map, int pattern, int32_t raw_width)
{
    int shift = 0;
    int fp_start = 219;
    int fp_end = 515;
    int x_rep = 8;
    int y_rep = 10;

    switch(pattern)
    {
        case PATTERN_EOSM:
        case PATTERN_650D:
        {
            // first pass is like fpm_mv720
            fpm_mv720(map, pattern, raw_width);
            break;
        }

        case PATTERN_700D:
        {
            // no first pass needed
            break;
        }

        case PATTERN_100D:
        {
            // first pass is like fpm_mv720
            fpm_mv720(map, pattern, raw_width);
            // second pass is like fpm_mv1080 with corrected fp_start/fp_end
            fp_start = 28;
            fp_end = 724;
            x_rep = 8;
            y_rep = 10;
            break;
        }

        default: // unsupported camera
            return;
    }

    for(int y = fp_start; y <= fp_end; y++)
    {
        if(((y + 0) % y_rep) == 0) shift=0;
        else if(((y + 1) % y_rep) == 0) shift = 1;
        else if(((y + 5) % y_rep) == 0) shift = 5;
        else if(((y + 6) % y_rep) == 0) shift = 4;
        else continue;

        for(int x = 72; x < raw_width; x++)
        {
            if(((x + shift) % x_rep) == 0)
            {
                if(!add_pixel_to_map(map, x, y)) return; //malloc error
            }
        }
    }
}

/* unified mode pattern generators ***************************************************************/

/*
  fpm_mv720_u() function
  Draw unified focus pixel pattern for mv720 video mode.
*/
static void fpm_mv720_u(pixel_map * map, int pattern, int32_t raw_width)
{
    int shift = 0;
    int fp_start = 28;
    int fp_end = 726;
    int x_rep = 8;
    int y_rep = 12;

    if(pattern == PATTERN_100D)
    {
        x_rep = 8;
        y_rep = 12;
    }

    for(int y = fp_start; y <= fp_end; y++)
    {
        if(((y + 3) % y_rep) == 0) shift = 7;
        else if(((y + 4) % y_rep) == 0) shift = 6;
        else if(((y + 9) % y_rep) == 0) shift = 3;
        else if(((y + 10) % y_rep) == 0) shift = 2;
        else continue;

        for(int x = 72; x < raw_width; x++)
        {
            if(((x + shift) % x_rep) == 0)
            {
                add_pixel_to_map(map, x, y);
            }
        }
    }
}

/*
  fpm_mv1080_u() function
  Draw unified focus pixel pattern for mv1080 video mode.
*/
static void fpm_mv1080_u(pixel_map * map, int pattern, int32_t raw_width)
{
    int shift = 0;
    int fp_start = 28;
    int fp_end = 1189;
    int x_rep = 8;
    int y_rep = 10;

    if(pattern == PATTERN_100D)
    {
        x_rep = 8;
        y_rep = 10;
    }

    for(int y = fp_start; y <= fp_end; y++)
    {
        if(((y + 0) % y_rep) == 0) shift=0;
        else if(((y + 1) % y_rep) == 0) shift = 1;
        else if(((y + 5) % y_rep) == 0) shift = 5;
        else if(((y + 6) % y_rep) == 0) shift = 4;
        else continue;

        for(int x = 72; x < raw_width; x++)
        {
            if(((x + shift) % x_rep) == 0)
            {
                add_pixel_to_map(map, x, y);
            }
        }
    }
}

/*
  fpm_mv1080crop_u() functions (shifted and normal)
  Draw unified focus pixel pattern for mv1080crop video mode.
*/
/* shifted */
static void fpm_mv1080crop_u_shifted(pixel_map * map, int pattern, int32_t raw_width)
{
    int shift = 0;
    int fp_start = 28;
    int fp_end = 1058;
    int x_rep = 8;
    int y_rep = 60;

    if(pattern == PATTERN_100D)
    {
        x_rep = 12;
        y_rep = 6;
    }

    for(int y = fp_start; y <= fp_end; y++)
    {
        switch(pattern)
        {
            case PATTERN_EOSM:
            case PATTERN_650D:
            case PATTERN_700D:
                if(((y + 7) % y_rep) == 0 ) shift = 2;
                else if(((y + 11) % y_rep) == 0 ) shift = 4;
                else if(((y + 12) % y_rep) == 0 ) shift = 1;
                else if(((y + 14) % y_rep) == 0 ) shift = 3;
                else if(((y + 26) % y_rep) == 0 ) shift = 7;
                else if(((y + 29) % y_rep) == 0 ) shift = 0;
                else if(((y + 37) % y_rep) == 0 ) shift = 6;
                else if(((y + 41) % y_rep) == 0 ) shift = 4;
                else if(((y + 42) % y_rep) == 0 ) shift = 5;
                else if(((y + 44) % y_rep) == 0 ) shift = 3;
                else if(((y + 56) % y_rep) == 0 ) shift = 7;
                else if(((y + 59) % y_rep) == 0 ) shift = 0;
                else continue;
                break;

            case PATTERN_100D:
                if(((y + 2) % y_rep) == 0 ) shift = 11;
                else if(((y + 5) % y_rep) == 0 ) shift = 0;
                else if(((y + 6) % y_rep) == 0 ) shift = 5;
                else if(((y + 7) % y_rep) == 0 ) shift = 6;
                else continue;
                break;
        }

        for(int x = 72; x < raw_width; x++)
        {
            if(((x + shift) % x_rep) == 0)
            {
                add_pixel_to_map(map, x, y);
            }
        }
    }
}
/* normal */
static void fpm_mv1080crop_u(pixel_map * map, int pattern, int32_t raw_width)
{
    int shift = 0;
    int fp_start = 28;
    int fp_end = 1058;
    int x_rep = 8;
    int y_rep = 60;

    if(pattern == PATTERN_100D)
    {
        x_rep = 12;
        y_rep = 6;
    }

    for(int y = fp_start; y <= fp_end; y++)
    {
        switch(pattern)
        {
            case PATTERN_EOSM:
            case PATTERN_650D:
            case PATTERN_700D:
                if(((y + 7) % y_rep) == 0 ) shift = 3;
                else if(((y + 11) % y_rep) == 0 ) shift = 5;
                else if(((y + 12) % y_rep) == 0 ) shift = 2;
                else if(((y + 14) % y_rep) == 0 ) shift = 4;
                else if(((y + 26) % y_rep) == 0 ) shift = 0;
                else if(((y + 29) % y_rep) == 0 ) shift = 1;
                else if(((y + 37) % y_rep) == 0 ) shift = 7;
                else if(((y + 41) % y_rep) == 0 ) shift = 5;
                else if(((y + 42) % y_rep) == 0 ) shift = 6;
                else if(((y + 44) % y_rep) == 0 ) shift = 4;
                else if(((y + 56) % y_rep) == 0 ) shift = 0;
                else if(((y + 59) % y_rep) == 0 ) shift = 1;
                else continue;
                break;

            case PATTERN_100D:
                if(((y + 2) % y_rep) == 0 ) shift = 0;
                else if(((y + 5) % y_rep) == 0 ) shift = 1;
                else if(((y + 6) % y_rep) == 0 ) shift = 6;
                else if(((y + 7) % y_rep) == 0 ) shift = 7;
                else continue;
                break;
        }

        for(int x = 72; x < raw_width; x++)
        {
            if(((x + shift) % x_rep) == 0)
            {
                add_pixel_to_map(map, x, y);
            }
        }
    }

    /* Second pass shifted */
    fpm_mv1080crop_u_shifted(map, pattern, raw_width);
}

/*
  zoom_u() function
  Draw unified focus pixel pattern for Zoom video mode.
*/
static void fpm_zoom_u(pixel_map * map, int pattern, int32_t raw_width)
{
    int shift = 0;
    int fp_start = 28;
    int fp_end = 1107;
    int x_rep = 8;
    int y_rep = 60;

    if(pattern == PATTERN_100D)
    {
        x_rep = 12;
        y_rep = 6;
    }

    for(int y = fp_start; y <= fp_end; y++)
    {
        switch(pattern)
        {
            case PATTERN_EOSM:
            case PATTERN_650D:
            case PATTERN_700D:
                if(((y + 7) % y_rep) == 0) shift = 3;
                else if(((y + 11) % y_rep) == 0) shift = 5;
                else if(((y + 12) % y_rep) == 0) shift = 2;
                else if(((y + 14) % y_rep) == 0) shift = 4;
                else if(((y + 26) % y_rep) == 0) shift = 0;
                else if(((y + 29) % y_rep) == 0) shift = 1;
                else if(((y + 37) % y_rep) == 0) shift = 7;
                else if(((y + 41) % y_rep) == 0) shift = 5;
                else if(((y + 42) % y_rep) == 0) shift = 6;
                else if(((y + 44) % y_rep) == 0) shift = 4;
                else if(((y + 56) % y_rep) == 0) shift = 0;
                else if(((y + 59) % y_rep) == 0) shift = 1;
                else continue;
                break;

            case PATTERN_100D:
                if(((y + 2) % y_rep) == 0) shift = 0;
                else if(((y + 5) % y_rep) == 0) shift = 1;
                else if(((y + 6) % y_rep) == 0) shift = 6;
                else if(((y + 7) % y_rep) == 0) shift = 7;
                else continue;
                break;
        }

        for(int x = 72; x < raw_width; x++)
        {
            if(((x + shift) % x_rep) == 0)
            {
                add_pixel_to_map(map, x, y);
            }
        }
    }
}

/*
  fpm_crop_rec_u() function
  Draw unified focus pixel pattern for crop_rec video mode.
  Requires the crop_rec module.
*/
static void fpm_crop_rec_u(pixel_map * map, int pattern, int32_t raw_width)
{
    // first pass is like mv720
    fpm_mv720_u(map, pattern, raw_width);

    int shift = 0;
    int fp_start = 28;
    int fp_end = 726;
    int x_rep = 8;
    int y_rep = 10;

    if(pattern == PATTERN_100D)
    {
        x_rep = 8;
        y_rep = 10;
    }

    for(int y = fp_start; y <= fp_end; y++)
    {
        if(((y + 0) % y_rep) == 0) shift=0;
        else if(((y + 1) % y_rep) == 0) shift = 1;
        else if(((y + 5) % y_rep) == 0) shift = 5;
        else if(((y + 6) % y_rep) == 0) shift = 4;
        else continue;

        for(int x = 72; x < raw_width; x++)
        {
            if(((x + shift) % x_rep) == 0)
            {
                add_pixel_to_map(map, x, y);
            }
        }
    }
}

/* end of pattern generators *********************************************************************/

/* returns focus pixel pattern A, B or NONE in case of unsupported camera */
static int fpm_get_pattern(uint32_t camera_model)
{
    switch(camera_model)
    {
        case 0x80000331:
            return PATTERN_EOSM;

        case 0x80000346:
            return PATTERN_100D;

        case 0x80000301:
            return PATTERN_650D;

        case 0x80000326:
            return PATTERN_700D;

        default: // unsupported camera
            return PATTERN_NONE;
    }
}

/* returns video mode name, special case when vid_mode == "crop_rec" */
static int fpm_get_video_mode(int32_t raw_width, int32_t raw_height, int crop_rec, int unified_mode)
{
    switch(raw_width)
    {
        case 1808:
            if(raw_height < 900)
            {
                if(crop_rec)
                {
                    return MV_CROPREC + unified_mode;
                }
                else
                {
                    return MV_720 + unified_mode;
                }
            }
            else
            {
                return MV_1080 + unified_mode;
            }

        case 1872:
            return MV_1080CROP + unified_mode;

        case 2592:
            return MV_ZOOM + unified_mode;

        default:
            return MV_NONE;
    }
}

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
                      int * ev2raw)
{
    int w = width;
    int h = height;
    int cropX = (pan_x + 7) & ~7;
    int cropY = pan_y & ~1;

    if(raw2ev == NULL)
    {
#ifndef STDOUT_SILENT
        err_printf("raw2ev LUT error\n");
#endif
        return;
    }

fpm_check:
    // fpm_status: 0 = not loaded, 1 = not exists (generate), 2 = loaded/generated (interpolate), 3 = no focus pixel map is generated (unsupported camera)
    switch(*fpm_status)
    {
        case 0: // load fpm
        {
            if(load_pixel_map(focus_pixel_map, camera_id, raw_width, raw_height))
            {
                *fpm_status = 2;
            }
            else
            {
                *fpm_status = 1;
            }
            goto fpm_check;
        }
        case 1: // generate pixel pattern
        {
            enum pattern pattern = fpm_get_pattern(camera_id);
            if(pattern == PATTERN_NONE)
            {
                *fpm_status = 3;
            }
            else
            {
                enum video_mode video_mode = fpm_get_video_mode(raw_width, raw_height, crop_rec, unified_mode);
#ifndef STDOUT_SILENT
                printf("\nGenerating focus pixel map for ");
#endif
                switch(video_mode)
                {
                    case MV_720:
#ifndef STDOUT_SILENT
                        printf("'mv720' mode\n");
#endif
                        fpm_mv720(focus_pixel_map, pattern, raw_width);
                        break;

                    case MV_1080:
#ifndef STDOUT_SILENT
                        printf("'mv1080' mode\n");
#endif
                        fpm_mv1080(focus_pixel_map, pattern, raw_width);
                        break;

                    case MV_1080CROP:
#ifndef STDOUT_SILENT
                        printf("'mv1080crop' mode\n");
#endif
                        fpm_mv1080crop(focus_pixel_map, pattern, raw_width);
                        break;

                    case MV_ZOOM:
#ifndef STDOUT_SILENT
                        printf("'mvZoom' mode\n");
#endif
                        fpm_zoom(focus_pixel_map, pattern, raw_width);
                        break;

                    case MV_CROPREC:
#ifndef STDOUT_SILENT
                        printf("'mvCrop_rec' mode\n");
#endif
                        fpm_crop_rec(focus_pixel_map, pattern, raw_width);
                        break;

                    case MV_720_U:
#ifndef STDOUT_SILENT
                        printf("'mv720' unified mode\n");
#endif
                        fpm_mv720_u(focus_pixel_map, pattern, raw_width);
                        break;

                    case MV_1080_U:
#ifndef STDOUT_SILENT
                        printf("'mv1080' unified mode\n");
#endif
                        fpm_mv1080_u(focus_pixel_map, pattern, raw_width);
                        break;

                    case MV_1080CROP_U:
#ifndef STDOUT_SILENT
                        printf("'mv1080crop' unified mode\n");
#endif
                        fpm_mv1080crop_u(focus_pixel_map, pattern, raw_width);
                        break;

                    case MV_ZOOM_U:
#ifndef STDOUT_SILENT
                        printf("'mvZoom' unified mode\n");
#endif
                        fpm_zoom_u(focus_pixel_map, pattern, raw_width);
                        break;

                    case MV_CROPREC_U:
#ifndef STDOUT_SILENT
                        printf("'mvCrop_rec' unified mode\n");
#endif
                        fpm_crop_rec_u(focus_pixel_map, pattern, raw_width);
                        break;

                    default:
                        break;
                }
#ifndef STDOUT_SILENT
                printf(""FMT_SIZE" pixels generated\n", focus_pixel_map->count);
#endif
                *fpm_status = (focus_pixel_map->count) ? 2 : 3;
            }
            goto fpm_check;
        }
        case 2: // interpolate pixels
        {
#ifndef STDOUT_SILENT
            if(dual_iso)
            {
                printf("Using fpi method for dualiso: 'HORIZONTAL'\n");
            }
            else if(average_method)
            {
                printf("Using fpi method: 'RAW2DNG'\n");
            }
            else
            {
                printf("Using fpi method: 'MLVFS'\n");
            }
#endif
            for (size_t m = 0; m < focus_pixel_map->count; m++)
            {
                int x = focus_pixel_map->pixels[m].x - cropX;
                int y = focus_pixel_map->pixels[m].y - cropY;

                int i = x + y*w;
                if (x > 2 && x < w - 3 && y > 2 && y < h - 3)
                {
                    if(dual_iso)
                    {
                        interpolate_horizontal(image_data, i, raw2ev, ev2raw);
                    }
                    else if(average_method) // 1 = raw2dng
                    {
                        interpolate_pixel(image_data, x, y, w, h);
                    }
                    else // 0 = mlvfs
                    {
                        interpolate_around(image_data, i, w, raw2ev, ev2raw);
                    }
                }
                else if(i > 0 && i < w * h)
                {
                    // handle edge pixels
                    int horizontal_edge = (x >= w - 3 && x < w) || (x >= 0 && x <= 3);
                    int vertical_edge = (y >= h - 3 && y < h) || (y >= 0 && y <= 3);
                    
                    if (horizontal_edge && !vertical_edge && !dual_iso)
                    {
                        interpolate_vertical(image_data, i, w, raw2ev, ev2raw);
                    }
                    else if (vertical_edge && !horizontal_edge)
                    {
                        interpolate_horizontal(image_data, i, raw2ev, ev2raw);
                    }
                    else if(x >= 0 && x <= 3)
                    {
                        image_data[i] = image_data[i + 2];
                    }
                    else if(x >= w - 3 && x < w)
                    {
                        image_data[i] = image_data[i - 2];
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

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
                    int * ev2raw)

{
    int w = width;
    int h = height;
    int black = black_level;
    int cropX = (pan_x + 7) & ~7;
    int cropY = pan_y & ~1;

    if(raw2ev == NULL)
    {
#ifndef STDOUT_SILENT
        err_printf("raw2ev LUT error\n");
#endif
        return;
    }

bpm_check:
    // bpm_status: 0 = not loaded, 1 = not exists (search), 2 = loaded/found (interpolate), 3 = no bad pixels found
    switch(*bpm_status)
    {
        case 0: // load bpm
        {
            if(load_pixel_map(bad_pixel_map, camera_id, raw_width, raw_height))
            {
                *bpm_status = 2;
            }
            else
            {
                *bpm_status = 1;
            }
            goto bpm_check;
        }
        case 1: // search for bad pixels
        {
#ifndef STDOUT_SILENT
            const char * method = NULL;
            if (aggressive)
            {
                method = "AGGRESSIVE";
            }
            else
            {
                method = "NORMAL";
            }
            printf("\nSearching for bad pixels using revealing method: '%s'\n", method);
#endif
            //just guess the dark noise for speed reasons
            int dark_noise = 12;
            int dark_min = black - (dark_noise * 8);
            int dark_max = black + (dark_noise * 8);
            int x,y;
            for (y = 6; y < h - 6; y ++)
            {
                for (x = 6; x < w - 6; x ++)
                {
                    int p = image_data[x + y * w];
                    
                    int neighbours[10];
                    int max1 = 0;
                    int max2 = 0;
                    int k = 0;
                    for (int i = -2; i <= 2; i+=2)
                    {
                        for (int j = -2; j <= 2; j+=2)
                        {
                            if (i == 0 && j == 0) continue;
                            int q = -(int)image_data[(x + j) + (y + i) * w];
                            neighbours[k++] = q;
                            if(q <= max1)
                            {
                                max2 = max1;
                                max1 = q;
                            }
                            else if(q <= max2)
                            {
                                max2 = q;
                            }
                        }
                    }

                    if (p < dark_min) //cold pixel
                    {
#ifndef STDOUT_SILENT
                        printf("COLD - p = %d, dark_min = %d, dark_max = %d, raw2ev[p] = %6d, raw2ev[-max2] = %d\n", p, dark_min, dark_max, raw2ev[p], raw2ev[-max2]);
#endif
                        if(!add_pixel_to_map(bad_pixel_map, x + cropX, y + cropY)) goto mem_err;
                    }
                    else if ((raw2ev[p] - raw2ev[-max2] > (2 * EV_RESOLUTION)) && (p > dark_max)) //hot pixel
                    {
#ifndef STDOUT_SILENT
                        printf("HOT  - p = %d, dark_min = %d, dark_max = %d, raw2ev[p] = %d, raw2ev[-max2] = %d\n", p, dark_min, dark_max, raw2ev[p], raw2ev[-max2]);
#endif
                        if(!add_pixel_to_map(bad_pixel_map, x + cropX, y + cropY)) goto mem_err;
                    }
                    else if (aggressive)
                    {
                        int max3 = kth_smallest_int(neighbours, k, 2);
#ifndef STDOUT_SILENT
                        printf("AGRR - p = %d, dark_min = %d, dark_max = %d, raw2ev[p] = %d, raw2ev[-max2] = %d, raw2ev[-max3] = %d\n", p, dark_min, dark_max, raw2ev[p], raw2ev[-max2], raw2ev[-max3]);
#endif
                        if(((raw2ev[p] - raw2ev[-max2] > EV_RESOLUTION) || (raw2ev[p] - raw2ev[-max3] > EV_RESOLUTION)) && (p > dark_max))
                        {
                            if(!add_pixel_to_map(bad_pixel_map, x + cropX, y + cropY)) goto mem_err;
                        }
                    }
                }
            }
            
#ifndef STDOUT_SILENT
            printf(""FMT_SIZE" bad pixels found\n", bad_pixel_map->count);
#endif

mem_err:
            /* 2 - bad pixels found, goto interpolation stage
             * 3 - bad pixels not found, interpolation not needed */
            *bpm_status = (bad_pixel_map->count) ? 2 : 3;

            goto bpm_check;
        }
        case 2: // interpolate pixels
        {
#ifndef STDOUT_SILENT
            if(dual_iso)
            {
                printf("Using bpi method for dualiso: 'HORIZONTAL'\n");
            }
            else if(average_method)
            {
                printf("Using bpi method: 'RAW2DNG'\n");
            }
            else
            {
                printf("Using bpi method: 'MLVFS'\n");
            }
#endif
            for (size_t m = 0; m < bad_pixel_map->count; m++)
            {
                int x = bad_pixel_map->pixels[m].x - cropX;
                int y = bad_pixel_map->pixels[m].y - cropY;

                int i = x + y*w;
                if (x > 2 && x < w - 3 && y > 2 && y < h - 3)
                {
                    if(dual_iso)
                    {
                        interpolate_horizontal(image_data, i, raw2ev, ev2raw);
                    }
                    else if(average_method)
                    {
                        interpolate_pixel(image_data, x, y, w, h);
                    }
                    else
                    {
                        interpolate_around(image_data, i, w, raw2ev, ev2raw);
                    }
                }
                else if(i > 0 && i < w * h)
                {
                    // handle edge pixels
                    int horizontal_edge = (x >= w - 3 && x < w) || (x >= 0 && x <= 3);
                    int vertical_edge = (y >= h - 3 && y < h) || (y >= 0 && y <= 3);

                    if (horizontal_edge && !vertical_edge && !dual_iso)
                    {
                        interpolate_vertical(image_data, i, w, raw2ev, ev2raw);
                    }
                    else if (vertical_edge && !horizontal_edge)
                    {
                        interpolate_horizontal(image_data, i, raw2ev, ev2raw);
                    }
                    else if(x >= 0 && x <= 3)
                    {
                        image_data[i] = image_data[i + 2];
                    }
                    else if(x >= w - 3 && x < w)
                    {
                        image_data[i] = image_data[i - 2];
                    }
                }
            }

            if(force)
            {
                *bpm_status = 1;
                bad_pixel_map->count = 0;
#ifndef STDOUT_SILENT
                printf("Searching bad pixels for every frame\n");
#endif
            }
            break;
        }
        default:
            break;
    }
}

void reset_fpm_status(pixel_map * focus_pixel_map, int * fpm_status)
{
    *fpm_status = 0;
    focus_pixel_map->count = 0;
    focus_pixel_map->capacity = 0;
    if(focus_pixel_map->pixels)
    {
        free(focus_pixel_map->pixels);
        focus_pixel_map->pixels = NULL;
    }
}

void reset_bpm_status(pixel_map * bad_pixel_map, int * bpm_status)
{
    *bpm_status = 0;
    bad_pixel_map->count = 0;
    bad_pixel_map->capacity = 0;
    if(bad_pixel_map->pixels)
    {
        free(bad_pixel_map->pixels);
        bad_pixel_map->pixels = NULL;
    }
}

void free_pixel_maps(pixel_map * focus_pixel_map, pixel_map * bad_pixel_map)
{
    if(focus_pixel_map->pixels)
    {
        free(focus_pixel_map->pixels);
        focus_pixel_map->pixels = NULL;
    }

    if(bad_pixel_map->pixels)
    {
        free(bad_pixel_map->pixels);
        bad_pixel_map->pixels = NULL;
    }
}
