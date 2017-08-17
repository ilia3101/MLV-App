/*
 * Copyright (C) 2017 The Magic Lantern Team
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

#include "../mlv/raw.h"
#include "opt_med.h"
#include "wirth.h"
#include "pixel_proc.h"

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

static int MAX_RAWVAL;

void set_max_rawval(int32_t bpp)
{
    MAX_RAWVAL = pow(2, bpp) - 1;
}

static int * get_raw2ev(int black)
{
    static int raw2ev[EV_RESOLUTION];
    
    static int initialized = 0;
    if(!initialized)
    {
        memset(raw2ev, 0, MAX_RAWVAL * sizeof(int));
        int i;
        for (i = 0; i < MAX_RAWVAL; i++)
        {
            raw2ev[i] = log2(MAX(1, i - black)) * EV_RESOLUTION;
        }
        initialized = 1;
    }
    return raw2ev;
}

static int * get_ev2raw(int black)
{
    static int _ev2raw[24*EV_RESOLUTION];
    int* ev2raw = _ev2raw + 10*EV_RESOLUTION;

    static int initialized = 0;
    if(!initialized)
    {
        int i;
        for (i = -10*EV_RESOLUTION; i < 14*EV_RESOLUTION; i++)
        {
            ev2raw[i] = black + pow(2, (float)i / EV_RESOLUTION);
        }
        initialized = 1;
    }
    return ev2raw;
}

void chroma_smooth(uint16_t * image_data, int width, int height, int black, int white, int method)
{
    int * raw2ev = get_raw2ev(black);
    int * ev2raw = get_ev2raw(black);
    
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
            err_printf("Unsupported chroma smooth method\n");
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

/* pixel map type */
enum { PIX_FOCUS, PIX_BAD };

struct pixel_xy
{
    int x;
    int y;
};

struct pixel_map
{
    int type;
    size_t count;
    size_t capacity;
    struct pixel_xy * pixels;
};

static struct pixel_map focus_pixel_map = { PIX_FOCUS, 0, 0, NULL };
static struct pixel_map bad_pixel_map = { PIX_BAD, 0, 0, NULL };

static int add_pixel_to_map(struct pixel_map * map, int x, int y)
{
    if(!map->capacity)
    {
        map->capacity = 32;
        map->pixels = malloc(sizeof(struct pixel_xy) * map->capacity);
        if(!map->pixels) goto malloc_error;
    }
    else if(map->count >= map->capacity)
    {
        map->capacity *= 2;
        map->pixels = realloc(map->pixels, sizeof(struct pixel_xy) * map->capacity);
        if(!map->pixels) goto malloc_error;
    }
    
    map->pixels[map->count].x = x;
    map->pixels[map->count].y = y;
    map->count++;
    return 1;

malloc_error:

    err_printf("malloc error\n");
    map->count = 0;
    return 0;
}

static int load_pixel_map(struct pixel_map * map, char * mlv_name, uint32_t camera_id, int raw_width, int raw_height, int dual_iso, int show_progress)
{
    const char * file_ext = ".fpm";
    const char * map_type = "focus";
    if(map->type)
    {
        file_ext = ".bpm";
        map_type = "bad";
    }

    char file_name[1024];
    strcpy(file_name, mlv_name);
    char *ext_dot = strrchr(file_name, '.');
    if(ext_dot) *ext_dot = '\000';
    strcat(file_name, file_ext);
    FILE* f = fopen(file_name, "r");
    if(!f && !map->type)
    {
        sprintf(file_name, "%x_%ix%i.fpm", camera_id, raw_width, raw_height);
        f = fopen(file_name, "r");
    }
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
        add_pixel_to_map(map, x, y);
    }
    
    if (show_progress)
    {
        printf("\nUsing %s pixel map: '%s'\n"FMT_SIZE" pixels loaded\n", map_type, file_name, map->count);
        if (dual_iso)
        {
            printf("Dualiso interpolation method 'HORIZONTAL'\n");
        }
    }
    
    fclose(f);
    return 1;
}

static void save_pixel_map(struct pixel_map * map, char * mlv_name, int show_progress)
{
    const char * file_ext = ".bpm";
    const char * map_type = "bad";
    if(!map->type)
    {
        file_ext = ".fpm";
        map_type = "focus";
    }

    char file_name[1024];
    strcpy(file_name, mlv_name);
    char *ext_dot = strrchr(file_name, '.');
    if(ext_dot) *ext_dot = '\000';
    strcat(file_name, file_ext);
    FILE* f = fopen(file_name, "w");

    if(!f)
    {
        if (show_progress)
        {
            printf("ERROR: Can not write to %s\n", file_name);
        }
        return;
    }
    
    for (size_t i = 0; i < map->count; ++i)
    {
        fprintf(f, "%d \t %d\n", map->pixels[i].x, map->pixels[i].y);
    }
    
    if (show_progress)
    {
        printf(""FMT_SIZE" %s pixels saved to '%s'\n", map->count, map_type, file_name);
    }
    
    fclose(f);
}

void fix_focus_pixels(uint16_t * image_data, struct parameter_list par)
{
    int w = par.width;
    int h = par.height;
    int black = par.black_level;
    int cropX = (par.pan_x + 7) & ~7;
    int cropY = par.pan_y & ~1;

    int * raw2ev = get_raw2ev(black);
    int * ev2raw = get_ev2raw(black);
    if(raw2ev == NULL)
    {
        err_printf("raw2ev LUT error\n");
        return;
    }

    static int fpm_status = 0; // 0 = not loaded, 1 = loaded, 2 = not exist
    switch(fpm_status)
    {
        case 0: // load fpm
        {
            if(load_pixel_map(&focus_pixel_map, par.mlv_name, par.camera_id, par.raw_width, par.raw_height, par.dual_iso, par.show_progress))
                fpm_status = 1;
            else
                fpm_status = 2;
        }
        case 1: // interpolate pixels
        {
            for (size_t m = 0; m < focus_pixel_map.count; m++)
            {
                int x = focus_pixel_map.pixels[m].x - cropX;
                int y = focus_pixel_map.pixels[m].y - cropY;

                int i = x + y*w;
                if (x > 2 && x < w - 3 && y > 2 && y < h - 3)
                {
                    if(par.dual_iso)
                    {
                        interpolate_horizontal(image_data, i, raw2ev, ev2raw);
                    }
                    else if(par.fpi_method)
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
                    
                    if (horizontal_edge && !vertical_edge && !par.dual_iso)
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
        {
            break;
        }
    }
}

void fix_bad_pixels(uint16_t * image_data, struct parameter_list par)
{
    int w = par.width;
    int h = par.height;
    int black = par.black_level;
    int cropX = (par.pan_x + 7) & ~7;
    int cropY = par.pan_y & ~1;

    int * raw2ev = get_raw2ev(black);
    int * ev2raw = get_ev2raw(black);
    if(raw2ev == NULL)
    {
        err_printf("raw2ev LUT error\n");
        return;
    }

    static int bpm_status = 0; // 0 = not loaded, 1 = loaded, 2 = not exist, 3 = no bad pixels found
    switch(bpm_status)
    {
        case 0: // load bpm
        {
            if(load_pixel_map(&bad_pixel_map, par.mlv_name, par.camera_id, par.raw_width, par.raw_height, par.dual_iso, par.show_progress))
            {
                bpm_status = 2;
            }
            else
            {
                bpm_status = 1;
            }
        }
        case 1: // search for bad pixels and save to file if needed
        {
            //just guess the dark noise for speed reasons
            int dark_noise = 12 ;
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
                        add_pixel_to_map(&bad_pixel_map, x + cropX, y + cropY);
                    }
                    else if ((raw2ev[p] - raw2ev[-max2] > 2 * EV_RESOLUTION) && (p > dark_max)) //hot pixel
                    {
                        add_pixel_to_map(&bad_pixel_map, x + cropX, y + cropY);
                    }
                    else if (par.aggressive)
                    {
                        int max3 = kth_smallest_int(neighbours, k, 2);
                        if(((raw2ev[p] - raw2ev[-max2] > EV_RESOLUTION) || (raw2ev[p] - raw2ev[-max3] > EV_RESOLUTION)) && (p > dark_max))
                        {
                            add_pixel_to_map(&bad_pixel_map, x + cropX, y + cropY);
                        }
                    }
                }
            }
            
            if (par.show_progress)
            {
                const char * method = NULL;
                if (par.aggressive)
                {
                    method = "AGGRESSIVE";
                }
                else
                {
                    method = "NORMAL";
                }

                printf("\nUsing bad pixel revealing method: '%s'\n", method);
                if (par.dual_iso) printf("Dualiso iterpolation method 'HORIZONTAL'\n");
                printf(""FMT_SIZE" bad pixels found for '%s' (crop: %d, %d)\n", bad_pixel_map.count, par.mlv_name, cropX, cropY);
                if (!bad_pixel_map.count && par.save_bpm) printf("Bad pixel map file not written\n");
            }

            if (bad_pixel_map.count)
            {
                if (par.save_bpm)
                {
                    /* if save_bpm is non zero - save bad pixels to a file */
                    save_pixel_map(&bad_pixel_map, par.mlv_name, par.show_progress);
                }
                bpm_status = 2; // bad pixels found, goto interpolation stage
            }
            else
            {
                bpm_status = 3; // bad pixels not found, interpolation not needed
            }
        }
        case 2: // interpolate pixels
        {
            for (size_t m = 0; m < bad_pixel_map.count; m++)
            {
                int x = bad_pixel_map.pixels[m].x - cropX;
                int y = bad_pixel_map.pixels[m].y - cropY;
               
                int i = x + y*w;
                if (x > 2 && x < w - 3 && y > 2 && y < h - 3)
                {
                    if(par.dual_iso)
                    {
                        interpolate_horizontal(image_data, i, raw2ev, ev2raw);
                    }
                    else if(par.bpi_method)
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

                    if (horizontal_edge && !vertical_edge && !par.dual_iso)
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
        {
            break;
        }
    }
}

void free_pixel_maps()
{
    if(focus_pixel_map.pixels) free(focus_pixel_map.pixels);
    if(bad_pixel_map.pixels) free(bad_pixel_map.pixels);
}
