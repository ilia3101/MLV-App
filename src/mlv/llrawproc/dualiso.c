/*
 * Copyright (C) 2014 David Milligan
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include "hist.h"
#include "dualiso.h"
#include "opt_med.h"
#include "wirth.h"
#include <pthread.h>
#include "../../debayer/debayer.h"

#define EV_RESOLUTION 65536
#ifndef M_PI
#define M_PI 3.14159265358979323846 /* pi */
#endif

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define COERCE(x,lo,hi) MAX(MIN((x),(hi)),(lo))
#define ABS(a) ((a) > 0 ? (a) : -(a))

#define LOCK(x) static pthread_mutex_t x = PTHREAD_MUTEX_INITIALIZER; pthread_mutex_lock(&x);
#define UNLOCK(x) pthread_mutex_unlock(&(x));

void scale_bits_for_diso(struct raw_info * raw_info, uint16_t * image_data, int lossless_bpp)
{
    if (raw_info->bits_per_pixel < 14)
    {
        int pixel_count = raw_info->width * raw_info->height;
        int shift_bits = 14 - raw_info->bits_per_pixel;

        raw_info->black_level <<= shift_bits;
        raw_info->white_level <<= shift_bits;

        for(int i = 0; i < pixel_count; ++i)
        {
            image_data[i] <<= shift_bits;
        }
    }
    else if(lossless_bpp < 14)
    {
        int pixel_count = raw_info->width * raw_info->height;
        int multiplier = raw_info->bits_per_pixel - lossless_bpp;

        raw_info->white_level = (raw_info->white_level - raw_info->black_level) * multiplier + raw_info->black_level;

        for(int i = 0; i < pixel_count; ++i)
        {
            image_data[i] = (image_data[i] - raw_info->black_level) * multiplier + raw_info->black_level;
        }
    }
}

//this is just meant to be fast
int diso_get_preview(uint16_t * image_data, uint16_t width, uint16_t height, int32_t black, int32_t white, int diso_check)
{
    //compute the median of the green channel for each multiple of 4 rows
    uint16_t median[4];
    struct histogram * hist[4];
    struct histogram * hist_hi = NULL;
    struct histogram * hist_lo = NULL;
    
    for(int i = 0; i < 4; i++)
        hist[i] = hist_create(white);
    
    for(uint16_t y = 4; y < height - 4; y += 5)
    {
        hist_add(hist[y % 4], &(image_data[y * width + (y + 1) % 2]), width - (y + 1) % 2, 3);
    }
    
    for(int i = 0; i < 4; i++)
    {
        median[i] = hist_median(hist[i]);
    }
    
    uint16_t dark_row_start = -1;
    if((median[2] - black) > ((median[0] - black) * 2) &&
       (median[2] - black) > ((median[1] - black) * 2) &&
       (median[3] - black) > ((median[0] - black) * 2) &&
       (median[3] - black) > ((median[1] - black) * 2))
    {
        dark_row_start = 0;
        hist_lo = hist[0];
        hist_hi = hist[2];
    }
    else if((median[0] - black) > ((median[1] - black) * 2) &&
            (median[0] - black) > ((median[2] - black) * 2) &&
            (median[3] - black) > ((median[1] - black) * 2) &&
            (median[3] - black) > ((median[2] - black) * 2))
    {
        dark_row_start = 1;
        hist_lo = hist[1];
        hist_hi = hist[0];
    }
    else if((median[0] - black) > ((median[2] - black) * 2) &&
            (median[0] - black) > ((median[3] - black) * 2) &&
            (median[1] - black) > ((median[2] - black) * 2) &&
            (median[1] - black) > ((median[3] - black) * 2))
    {
        dark_row_start = 2;
        hist_lo = hist[2];
        hist_hi = hist[0];
    }
    else if((median[1] - black) > ((median[0] - black) * 2) &&
            (median[1] - black) > ((median[3] - black) * 2) &&
            (median[2] - black) > ((median[0] - black) * 2) &&
            (median[2] - black) > ((median[3] - black) * 2))
    {
        dark_row_start = 3;
        hist_lo = hist[0];
        hist_hi = hist[2];
    }
    else
    {
#ifndef STDOUT_SILENT
        err_printf("\nCould not detect dual ISO interlaced lines\n");
#endif

        for(int i = 0; i < 4; i++)
        {
            hist_destroy(hist[i]);
        }
        return 0;
    }
    
    if(diso_check)
    {
#ifndef STDOUT_SILENT
        err_printf("\nDetected dual ISO interlaced lines\n");
#endif

        for(int i = 0; i < 4; i++)
        {
            hist_destroy(hist[i]);
        }
        return 1;
    }

    /* compare the two histograms and plot the curve between the two exposures (dark as a function of bright) */
    const int min_pix = 100;                                /* extract a data point every N image pixels */
    int data_size = (width * height / min_pix + 1);                  /* max number of data points */
    int* data_x = (int *)malloc(data_size * sizeof(data_x[0]));
    int* data_y = (int *)malloc(data_size * sizeof(data_y[0]));
    double* data_w = (double *)malloc(data_size * sizeof(data_w[0]));
    int data_num = 0;
    
    int acc_lo = 0;
    int acc_hi = 0;
    int raw_lo = 0;
    int raw_hi = 0;
    int prev_acc_hi = 0;
    
    int hist_total = hist[0]->count;
    
    for (raw_hi = 0; raw_hi < hist_total; raw_hi++)
    {
        acc_hi += hist_hi->data[raw_hi];
        
        while (acc_lo < acc_hi)
        {
            acc_lo += hist_lo->data[raw_lo];
            raw_lo++;
        }
        
        if (raw_lo >= white)
            break;
        
        if (acc_hi - prev_acc_hi > min_pix)
        {
            if (acc_hi > hist_total * 1 / 100 && acc_hi < hist_total * 99.99 / 100)    /* throw away outliers */
            {
                data_x[data_num] = raw_hi - black;
                data_y[data_num] = raw_lo - black;
                data_w[data_num] = (MAX(0, raw_hi - black + 100));    /* points from higher brightness are cleaner */
                data_num++;
                prev_acc_hi = acc_hi;
            }
        }
    }
    
    /**
     * plain least squares
     * y = ax + b
     * a = (mean(xy) - mean(x)mean(y)) / (mean(x^2) - mean(x)^2)
     * b = mean(y) - a mean(x)
     */
    
    double mx = 0, my = 0, mxy = 0, mx2 = 0;
    double weight = 0;
    for (int i = 0; i < data_num; i++)
    {
        mx += data_x[i] * data_w[i];
        my += data_y[i] * data_w[i];
        mxy += (double)data_x[i] * data_y[i] * data_w[i];
        mx2 += (double)data_x[i] * data_x[i] * data_w[i];
        weight += data_w[i];
    }
    mx /= weight;
    my /= weight;
    mxy /= weight;
    mx2 /= weight;
    double a = (mxy - mx*my) / (mx2 - mx*mx);
    double b = my - a * mx;
    
    free(data_w);
    free(data_y);
    free(data_x);

    for(int i = 0; i < 4; i++)
    {
        hist_destroy(hist[i]);
    }
    
    //TODO: what's a better way to pick a value for this?
    uint16_t shadow = (uint16_t)(black + 1 / (a * a) + b);
    
    for(int y = 0; y < height; y++)
    {
        int row_start = y * width;
        if (((y - dark_row_start + 4) % 4) >= 2)
        {
            //bright row
            for(int i = row_start; i < row_start + width; i++)
            {
                if(image_data[i] >= white)
                {
                    image_data[i] = y > 2 ? (y < height - 2 ? (image_data[i-width*2] + image_data[i+width*2]) / 2 : image_data[i-width*2]) : image_data[i+width*2];
                }
                else
                {
                    image_data[i] = (uint16_t)(MIN(white,(image_data[i] - black) * a + black + b));
                }
            }
        }
        else
        {
            //dark row
            for(int i = row_start; i < row_start + width; i++)
            {
                if(image_data[i] < shadow)
                {
                    image_data[i] = (uint16_t)(y > 2 ? (y < height - 2 ? (image_data[i-width*2] + MIN(white,(image_data[i+width*2]  - black) * a + black + b)) / 2 : image_data[i-width*2]) : MIN(white,(image_data[i+width*2]  - black) * a + black + b));
                }
                
            }
        }
    }

    return 1;
}


//from cr2hdr 20bit version
//this is not thread safe (yet)

#define BRIGHT_ROW (is_bright[y % 4])
#define COUNT(x) ((int)(sizeof(x)/sizeof((x)[0])))

#define raw_get_pixel(x,y) (image_data[(x) + (y) * raw_info.width])
#define raw_get_pixel16(x,y) (image_data[(x) + (y) * raw_info.width])
#define raw_get_pixel_14to20(x,y) ((((uint32_t)image_data[(x) + (y) * raw_info.width]) << 6) & 0xFFFFF)
#define raw_get_pixel32(x,y) (raw_buffer_32[(x) + (y) * raw_info.width])
#define raw_set_pixel32(x,y,value) raw_buffer_32[(x) + (y)*raw_info.width] = value
#define raw_get_pixel_20to16(x,y) ((raw_get_pixel32(x,y) >> 4) & 0xFFFF)
#define raw_set_pixel_20to16_rand(x,y,value) image_data[(x) + (y) * raw_info.width] = COERCE((int)((value) / 16.0 + fast_randn05() + 0.5), 0, 0xFFFF)
#define raw_set_pixel20(x,y,value) raw_buffer_32[(x) + (y) * raw_info.width] = COERCE((value), 0, 0xFFFFF)

static const double fullres_thr = 0.8;

/* trial and error - too high = aliasing, too low = noisy */
static const int ALIAS_MAP_MAX = 15000;

static void white_detect(struct raw_info raw_info, uint16_t * image_data, int* white_dark, int* white_bright, int * is_bright)
{
    /* sometimes the white level is much lower than 15000; this would cause pink highlights */
    /* workaround: consider the white level as a little under the maximum pixel value from the raw file */
    /* caveat: bright and dark exposure may have different white levels, so we'll take the minimum value */
    /* side effect: if the image is not overexposed, it may get brightened a little; shouldn't hurt */
    
    int whites[2]         = {  0,    0};
    int discard_pixels[2] = { 10,   50}; /* discard the brightest N pixels */
    int safety_margins[2] = {100, 1500}; /* use a higher safety margin for the higher ISO */
    /* note: with the high-ISO WL underestimated by 1500, you would lose around 0.15 EV of non-aliased detail */
    
    int* pixels[2];
    int max_pix = raw_info.width * raw_info.height / 2 / 9;
    pixels[0] = malloc(max_pix * sizeof(pixels[0][0]));
    pixels[1] = malloc(max_pix * sizeof(pixels[0][0]));
    memset(pixels[0], 0, sizeof(max_pix * sizeof(pixels[0][0])));
    memset(pixels[1], 0, sizeof(max_pix * sizeof(pixels[0][0])));
    int counts[2] = {0, 0};
    
    /* collect all the pixels and find the k-th max, thus ignoring hot pixels */
    /* change the sign in order to use kth_smallest_int */
    for (int y = raw_info.active_area.y1; y < raw_info.active_area.y2; y += 3)
    {
        for (int x = raw_info.active_area.x1; x < raw_info.active_area.x2; x += 3)
        {
            int pix = raw_get_pixel16(x, y);
            
#define BIN_IDX is_bright[y%4]
            counts[BIN_IDX] = MIN(counts[BIN_IDX], max_pix-1);
            pixels[BIN_IDX][counts[BIN_IDX]] = -pix;
            counts[BIN_IDX]++;
#undef BIN_IDX
        }
    }
    
    whites[0] = -kth_smallest_int(pixels[0], counts[0], discard_pixels[0]) - safety_margins[0];
    whites[1] = -kth_smallest_int(pixels[1], counts[1], discard_pixels[1]) - safety_margins[1];
    
    //~ printf("%8d %8d\n", whites[0], whites[1]);
    //~ printf("%8d %8d\n", counts[0], counts[1]);
    
    /* we assume 14-bit input data; out-of-range white levels may cause crash */
    *white_dark = COERCE(whites[0], 10000, 16383);
    *white_bright = COERCE(whites[1], 5000, 16383);
#ifndef STDOUT_SILENT
    printf("White levels    : %d %d\n", *white_dark, *white_bright);
#endif
    free(pixels[0]);
    free(pixels[1]);
}

static void compute_black_noise(struct raw_info raw_info, uint16_t * image_data, int x1, int x2, int y1, int y2, int dx, int dy, double* out_mean, double* out_stdev)
{
    long long black = 0;
    int num = 0;
    /* compute average level */
    for (int y = y1; y < y2; y += dy)
    {
        for (int x = x1; x < x2; x += dx)
        {
            black += raw_get_pixel(x, y);
            num++;
        }
    }
    
    double mean = (double) black / num;
    
    /* compute standard deviation */
    double stdev = 0;
    for (int y = y1; y < y2; y += dy)
    {
        for (int x = x1; x < x2; x += dx)
        {
            double dif = raw_get_pixel(x, y) - mean;
            stdev += dif * dif;
        }
    }
    stdev /= (num-1);
    stdev = sqrt(stdev);
    
    if (num == 0)
    {
        mean = raw_info.black_level;
        stdev = 8; /* default to 11 stops of DR */
    }
    
    *out_mean = mean;
    *out_stdev = stdev;
}

static int mean2(int a, int b, int white, int* err)
{
    if (a >= white || b >= white)
    {
        if (err) *err = 10000000;
        return white;
    }
    
    int m = (a + b) / 2;
    
    if (err)
        *err = ABS(a - b);
    
    return m;
}

static int mean3(int a, int b, int c, int white, int* err)
{
    int m = (a + b + c) / 3;
    
    if (err)
        *err = MAX(MAX(ABS(a - m), ABS(b - m)), ABS(c - m));
    
    if (a >= white || b >= white || c >= white)
        return MAX(m, white);
    
    return m;
}

/* http://www.developpez.net/forums/d544518/c-cpp/c/equivalent-randn-matlab-c/#post3241904 */

#define TWOPI (6.2831853071795864769252867665590057683943387987502) /* 2 * pi */

/*
 RAND is a macro which returns a pseudo-random numbers from a uniform
 distribution on the interval [0 1]
 */
#define RAND (rand())/((double) RAND_MAX)

/*
 RANDN is a macro which returns a pseudo-random numbers from a normal
 distribution with mean zero and standard deviation one. This macro uses Box
 Muller's algorithm
 */
#define RANDN (sqrt(-2.0*log(RAND))*cos(TWOPI*RAND))

/* anti-posterization noise */
/* before rounding, it's a good idea to add a Gaussian noise of stdev=0.5 */
static float randn05_cache[1024];

void fast_randn_init()
{
    int i;
    for (i = 0; i < 1024; i++)
    {
        randn05_cache[i] = RANDN / 2;
    }
}

float fast_randn05()
{
    static int k = 0;
    return randn05_cache[(k++) & 1023];
}

static int identify_rggb_or_gbrg(struct raw_info raw_info, uint16_t * image_data)
{
    int w = raw_info.width;
    int h = raw_info.height;
    
    /* build 4 little histograms: one for red, one for blue and two for green */
    /* we don't know yet which channels are which, but that's what we are trying to find out */
    /* the ones with the smallest difference are likely the green channels */
    int hist_size = 16384 * sizeof(int);
    int* hist[4];
    for (int i = 0; i < 4; i++)
    {
        hist[i] = malloc(hist_size);
        memset(hist[i], 0, hist_size);
    }
    
    int y0 = (raw_info.active_area.y1 + 3) & ~3;
    
    /* to simplify things, analyze an identical number of bright and dark lines */
    for (int y = y0; y < h/4*4; y++)
    {
        for (int x = 0; x < w; x++)
            hist[(y%2)*2 + (x%2)][raw_get_pixel16(x,y) & 16383]++;
    }
    
    /* compute cdf */
    for (int k = 0; k < 4; k++)
    {
        int acc = 0;
        for (int i = 0; i < 16384; i++)
        {
            acc += hist[k][i];
            hist[k][i] = acc;
        }
    }
    
    /* compare cdf's */
    /* for rggb, greens are at y%2 != x%2, that is, 1 and 2 */
    /* for gbrg, greens are at y%2 == x%2, that is, 0 and 3 */
    double diffs_rggb = 0;
    double diffs_gbrg = 0;
    for (int i = 0; i < 16384; i++)
    {
        diffs_rggb += ABS(hist[1][i] - hist[2][i]);
        diffs_gbrg += ABS(hist[0][i] - hist[3][i]);
    }
    
    for (int i = 0; i < 4; i++)
    {
        free(hist[i]); hist[i] = 0;
    }
    
    /* which one is most likely? */
    return diffs_rggb < diffs_gbrg;
}

static int identify_bright_and_dark_fields(struct raw_info raw_info, uint16_t * image_data, int rggb, int * is_bright)
{
    /* first we need to know which lines are dark and which are bright */
    /* the pattern is not always the same, so we need to autodetect it */
    
    /* it may look like this */                       /* or like this */
    /*
     ab cd ef gh  ab cd ef gh               ab cd ef gh  ab cd ef gh
     
     0  RG RG RG RG  RG RG RG RG            0  rg rg rg rg  rg rg rg rg
     1  gb gb gb gb  gb gb gb gb            1  gb gb gb gb  gb gb gb gb
     2  rg rg rg rg  rg rg rg rg            2  RG RG RG RG  RG RG RG RG
     3  GB GB GB GB  GB GB GB GB            3  GB GB GB GB  GB GB GB GB
     4  RG RG RG RG  RG RG RG RG            4  rg rg rg rg  rg rg rg rg
     5  gb gb gb gb  gb gb gb gb            5  gb gb gb gb  gb gb gb gb
     6  rg rg rg rg  rg rg rg rg            6  RG RG RG RG  RG RG RG RG
     7  GB GB GB GB  GB GB GB GB            7  GB GB GB GB  GB GB GB GB
     8  RG RG RG RG  RG RG RG RG            8  rg rg rg rg  rg rg rg rg
     */
    
    /* white level is not yet known, just use a rough guess */
    int white = 10000;
    int black = raw_info.black_level;
    
    int w = raw_info.width;
    int h = raw_info.height;
    
    /* build 4 little histograms */
    int hist_size = 16384 * sizeof(int);
    int* hist[4];
    for (int i = 0; i < 4; i++)
    {
        hist[i] = malloc(hist_size);
        memset(hist[i], 0, hist_size);
    }
    
    int y0 = (raw_info.active_area.y1 + 3) & ~3;
    
    /* to simplify things, analyze an identical number of bright and dark lines */
    for (int y = y0; y < h/4*4; y++)
    {
        for (int x = 0; x < w; x++)
        {
            if ((x%2) != (y%2))
            {
                /* only check the green pixels */
                hist[y%4][raw_get_pixel16(x,y) & 16383]++;
            }
        }
    }
    
    int hist_total = 0;
    for (int i = 0; i < 16384; i++)
        hist_total += hist[0][i];
    
    /* choose the highest percentile that is not overexposed */
    /* but not higher than 99.8, to keep a tiny bit of robustness (specular highlights may play dirty tricks) */
    int acc[4] = {0};
    int raw[4] = {0};
    int off[4] = {0};
    int ref;
    int ref_max = hist_total * 0.998;
    int ref_off = hist_total * 0.05;
    for (ref = 0; ref < ref_max; ref++)
    {
        for (int i = 0; i < 4; i++)
        {
            while (acc[i] < ref)
            {
                acc[i] += hist[i][raw[i]];
                raw[i]++;
            }
        }
        
        if (ref < ref_off)
        {
            if (MAX(MAX(raw[0], raw[1]), MAX(raw[2], raw[3])) < black + (white-black) / 4)
            {
                /* try to remove the black offset by estimating it from relatively dark pixels */
                off[0] = raw[0];
                off[1] = raw[1];
                off[2] = raw[2];
                off[3] = raw[3];
            }
        }
        
        if (raw[0] >= white) break;
        if (raw[1] >= white) break;
        if (raw[2] >= white) break;
        if (raw[3] >= white) break;
    }
    
    for (int i = 0; i < 4; i++)
    {
        free(hist[i]); hist[i] = 0;
    }
    
    /* remove black offsets */
    raw[0] -= off[0];
    raw[1] -= off[1];
    raw[2] -= off[2];
    raw[3] -= off[3];
    
    /* very crude way to compute median */
    int sorted_bright[4];
    memcpy(sorted_bright, raw, sizeof(sorted_bright));
    {
        for (int i = 0; i < 4; i++)
        {
            for (int j = i+1; j < 4; j++)
            {
                if (sorted_bright[i] > sorted_bright[j])
                {
                    double aux = sorted_bright[i];
                    sorted_bright[i] = sorted_bright[j];
                    sorted_bright[j] = aux;
                }
            }
        }
    }
    double median_bright = (sorted_bright[1] + sorted_bright[2]) / 2;
    
    for (int i = 0; i < 4; i++)
        is_bright[i] = raw[i] > median_bright;
#ifndef STDOUT_SILENT
    printf("ISO pattern     : %c%c%c%c %s\n", is_bright[0] ? 'B' : 'd', is_bright[1] ? 'B' : 'd', is_bright[2] ? 'B' : 'd', is_bright[3] ? 'B' : 'd', rggb ? "RGGB" : "GBRG");
#endif
    if (is_bright[0] + is_bright[1] + is_bright[2] + is_bright[3] != 2)
    {
#ifndef STDOUT_SILENT
        printf("Bright/dark detection error\n");
#endif
        return 0;
    }
    
    if (is_bright[0] == is_bright[2] || is_bright[1] == is_bright[3])
    {
#ifndef STDOUT_SILENT
        printf("Interlacing method not supported\n");
#endif
        return 0;
    }
    return 1;
}

static int match_exposures(struct raw_info raw_info, uint32_t * raw_buffer_32, double * corr_ev, int * white_darkened, int * is_bright)
{
    /* guess ISO - find the factor and the offset for matching the bright and dark images */
    int black20 = raw_info.black_level;
    int white20 = MIN(raw_info.white_level, *white_darkened);
    int black = black20/16;
    int white = white20/16;
    int clip0 = white - black;
    int clip  = clip0 * 0.95;    /* there may be nonlinear response in very bright areas */
    
    int w = raw_info.width;
    int h = raw_info.height;
    int y0 = raw_info.active_area.y1 + 2;
    
    /* quick interpolation for matching */
    int* dark   = malloc(w * h * sizeof(dark[0]));
    int* bright = malloc(w * h * sizeof(bright[0]));
    memset(dark, 0, w * h * sizeof(dark[0]));
    memset(bright, 0, w * h * sizeof(bright[0]));
    
    for (int y = y0; y < h-2; y += 3)
    {
        int* native = BRIGHT_ROW ? bright : dark;
        int* interp = BRIGHT_ROW ? dark : bright;
        
        for (int x = 0; x < w; x += 3)
        {
            int pa = raw_get_pixel_20to16(x, y-2) - black;
            int pb = raw_get_pixel_20to16(x, y+2) - black;
            int pn = raw_get_pixel_20to16(x, y) - black;
            int pi = (pa + pb + 1) / 2;
            if (pa >= clip || pb >= clip) pi = clip0;               /* pixel too bright? discard */
            if (pi >= clip) pn = clip0;                             /* interpolated pixel not good? discard the other one too */
            interp[x + y * w] = pi;
            native[x + y * w] = pn;
        }
    }
    
    /*
     * Robust line fit (match unclipped data):
     * - use (median_bright, median_dark) as origin
     * - select highlights between 98 and 99.9th percentile to find the slope (ISO)
     * - choose the slope that explains the largest number of highlight points (inspired from RANSAC)
     *
     * Rationale:
     * - exposure matching is important to be correct in bright_highlights (which are combined with dark_midtones)
     * - low percentiles are likely affected by noise (this process is essentially a histogram matching)
     * - as ad-hoc as it looks, it's the only method that passed all the test samples so far.
     */
    int nmax = (w+2) * (h+2) / 9;   /* downsample by 3x3 for speed */
    int * tmp = malloc(nmax * sizeof(tmp[0]));
    
    /* median_bright */
    int n = 0;
    for (int y = y0; y < h-2; y += 3)
    {
        for (int x = 0; x < w; x += 3)
        {
            int b = bright[x + y*w];
            if (b >= clip) continue;
            tmp[n++] = b;
        }
    }
    int bmed = median_int_wirth(tmp, n);
    
    int * bps = 0;
    
    /* also compute the range for bright pixels (used to find the slope) */
    int b_lo = kth_smallest_int(tmp, n, n*98/100);
    int b_hi = kth_smallest_int(tmp, n, n*99.9/100);
    
    /* median_dark */
    n = 0;
    for (int y = y0; y < h-2; y += 3)
    {
        for (int x = 0; x < w; x += 3)
        {
            int d = dark[x + y*w];
            int b = bright[x + y*w];
            if (b >= clip) continue;
            tmp[n++] = d;
        }
    }
    int dmed = median_int_wirth(tmp, n);
    
    int * dps = 0;
    
    /* select highlights used to find the slope (ISO) */
    /* (98th percentile => up to 2% highlights) */
    int hi_nmax = nmax/50;
    int hi_n = 0;
    int* hi_dark = malloc(hi_nmax * sizeof(hi_dark[0]));
    int* hi_bright = malloc(hi_nmax * sizeof(hi_bright[0]));
    
    for (int y = y0; y < h-2; y += 3)
    {
        for (int x = 0; x < w; x += 3)
        {
            int d = dark[x + y*w];
            int b = bright[x + y*w];
            if (b >= b_hi) continue;
            if (b <= b_lo) continue;
            hi_dark[hi_n] = d;
            hi_bright[hi_n] = b;
            hi_n++;
            if (hi_n >= hi_nmax) break;
        }
    }
    
    //~ printf("Selected %d highlight points (max %d)\n", hi_n, hi_nmax);
    
    double a = 0;
    double b = 0;
    
    int best_score = 0;
    for (double ev = 0; ev < 6; ev += 0.002)
    {
        double test_a = pow(2, -ev);
        double test_b = dmed - bmed * test_a;
        
        int score = 0;
        for (int i = 0; i < hi_n; i++)
        {
            int d = hi_dark[i];
            int b = hi_bright[i];
            int e = d - (b*test_a + test_b);
            if (ABS(e) < 50) score++;
        }
        if (score > best_score)
        {
            best_score = score;
            a = test_a;
            b = test_b;
            //~ printf("%f: %d\n", a, score);
        }
    }
    free(hi_dark); hi_dark = 0;
    free(hi_bright); hi_bright = 0;
    free(tmp); tmp = 0;
    
    free(dark);
    free(bright);
    if (dps) free(dps);
    if (bps) free(bps);
    
    /* apply the correction */
    double b20 = b * 16;
    for (int y = 0; y < h; y ++)
    {
        for (int x = 0; x < w; x ++)
        {
            int p = raw_get_pixel32(x, y);
            if (p == 0) continue;
            
            if (BRIGHT_ROW)
            {
                /* bright exposure: darken and apply the black offset (fixme: why not half?) */
                p = (p - black20) * a + black20 + b20*a;
            }
            else
            {
                p = p - b20 + b20*a;
            }
            
            /* out of range? */
            /* note: this breaks M24-1127 */
            p = COERCE(p, 0, 0xFFFFF);
            
            raw_set_pixel20(x, y, p);
        }
    }
    *white_darkened = (white20 - black20 + b20) * a + black20;
    
    double factor = 1/a;
    if (factor < 1.2 || !isfinite(factor))
    {
#ifndef STDOUT_SILENT
        printf("Doesn't look like interlaced ISO\n");
#endif
        return 0;
    }
    
    *corr_ev = log2(factor);
#ifndef STDOUT_SILENT
    printf("ISO difference  : %.2f EV (%d)\n", log2(factor), (int)round(factor*100));
    printf("Black delta     : %.2f\n", b/4); /* we want to display black delta for the 14-bit original data, but we have computed it from 16-bit data */
#endif
    return 1;
}

static inline uint32_t * convert_to_20bit(struct raw_info raw_info, uint16_t * image_data)
{
    int w = raw_info.width;
    int h = raw_info.height;
    /* promote from 14 to 20 bits (original raw buffer holds 14-bit values stored as uint16_t) */
    uint32_t * raw_buffer_32 = malloc(w * h * sizeof(raw_buffer_32[0]));
    
    for (int y = 0; y < h; y ++)
        for (int x = 0; x < w; x ++)
            raw_buffer_32[x + y*w] = raw_get_pixel_14to20(x, y);
    
    return raw_buffer_32;
}

static inline void build_ev2raw_lut(int * raw2ev, int * ev2raw_0, int black, int white)
{
    int* ev2raw = ev2raw_0 + 10*EV_RESOLUTION;
    
    for (int i = 0; i < 1<<20; i++)
    {
        double signal = MAX(i/64.0 - black/64.0, -1023);
        if (signal > 0)
            raw2ev[i] = (int)round(log2(1+signal) * EV_RESOLUTION);
        else
            raw2ev[i] = -(int)round(log2(1-signal) * EV_RESOLUTION);
    }
    
    for (int i = -10*EV_RESOLUTION; i < 0; i++)
    {
        ev2raw[i] = COERCE(black+64 - round(64*pow(2, ((double)-i/EV_RESOLUTION))), 0, black);
    }
    
    for (int i = 0; i < 14*EV_RESOLUTION; i++)
    {
        ev2raw[i] = COERCE(black-64 + round(64*pow(2, ((double)i/EV_RESOLUTION))), black, (1<<20)-1);
        
        if (i >= raw2ev[white])
        {
            ev2raw[i] = MAX(ev2raw[i], white);
        }
    }
    
    /* keep "bad" pixels, if any */
    ev2raw[raw2ev[0]] = 0;
    ev2raw[raw2ev[0]] = 0;
    
    /* check raw <--> ev conversion */
    //~ printf("%d %d %d %d %d %d %d *%d* %d %d %d %d %d\n", raw2ev[0],         raw2ev[16000],         raw2ev[32000],         raw2ev[131068],         raw2ev[131069],         raw2ev[131070],         raw2ev[131071],         raw2ev[131072],         raw2ev[131073],         raw2ev[131074],         raw2ev[131075],         raw2ev[131076],         raw2ev[132000]);
    //~ printf("%d %d %d %d %d %d %d *%d* %d %d %d %d %d\n", ev2raw[raw2ev[0]], ev2raw[raw2ev[16000]], ev2raw[raw2ev[32000]], ev2raw[raw2ev[131068]], ev2raw[raw2ev[131069]], ev2raw[raw2ev[131070]], ev2raw[raw2ev[131071]], ev2raw[raw2ev[131072]], ev2raw[raw2ev[131073]], ev2raw[raw2ev[131074]], ev2raw[raw2ev[131075]], ev2raw[raw2ev[131076]], ev2raw[raw2ev[132000]]);
}

static inline double compute_noise(struct raw_info raw_info, uint16_t * image_data, double * noise_std, double * dark_noise, double * bright_noise, double * dark_noise_ev, double * bright_noise_ev)
{
    double noise_avg = 0.0;
    for (int y = 0; y < 4; y++)
        compute_black_noise(raw_info, image_data, 8, raw_info.active_area.x1 - 8, raw_info.active_area.y1/4*4 + 20 + y, raw_info.active_area.y2 - 20, 1, 4, &noise_avg, &noise_std[y]);
#ifndef STDOUT_SILENT
    printf("Noise levels    : %.02f %.02f %.02f %.02f (14-bit)\n", noise_std[0], noise_std[1], noise_std[2], noise_std[3]);
#endif
    *dark_noise = MIN(MIN(noise_std[0], noise_std[1]), MIN(noise_std[2], noise_std[3]));
    *bright_noise = MAX(MAX(noise_std[0], noise_std[1]), MAX(noise_std[2], noise_std[3]));
    *dark_noise_ev = log2(*dark_noise);
    *bright_noise_ev = log2(*bright_noise);
    return noise_avg;
}

static inline double * build_fullres_curve(int black)
{
    /* fullres mixing curve */
    static double fullres_curve[1<<20];
    static int previous_black = -1;
    
    if(previous_black == black) return fullres_curve;
    
    previous_black = black;
    
    const double fullres_start = 4;
    const double fullres_transition = 4;
    //const double fullres_thr = 0.8;
    
    for (int i = 0; i < (1<<20); i++)
    {
        double ev2 = log2(MAX(i/64.0 - black/64.0, 1));
        double c2 = -cos(COERCE(ev2 - fullres_start, 0, fullres_transition)*M_PI/fullres_transition);
        double f = (c2+1) / 2;
        fullres_curve[i] = f;
    }
    
    return fullres_curve;
}

/* define edge directions for interpolation */
struct xy { int x; int y; };
const struct
{
    struct xy ack;      /* verification pixel near a */
    struct xy a;        /* interpolation pixel from the nearby line: normally (0,s) but also (1,s) or (-1,s) */
    struct xy b;        /* interpolation pixel from the other line: normally (0,-2s) but also (1,-2s), (-1,-2s), (2,-2s) or (-2,-2s) */
    struct xy bck;      /* verification pixel near b */
}
edge_directions[] = {       /* note: all y coords should be multiplied by s */
    //~ { {-6,2}, {-3,1}, { 6,-2}, { 9,-3} },     /* almost horizontal (little or no improvement) */
    { {-4,2}, {-2,1}, { 4,-2}, { 6,-3} },
    { {-3,2}, {-1,1}, { 3,-2}, { 4,-3} },
    { {-2,2}, {-1,1}, { 2,-2}, { 3,-3} },     /* 45-degree diagonal */
    { {-1,2}, {-1,1}, { 1,-2}, { 2,-3} },
    { {-1,2}, { 0,1}, { 1,-2}, { 1,-3} },
    { { 0,2}, { 0,1}, { 0,-2}, { 0,-3} },     /* vertical, preferred; no extra confirmations needed */
    { { 1,2}, { 0,1}, {-1,-2}, {-1,-3} },
    { { 1,2}, { 1,1}, {-1,-2}, {-2,-3} },
    { { 2,2}, { 1,1}, {-2,-2}, {-3,-3} },     /* 45-degree diagonal */
    { { 3,2}, { 1,1}, {-3,-2}, {-4,-3} },
    { { 4,2}, { 2,1}, {-4,-2}, {-6,-3} },
    //~ { { 6,2}, { 3,1}, {-6,-2}, {-9,-3} },     /* almost horizontal */
};

static inline int edge_interp(float ** plane, int * squeezed, int * raw2ev, int dir, int x, int y, int s)
{
    
    int dxa = edge_directions[dir].a.x;
    int dya = edge_directions[dir].a.y * s;
    int pa = COERCE((int)plane[squeezed[y+dya]][x+dxa], 0, 0xFFFFF);
    int dxb = edge_directions[dir].b.x;
    int dyb = edge_directions[dir].b.y * s;
    int pb = COERCE((int)plane[squeezed[y+dyb]][x+dxb], 0, 0xFFFFF);
    int pi = (raw2ev[pa] * 2 + raw2ev[pb]) / 3;
    
    return pi;
}

static inline void amaze_interpolate(struct raw_info raw_info, uint32_t * raw_buffer_32, uint32_t* dark, uint32_t* bright, int black, int white, int white_darkened, int * is_bright)
{
    int w = raw_info.width;
    int h = raw_info.height;
    
    int* squeezed = malloc(h * sizeof(int));
    memset(squeezed, 0, h * sizeof(int));
    
    float** rawData = malloc(h * sizeof(rawData[0]));
    float** red     = malloc(h * sizeof(red[0]));
    float** green   = malloc(h * sizeof(green[0]));
    float** blue    = malloc(h * sizeof(blue[0]));
    
    for (int i = 0; i < h; i++)
    {
        int wx = w + 16;
        rawData[i] =   malloc(wx * sizeof(rawData[0][0]));
        memset(rawData[i], 0, wx * sizeof(rawData[0][0]));
        red[i]     = malloc(wx * sizeof(red[0][0]));
        green[i]   = malloc(wx * sizeof(green[0][0]));
        blue[i]    = malloc(wx * sizeof(blue[0][0]));
    }
    
    /* squeeze the dark image by deleting fields from the bright exposure */
    int yh = -1;
    for (int y = 0; y < h; y ++)
    {
        if (BRIGHT_ROW)
            continue;
        
        if (yh < 0) /* make sure we start at the same parity (RGGB cell) */
            yh = y;
        
        for (int x = 0; x < w; x++)
        {
            int p = raw_get_pixel32(x, y);
            
            if (x%2 != y%2) /* divide green channel by 2 to approximate the final WB better */
                p = (p - black) / 2 + black;
            
            rawData[yh][x] = p;
        }
        
        squeezed[y] = yh;
        
        yh++;
    }
    
    /* now the same for the bright exposure */
    yh = -1;
    for (int y = 0; y < h; y ++)
    {
        if (!BRIGHT_ROW)
            continue;
        
        if (yh < 0) /* make sure we start with the same parity (RGGB cell) */
            yh = h/4*2 + y;
        
        for (int x = 0; x < w; x++)
        {
            int p = raw_get_pixel32(x, y);
            
            if (x%2 != y%2) /* divide green channel by 2 to approximate the final WB better */
                p = (p - black) / 2 + black;
            
            rawData[yh][x] = p;
        }
        
        squeezed[y] = yh;
        
        yh++;
        if (yh >= h) break; /* just in case */
    }
#if 0
    void amaze_demosaic_RT(
                           float** rawData,    /* holds preprocessed pixel values, rawData[i][j] corresponds to the ith row and jth column */
                           float** red,        /* the interpolated red plane */
                           float** green,      /* the interpolated green plane */
                           float** blue,       /* the interpolated blue plane */
                           int winx, int winy, /* crop window for demosaicing */
                           int winw, int winh
                           );
#endif
    //IDK if AMaZE is actually thread safe, but I'm just going to assume not, rather than inspecting that huge mess of code
    LOCK(amaze_mutex)
    {
        demosaic(& (amazeinfo_t) { rawData, red, green, blue, 0, 0, w, h, 0 });
    }
    UNLOCK(amaze_mutex)
    
    /* undo green channel scaling and clamp the other channels */
    for (int y = 0; y < h; y ++)
    {
        for (int x = 0; x < w; x ++)
        {
            green[y][x] = COERCE((green[y][x] - black) * 2 + black, 0, 0xFFFFF);
            red[y][x] = COERCE(red[y][x], 0, 0xFFFFF);
            blue[y][x] = COERCE(blue[y][x], 0, 0xFFFFF);
        }
    }
#ifndef STDOUT_SILENT
    printf("Edge-directed interpolation...\n");
#endif
    //~ printf("Grayscale...\n");
    /* convert to grayscale and de-squeeze for easier processing */
    uint32_t * gray = malloc(w * h * sizeof(gray[0]));
    for (int y = 0; y < h; y ++)
        for (int x = 0; x < w; x ++)
            gray[x + y*w] = green[squeezed[y]][x]/2 + red[squeezed[y]][x]/4 + blue[squeezed[y]][x]/4;
    
    
    uint8_t* edge_direction = malloc(w * h * sizeof(edge_direction[0]));
    int d0 = COUNT(edge_directions)/2;
    for (int y = 0; y < h; y ++)
        for (int x = 0; x < w; x ++)
            edge_direction[x + y*w] = d0;
    
    double * fullres_curve = build_fullres_curve(black);
    
    //~ printf("Cross-correlation...\n");
    int semi_overexposed = 0;
    int not_overexposed = 0;
    int deep_shadow = 0;
    int not_shadow = 0;
    
    /* for fast EV - raw conversion */
    static int raw2ev[1<<20];   /* EV x EV_RESOLUTION */
    static int ev2raw_0[24*EV_RESOLUTION];
    static int previous_black = -1;
    
    /* handle sub-black values (negative EV) */
    int* ev2raw = ev2raw_0 + 10*EV_RESOLUTION;
    
    LOCK(ev2raw_mutex)
    {
        if(black != previous_black)
        {
            build_ev2raw_lut(raw2ev, ev2raw_0, black, white);
            previous_black = black;
        }
        for (int y = 5; y < h-5; y ++)
        {
            int s = (is_bright[y%4] == is_bright[(y+1)%4]) ? -1 : 1;    /* points to the closest row having different exposure */
            for (int x = 5; x < w-5; x ++)
            {
                int e_best = INT_MAX;
                int d_best = d0;
                int dmin = 0;
                int dmax = COUNT(edge_directions)-1;
                int search_area = 5;
                
                /* only use high accuracy on the dark exposure where the bright ISO is overexposed */
                if (!BRIGHT_ROW)
                {
                    /* interpolating bright exposure */
                    if (fullres_curve[raw_get_pixel32(x, y)] > fullres_thr)
                    {
                        /* no high accuracy needed, just interpolate vertically */
                        not_shadow++;
                        dmin = d0;
                        dmax = d0;
                    }
                    else
                    {
                        /* deep shadows, unlikely to use fullres, so we need a good interpolation */
                        deep_shadow++;
                    }
                }
                else if (raw_get_pixel32(x, y) < (unsigned int)white_darkened)
                {
                    /* interpolating dark exposure, but we also have good data from the bright one */
                    not_overexposed++;
                    dmin = d0;
                    dmax = d0;
                }
                else
                {
                    /* interpolating dark exposure, but the bright one is clipped */
                    semi_overexposed++;
                }
                
                if (dmin == dmax)
                {
                    d_best = dmin;
                }
                else
                {
                    for (int d = dmin; d <= dmax; d++)
                    {
                        int e = 0;
                        for (int j = -search_area; j <= search_area; j++)
                        {
                            int dx1 = edge_directions[d].ack.x + j;
                            int dy1 = edge_directions[d].ack.y * s;
                            int p1 = raw2ev[gray[x+dx1 + (y+dy1)*w]];
                            int dx2 = edge_directions[d].a.x + j;
                            int dy2 = edge_directions[d].a.y * s;
                            int p2 = raw2ev[gray[x+dx2 + (y+dy2)*w]];
                            int dx3 = edge_directions[d].b.x + j;
                            int dy3 = edge_directions[d].b.y * s;
                            int p3 = raw2ev[gray[x+dx3 + (y+dy3)*w]];
                            int dx4 = edge_directions[d].bck.x + j;
                            int dy4 = edge_directions[d].bck.y * s;
                            int p4 = raw2ev[gray[x+dx4 + (y+dy4)*w]];
                            e += ABS(p1-p2) + ABS(p2-p3) + ABS(p3-p4);
                        }
                        
                        /* add a small penalty for diagonal directions */
                        /* (the improvement should be significant in order to choose one of these) */
                        e += ABS(d - d0) * EV_RESOLUTION/8;
                        
                        if (e < e_best)
                        {
                            e_best = e;
                            d_best = d;
                        }
                    }
                }
                
                edge_direction[x + y*w] = d_best;
            }
        }
#ifndef STDOUT_SILENT
        printf("Semi-overexposed: %.02f%%\n", semi_overexposed * 100.0 / (semi_overexposed + not_overexposed));
        printf("Deep shadows    : %.02f%%\n", deep_shadow * 100.0 / (deep_shadow + not_shadow));
#endif
        //~ printf("Actual interpolation...\n");
        
        for (int y = 2; y < h-2; y ++)
        {
            uint32_t* native = BRIGHT_ROW ? bright : dark;
            uint32_t* interp = BRIGHT_ROW ? dark : bright;
            int is_rg = (y % 2 == 0); /* RG or GB? */
            int s = (is_bright[y%4] == is_bright[(y+1)%4]) ? -1 : 1;    /* points to the closest row having different exposure */
            
            //~ printf("Interpolating %s line %d from [near] %d (squeezed %d) and [far] %d (squeezed %d)\n", BRIGHT_ROW ? "BRIGHT" : "DARK", y, y+s, yh_near, y-2*s, yh_far);
            
            for (int x = 2; x < w-2; x += 2)
            {
                for (int k = 0; k < 2; k++, x++)
                {
                    float** plane = is_rg ? (x%2 == 0 ? red   : green)
                    : (x%2 == 0 ? green : blue );
                    
                    int dir = edge_direction[x + y*w];
                    
                    /* vary the interpolation direction and average the result (reduces aliasing) */
                    int pi0 = edge_interp(plane, squeezed, raw2ev, dir, x, y, s);
                    int pip = edge_interp(plane, squeezed, raw2ev, MIN(dir+1, COUNT(edge_directions)-1), x, y, s);
                    int pim = edge_interp(plane, squeezed, raw2ev, MAX(dir-1,0), x, y, s);
                    
                    interp[x   + y * w] = ev2raw[(2*pi0+pip+pim)/4];
                    native[x   + y * w] = raw_get_pixel32(x, y);
                }
                x -= 2;
            }
        }
    }
    UNLOCK(ev2raw_mutex)
    
    for (int i = 0; i < h; i++)
    {
        free(rawData[i]);
        free(red[i]);
        free(green[i]);
        free(blue[i]);
    }
    
    free(squeezed); squeezed = 0;
    free(rawData); rawData = 0;
    free(red); red = 0;
    free(green); green = 0;
    free(blue); blue = 0;
    free(gray); gray = 0;
    free(edge_direction);
}

static inline void mean23_interpolate(struct raw_info raw_info, uint32_t * raw_buffer_32, uint32_t* dark, uint32_t* bright, int black, int white, int white_darkened, int * is_bright)
{
    int w = raw_info.width;
    int h = raw_info.height;
#ifndef STDOUT_SILENT
    printf("Interpolation   : mean23\n");
#endif
    /* for fast EV - raw conversion */
    static int raw2ev[1<<20];   /* EV x EV_RESOLUTION */
    static int ev2raw_0[24*EV_RESOLUTION];
    static int previous_black = -1;
    
    /* handle sub-black values (negative EV) */
    int* ev2raw = ev2raw_0 + 10*EV_RESOLUTION;
    
    LOCK(ev2raw_mutex)
    {
        if(black != previous_black)
        {
            build_ev2raw_lut(raw2ev, ev2raw_0, black, white);
            previous_black = black;
        }
        for (int y = 2; y < h-2; y ++)
        {
            uint32_t* native = BRIGHT_ROW ? bright : dark;
            uint32_t* interp = BRIGHT_ROW ? dark : bright;
            int is_rg = (y % 2 == 0); /* RG or GB? */
            int white = !BRIGHT_ROW ? white_darkened : raw_info.white_level;
            
            for (int x = 2; x < w-3; x += 2)
            {
                
                /* red/blue: interpolate from (x,y+2) and (x,y-2) */
                /* green: interpolate from (x+1,y+1),(x-1,y+1),(x,y-2) or (x+1,y-1),(x-1,y-1),(x,y+2), whichever has the correct brightness */
                
                int s = (is_bright[y%4] == is_bright[(y+1)%4]) ? -1 : 1;
                
                if (is_rg)
                {
                    int ra = raw_get_pixel32(x, y-2);
                    int rb = raw_get_pixel32(x, y+2);
                    int ri = mean2(raw2ev[ra], raw2ev[rb], raw2ev[white], 0);
                    
                    int ga = raw_get_pixel32(x+1+1, y+s);
                    int gb = raw_get_pixel32(x+1-1, y+s);
                    int gc = raw_get_pixel32(x+1, y-2*s);
                    int gi = mean3(raw2ev[ga], raw2ev[gb], raw2ev[gc], raw2ev[white], 0);
                    
                    interp[x   + y * w] = ev2raw[ri];
                    interp[x+1 + y * w] = ev2raw[gi];
                }
                else
                {
                    int ba = raw_get_pixel32(x+1  , y-2);
                    int bb = raw_get_pixel32(x+1  , y+2);
                    int bi = mean2(raw2ev[ba], raw2ev[bb], raw2ev[white], 0);
                    
                    int ga = raw_get_pixel32(x+1, y+s);
                    int gb = raw_get_pixel32(x-1, y+s);
                    int gc = raw_get_pixel32(x, y-2*s);
                    int gi = mean3(raw2ev[ga], raw2ev[gb], raw2ev[gc], raw2ev[white], 0);
                    
                    interp[x   + y * w] = ev2raw[gi];
                    interp[x+1 + y * w] = ev2raw[bi];
                }
                
                native[x   + y * w] = raw_get_pixel32(x, y);
                native[x+1 + y * w] = raw_get_pixel32(x+1, y);
            }
        }
    }
    UNLOCK(ev2raw_mutex)
}

static inline void border_interpolate(struct raw_info raw_info, uint32_t * raw_buffer_32, uint32_t* dark, uint32_t* bright, int * is_bright)
{
    int w = raw_info.width;
    int h = raw_info.height;
    
    /* border interpolation */
    for (int y = 0; y < 3; y ++)
    {
        uint32_t* native = BRIGHT_ROW ? bright : dark;
        uint32_t* interp = BRIGHT_ROW ? dark : bright;
        
        for (int x = 0; x < w; x ++)
        {
            interp[x + y * w] = raw_get_pixel32(x, y+2);
            native[x + y * w] = raw_get_pixel32(x, y);
        }
    }
    
    for (int y = h-4; y < h; y ++)
    {
        uint32_t* native = BRIGHT_ROW ? bright : dark;
        uint32_t* interp = BRIGHT_ROW ? dark : bright;
        
        for (int x = 0; x < w; x ++)
        {
            interp[x + y * w] = raw_get_pixel32(x, y-2);
            native[x + y * w] = raw_get_pixel32(x, y);
        }
    }
    
    for (int y = 2; y < h; y ++)
    {
        uint32_t* native = BRIGHT_ROW ? bright : dark;
        uint32_t* interp = BRIGHT_ROW ? dark : bright;
        
        for (int x = 0; x < 2; x ++)
        {
            interp[x + y * w] = raw_get_pixel32(x, y-2);
            native[x + y * w] = raw_get_pixel32(x, y);
        }
        
        for (int x = w-3; x < w; x ++)
        {
            interp[x + y * w] = raw_get_pixel32(x-2, y-2);
            native[x + y * w] = raw_get_pixel32(x-2, y);
        }
    }
}

static inline void fullres_reconstruction(struct raw_info raw_info, uint32_t * fullres, uint32_t* dark, uint32_t* bright, uint32_t white_darkened, int * is_bright)
{
    int w = raw_info.width;
    int h = raw_info.height;
    
    /* reconstruct a full-resolution image (discard interpolated fields whenever possible) */
    /* this has full detail and lowest possible aliasing, but it has high shadow noise and color artifacts when high-iso starts clipping */
#ifndef STDOUT_SILENT
    printf("Full-res reconstruction...\n");
#endif
    for (int y = 0; y < h; y ++)
    {
        for (int x = 0; x < w; x ++)
        {
            if (BRIGHT_ROW)
            {
                uint32_t f = bright[x + y*w];
                /* if the brighter copy is overexposed, the guessed pixel for sure has higher brightness */
                fullres[x + y*w] = f < white_darkened ? f : MAX(f, dark[x + y*w]);
            }
            else
            {
                fullres[x + y*w] = dark[x + y*w];
            }
        }
    }
}

static inline void build_alias_map(struct raw_info raw_info, uint16_t* alias_map, uint32_t* fullres_smooth, uint32_t* halfres_smooth, uint32_t* bright, int dark_noise, int black, int * raw2ev)
{
    if(!alias_map) return;
    
    int w = raw_info.width;
    int h = raw_info.height;
    
    double * fullres_curve = build_fullres_curve(black);
#ifndef STDOUT_SILENT
    printf("Building alias map...\n");
#endif
    uint16_t* alias_aux = malloc(w * h * sizeof(uint16_t));
    
    /* build the aliasing maps (where it's likely to get aliasing) */
    /* do this by comparing fullres and halfres images */
    /* if the difference is small, we'll prefer halfres for less noise, otherwise fullres for less aliasing */
    for (int y = 0; y < h; y ++)
    {
        for (int x = 0; x < w; x ++)
        {
            /* do not compute alias map where we'll use fullres detail anyway */
            if (fullres_curve[bright[x + y*w]] > fullres_thr)
                continue;
            
            int f = fullres_smooth[x + y*w];
            int h = halfres_smooth[x + y*w];
            int fe = raw2ev[f];
            int he = raw2ev[h];
            int e_lin = ABS(f - h); /* error in linear space, for shadows (downweights noise) */
            e_lin = MAX(e_lin - dark_noise*3/2, 0);
            int e_log = ABS(fe - he); /* error in EV space, for highlights (highly sensitive to noise) */
            alias_map[x + y*w] = MIN(MIN(e_lin/2, e_log/16), 65530);
        }
    }
    
    memcpy(alias_aux, alias_map, w * h * sizeof(uint16_t));
#ifndef STDOUT_SILENT
    printf("Filtering alias map...\n");
#endif
    for (int y = 6; y < h-6; y ++)
    {
        for (int x = 6; x < w-6; x ++)
        {
            /* do not compute alias map where we'll use fullres detail anyway */
            if (fullres_curve[bright[x + y*w]] > fullres_thr)
                continue;
            
            /* use 5th max (out of 37) to filter isolated pixels */
            int neighbours[] = {
                                                                              -alias_map[x-2 + (y-6) * w], -alias_map[x+0 + (y-6) * w], -alias_map[x+2 + (y-6) * w],
                                                 -alias_map[x-4 + (y-4) * w], -alias_map[x-2 + (y-4) * w], -alias_map[x+0 + (y-4) * w], -alias_map[x+2 + (y-4) * w], -alias_map[x+4 + (y-4) * w],
                    -alias_map[x-6 + (y-2) * w], -alias_map[x-4 + (y-2) * w], -alias_map[x-2 + (y-2) * w], -alias_map[x+0 + (y-2) * w], -alias_map[x+2 + (y-2) * w], -alias_map[x+4 + (y-2) * w], -alias_map[x+6 + (y-2) * w], 
                    -alias_map[x-6 + (y+0) * w], -alias_map[x-4 + (y+0) * w], -alias_map[x-2 + (y+0) * w], -alias_map[x+0 + (y+0) * w], -alias_map[x+2 + (y+0) * w], -alias_map[x+4 + (y+0) * w], -alias_map[x+6 + (y+0) * w], 
                    -alias_map[x-6 + (y+2) * w], -alias_map[x-4 + (y+2) * w], -alias_map[x-2 + (y+2) * w], -alias_map[x+0 + (y+2) * w], -alias_map[x+2 + (y+2) * w], -alias_map[x+4 + (y+2) * w], -alias_map[x+6 + (y+2) * w], 
                                                 -alias_map[x-4 + (y+4) * w], -alias_map[x-2 + (y+4) * w], -alias_map[x+0 + (y+4) * w], -alias_map[x+2 + (y+4) * w], -alias_map[x+4 + (y+4) * w],
                                                                              -alias_map[x-2 + (y+6) * w], -alias_map[x+0 + (y+6) * w], -alias_map[x+2 + (y+6) * w],
            };
            alias_aux[x + y * w] = -kth_smallest_int(neighbours, COUNT(neighbours), 5);
        }
    }
#ifndef STDOUT_SILENT
    printf("Smoothing alias map...\n");
#endif
    /* gaussian blur */
    for (int y = 6; y < h-6; y ++)
    {
        for (int x = 6; x < w-6; x ++)
        {
            /* do not compute alias map where we'll use fullres detail anyway */
            if (fullres_curve[bright[x + y*w]] > fullres_thr)
                continue;
            
            int c =
            (alias_aux[x+0 + (y+0) * w])+
            (alias_aux[x+0 + (y-2) * w] + alias_aux[x-2 + (y+0) * w] + alias_aux[x+2 + (y+0) * w] + alias_aux[x+0 + (y+2) * w]) * 820 / 1024 +
            (alias_aux[x-2 + (y-2) * w] + alias_aux[x+2 + (y-2) * w] + alias_aux[x-2 + (y+2) * w] + alias_aux[x+2 + (y+2) * w]) * 657 / 1024 +
            (alias_aux[x+0 + (y-2) * w] + alias_aux[x-2 + (y+0) * w] + alias_aux[x+2 + (y+0) * w] + alias_aux[x+0 + (y+2) * w]) * 421 / 1024 +
            (alias_aux[x-2 + (y-2) * w] + alias_aux[x+2 + (y-2) * w] + alias_aux[x-2 + (y-2) * w] + alias_aux[x+2 + (y-2) * w] + alias_aux[x-2 + (y+2) * w] + alias_aux[x+2 + (y+2) * w] + alias_aux[x-2 + (y+2) * w] + alias_aux[x+2 + (y+2) * w]) * 337 / 1024 +
            (alias_aux[x-2 + (y-2) * w] + alias_aux[x+2 + (y-2) * w] + alias_aux[x-2 + (y+2) * w] + alias_aux[x+2 + (y+2) * w]) * 173 / 1024 +
            (alias_aux[x+0 + (y-6) * w] + alias_aux[x-6 + (y+0) * w] + alias_aux[x+6 + (y+0) * w] + alias_aux[x+0 + (y+6) * w]) * 139 / 1024 +
            (alias_aux[x-2 + (y-6) * w] + alias_aux[x+2 + (y-6) * w] + alias_aux[x-6 + (y-2) * w] + alias_aux[x+6 + (y-2) * w] + alias_aux[x-6 + (y+2) * w] + alias_aux[x+6 + (y+2) * w] + alias_aux[x-2 + (y+6) * w] + alias_aux[x+2 + (y+6) * w]) * 111 / 1024 +
            (alias_aux[x-2 + (y-6) * w] + alias_aux[x+2 + (y-6) * w] + alias_aux[x-6 + (y-2) * w] + alias_aux[x+6 + (y-2) * w] + alias_aux[x-6 + (y+2) * w] + alias_aux[x+6 + (y+2) * w] + alias_aux[x-2 + (y+6) * w] + alias_aux[x+2 + (y+6) * w]) * 57 / 1024;
            alias_map[x + y * w] = c;
        }
    }
    
    /* make it grayscale */
    for (int y = 2; y < h-2; y += 2)
    {
        for (int x = 2; x < w-2; x += 2)
        {
            int a = alias_map[x   +     y * w];
            int b = alias_map[x+1 +     y * w];
            int c = alias_map[x   + (y+1) * w];
            int d = alias_map[x+1 + (y+1) * w];
            int C = MAX(MAX(a,b), MAX(c,d));
            
            C = MIN(C, ALIAS_MAP_MAX);
            
            alias_map[x   +     y * w] =
            alias_map[x+1 +     y * w] =
            alias_map[x   + (y+1) * w] =
            alias_map[x+1 + (y+1) * w] = C;
        }
    }
    
    free(alias_aux);
}

#define CHROMA_SMOOTH_TYPE uint32_t

#define CHROMA_SMOOTH_2X2
#include "chroma_smooth.c"
#undef CHROMA_SMOOTH_2X2

#define CHROMA_SMOOTH_3X3
#include "chroma_smooth.c"
#undef CHROMA_SMOOTH_3X3

#define CHROMA_SMOOTH_5X5
#include "chroma_smooth.c"
#undef CHROMA_SMOOTH_5X5

static inline void hdr_chroma_smooth(struct raw_info raw_info, uint32_t * input, uint32_t * output, int method, int * raw2ev, int * ev2raw)
{
    int w = raw_info.width;
    int h = raw_info.height;
    int black = raw_info.black_level;
    int white = raw_info.white_level;
    
    switch (method) {
        case 2:
            chroma_smooth_2x2(w, h, input, output, raw2ev, ev2raw, black, white);
            break;
        case 3:
            chroma_smooth_3x3(w, h, input, output, raw2ev, ev2raw, black, white);
            break;
        case 5:
            chroma_smooth_5x5(w, h, input, output, raw2ev, ev2raw, black, white);
            break;
            
        default:
#ifndef STDOUT_SILENT
            err_printf("Unsupported chroma smooth method\n");
#endif
            break;
    }
}

static inline int mix_images(struct raw_info raw_info, uint32_t* fullres, uint32_t* fullres_smooth, uint32_t* halfres, uint32_t* halfres_smooth, uint16_t* alias_map, uint32_t* dark, uint32_t* bright, uint16_t * overexposed, int dark_noise, uint32_t white_darkened, double corr_ev, double lowiso_dr, uint32_t black, uint32_t white, int chroma_smooth_method)
{
    int w = raw_info.width;
    int h = raw_info.height;
    
    /* mix the two images */
    /* highlights:  keep data from dark image only */
    /* shadows:     keep data from bright image only */
    /* midtones:    mix data from both, to bring back the resolution */
    
    /* estimate ISO overlap */
    /*
     ISO 100:       ###...........  (11 stops)
     ISO 1600:  ####..........      (10 stops)
     Combined:  XX##..............  (14 stops)
     */
    double clipped_ev = corr_ev;
    double overlap = lowiso_dr - clipped_ev;
    
    /* you get better colors, less noise, but a little more jagged edges if we underestimate the overlap amount */
    /* maybe expose a tuning factor? (preference towards resolution or colors) */
    overlap -= MIN(3, overlap - 3);
#ifndef STDOUT_SILENT
    printf("ISO overlap     : %.1f EV (approx)\n", overlap);
#endif
    if (overlap < 0.5)
    {
#ifndef STDOUT_SILENT
        printf("Overlap error\n");
#endif
        return 0;
    }
    else if (overlap < 2)
    {
#ifndef STDOUT_SILENT
        printf("Overlap too small, use a smaller ISO difference for better results.\n");
#endif
    }
#ifndef STDOUT_SILENT
    printf("Half-res blending...\n");
#endif
    /* mixing curve */
    double max_ev = log2(white/64 - black/64);
    double * mix_curve = malloc((1<<20) * sizeof(double));
    
    for (int i = 0; i < 1<<20; i++)
    {
        double ev = log2(MAX(i/64.0 - black/64.0, 1)) + corr_ev;
        double c = -cos(MAX(MIN(ev-(max_ev-overlap),overlap),0)*M_PI/overlap);
        double k = (c+1) / 2;
        mix_curve[i] = k;
    }


    /* for fast EV - raw conversion */
    static int raw2ev[1<<20];   /* EV x EV_RESOLUTION */
    static int ev2raw_0[24*EV_RESOLUTION];
    static uint32_t previous_black = -1;
    
    /* handle sub-black values (negative EV) */
    int* ev2raw = ev2raw_0 + 10*EV_RESOLUTION;
    
    LOCK(ev2raw_mutex)
    {
        if(black != previous_black)
        {
            build_ev2raw_lut(raw2ev, ev2raw_0, black, white);
            previous_black = black;
        }
        
        for (int y = 0; y < h; y ++)
        {
            for (int x = 0; x < w; x ++)
            {
                /* bright and dark source pixels  */
                /* they may be real or interpolated */
                /* they both have the same brightness (they were adjusted before this loop), so we are ready to mix them */
                int b = bright[x + y*w];
                int d = dark[x + y*w];
                
                /* go from linear to EV space */
                int bev = raw2ev[b];
                int dev = raw2ev[d];
                
                /* blending factor */
                double k = COERCE(mix_curve[b & 0xFFFFF], 0, 1);
                
                /* mix bright and dark exposures */
                int mixed = bev * (1-k) + dev * k;
                halfres[x + y*w] = ev2raw[mixed];
            }
        }
        if (chroma_smooth_method)
        {
#ifndef STDOUT_SILENT
            printf("Chroma smoothing...\n");
#endif
            memcpy(fullres_smooth, fullres, w * h * sizeof(uint32_t));
            memcpy(halfres_smooth, halfres, w * h * sizeof(uint32_t));
            hdr_chroma_smooth(raw_info, fullres, fullres_smooth, chroma_smooth_method, raw2ev, ev2raw);
            hdr_chroma_smooth(raw_info, halfres, halfres_smooth, chroma_smooth_method, raw2ev, ev2raw);
        }
        if(alias_map)
        {
            build_alias_map(raw_info, alias_map, fullres_smooth, halfres_smooth, bright, dark_noise, black, raw2ev);
        }
    }
    UNLOCK(ev2raw_mutex)
    
    for (int y = 0; y < h; y ++)
    {
        for (int x = 0; x < w; x ++)
        {
            overexposed[x + y * w] = bright[x + y * w] >= white_darkened || dark[x + y * w] >= white ? 100 : 0;
        }
    }
    
    /* "blur" the overexposed map */
    uint16_t* over_aux = malloc(w * h * sizeof(uint16_t));
    memcpy(over_aux, overexposed, w * h * sizeof(uint16_t));
    
    for (int y = 3; y < h-3; y ++)
    {
        for (int x = 3; x < w-3; x ++)
        {
            overexposed[x + y * w] =
            (over_aux[x+0 + (y+0) * w])+
            (over_aux[x+0 + (y-1) * w] + over_aux[x-1 + (y+0) * w] + over_aux[x+1 + (y+0) * w] + over_aux[x+0 + (y+1) * w]) * 820 / 1024 +
            (over_aux[x-1 + (y-1) * w] + over_aux[x+1 + (y-1) * w] + over_aux[x-1 + (y+1) * w] + over_aux[x+1 + (y+1) * w]) * 657 / 1024 +
            //~ (over_aux[x+0 + (y-2) * w] + over_aux[x-2 + (y+0) * w] + over_aux[x+2 + (y+0) * w] + over_aux[x+0 + (y+2) * w]) * 421 / 1024 +
            //~ (over_aux[x-1 + (y-2) * w] + over_aux[x+1 + (y-2) * w] + over_aux[x-2 + (y-1) * w] + over_aux[x+2 + (y-1) * w] + over_aux[x-2 + (y+1) * w] + over_aux[x+2 + (y+1) * w] + over_aux[x-1 + (y+2) * w] + over_aux[x+1 + (y+2) * w]) * 337 / 1024 +
            //~ (over_aux[x-2 + (y-2) * w] + over_aux[x+2 + (y-2) * w] + over_aux[x-2 + (y+2) * w] + over_aux[x+2 + (y+2) * w]) * 173 / 1024 +
            //~ (over_aux[x+0 + (y-3) * w] + over_aux[x-3 + (y+0) * w] + over_aux[x+3 + (y+0) * w] + over_aux[x+0 + (y+3) * w]) * 139 / 1024 +
            //~ (over_aux[x-1 + (y-3) * w] + over_aux[x+1 + (y-3) * w] + over_aux[x-3 + (y-1) * w] + over_aux[x+3 + (y-1) * w] + over_aux[x-3 + (y+1) * w] + over_aux[x+3 + (y+1) * w] + over_aux[x-1 + (y+3) * w] + over_aux[x+1 + (y+3) * w]) * 111 / 1024 +
            //~ (over_aux[x-2 + (y-3) * w] + over_aux[x+2 + (y-3) * w] + over_aux[x-3 + (y-2) * w] + over_aux[x+3 + (y-2) * w] + over_aux[x-3 + (y+2) * w] + over_aux[x+3 + (y+2) * w] + over_aux[x-2 + (y+3) * w] + over_aux[x+2 + (y+3) * w]) * 57 / 1024;
            0;
        }
    }
    
    free(over_aux); over_aux = 0;
    free(mix_curve);
    
    return 1;
}

static inline void final_blend(struct raw_info raw_info, uint32_t* raw_buffer_32, uint32_t* fullres, uint32_t* fullres_smooth, uint32_t* halfres_smooth, uint32_t* dark, uint32_t* bright, uint16_t* overexposed, uint16_t* alias_map, int black, int white, int dark_noise)
{
    /* fullres mixing curve */
    double * fullres_curve = build_fullres_curve(black);
    
    int w = raw_info.width;
    int h = raw_info.height;
    
    /* for fast EV - raw conversion */
    static int raw2ev[1<<20];   /* EV x EV_RESOLUTION */
    static int ev2raw_0[24*EV_RESOLUTION];
    static int previous_black = -1;
    
    /* handle sub-black values (negative EV) */
    int* ev2raw = ev2raw_0 + 10*EV_RESOLUTION;
    
    LOCK(ev2raw_mutex)
    {
        if(black != previous_black)
        {
            build_ev2raw_lut(raw2ev, ev2raw_0, black, white);
            previous_black = black;
        }
#ifndef STDOUT_SILENT
        printf("Final blending...\n");
#endif
        for (int y = 0; y < h; y ++)
        {
            for (int x = 0; x < w; x ++)
            {
                /* high-iso image (for measuring signal level) */
                int b = bright[x + y*w];
                
                /* half-res image (interpolated and chroma filtered, best for low-contrast shadows) */
                int hr = halfres_smooth[x + y*w];
                
                /* full-res image (non-interpolated, except where one ISO is blown out) */
                int fr = fullres[x + y*w];
                
                /* full res with some smoothing applied to hide aliasing artifacts */
                int frs = fullres_smooth[x + y*w];
                
                /* go from linear to EV space */
                int hrev = raw2ev[hr];
                int frev = raw2ev[fr];
                int frsev = raw2ev[frs];
                
                int output = 0;
                
                /* blending factor */
                double f = fullres_curve[b & 0xFFFFF];
                
                double c = 0;
                
                if (alias_map)
                {
                    int co = alias_map[x + y*w];
                    c = COERCE(co / (double) ALIAS_MAP_MAX, 0, 1);
                }
                
                double ovf = COERCE(overexposed[x + y*w] / 200.0, 0, 1);
                c = MAX(c, ovf);
                
                double noisy_or_overexposed = MAX(ovf, 1-f);
                
                /* use data from both ISOs in high-detail areas, even if it's noisier (less aliasing) */
                f = MAX(f, c);
                
                /* use smoothing in noisy near-overexposed areas to hide color artifacts */
                double fev = noisy_or_overexposed * frsev + (1-noisy_or_overexposed) * frev;
                
                /* limit the use of fullres in dark areas (fixes some black spots, but may increase aliasing) */
                int sig = (dark[x + y*w] + bright[x + y*w]) / 2;
                f = MAX(0, MIN(f, (double)(sig - black) / (4*dark_noise)));
                
                /* blend "half-res" and "full-res" images smoothly to avoid banding*/
                output = hrev * (1-f) + fev * f;
                
                /* show full-res map (for debugging) */
                //~ output = f * 14*EV_RESOLUTION;
                
                /* show alias map (for debugging) */
                //~ output = c * 14*EV_RESOLUTION;
                
                //~ output = hotpixel[x+y*w] ? 14*EV_RESOLUTION : 0;
                //~ output = raw2ev[dark[x+y*w]];
                /* safeguard */
                output = COERCE(output, -10*EV_RESOLUTION, 14*EV_RESOLUTION-1);
                
                
                /* back to linear space and commit */
                raw_set_pixel32(x, y, ev2raw[output]);
            }
        }
    }
    UNLOCK(ev2raw_mutex)
}

static inline void convert_20_to_16bit(struct raw_info raw_info, uint16_t * image_data, uint32_t * raw_buffer_32)
{
    int w = raw_info.width;
    int h = raw_info.height;
    /* go back from 20-bit to 16-bit output */
    //raw_info.buffer = raw_buffer_16;
    raw_info.black_level /= 16;
    raw_info.white_level /= 16;
    
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            raw_set_pixel_20to16_rand(x, y, raw_buffer_32[x + y*w]);
}

int diso_get_full20bit(struct raw_info raw_info, uint16_t * image_data, int interp_method, int use_alias_map, int use_fullres, int chroma_smooth_method)
{
    int w = raw_info.width;
    int h = raw_info.height;
    
    if (w <= 0 || h <= 0) return 0;

    /* RGGB or GBRG? */
    int rggb = identify_rggb_or_gbrg(raw_info, image_data);
    
    if (!rggb) /* this code assumes RGGB, so we need to skip one line */
    {
        image_data += raw_info.pitch;
        raw_info.active_area.y1++;
        raw_info.active_area.y2--;
        raw_info.height--;
        h--;
    }
    
    int is_bright[4];
    
    if (!identify_bright_and_dark_fields(raw_info, image_data, rggb, is_bright)) return 0;
    
    int ret = 0;
    
    /* will use 20-bit processing and 16-bit output, instead of 14 */
    raw_info.black_level *= 64;
    raw_info.white_level *= 64;
    
    int black = raw_info.black_level;
    int white = raw_info.white_level;
    
    int white_bright = white;
    white_detect(raw_info, image_data, &white, &white_bright, is_bright);
    white *= 64;
    white_bright *= 64;
    raw_info.white_level = white;
    
    double noise_std[4];
    double dark_noise, bright_noise, dark_noise_ev, bright_noise_ev;
    double noise_avg = compute_noise(raw_info, image_data, noise_std, &dark_noise, &bright_noise, &dark_noise_ev, &bright_noise_ev);
    
    /* promote from 14 to 20 bits (original raw buffer holds 14-bit values stored as uint16_t) */
    uint32_t * raw_buffer_32 = convert_to_20bit(raw_info, image_data);
    
    /* we have now switched to 20-bit, update noise numbers */
    dark_noise *= 64;
    bright_noise *= 64;
    dark_noise_ev += 6;
    bright_noise_ev += 6;
    
    /* dark and bright exposures, interpolated */
    uint32_t* dark   = malloc(w * h * sizeof(uint32_t));
    uint32_t* bright = malloc(w * h * sizeof(uint32_t));
    memset(dark, 0, w * h * sizeof(uint32_t));
    memset(bright, 0, w * h * sizeof(uint32_t));
    
    /* fullres image (minimizes aliasing) */
    uint32_t* fullres = malloc(w * h * sizeof(uint32_t));
    memset(fullres, 0, w * h * sizeof(uint32_t));
    uint32_t* fullres_smooth = fullres;
    
    /* halfres image (minimizes noise and banding) */
    uint32_t* halfres = malloc(w * h * sizeof(uint32_t));
    memset(halfres, 0, w * h * sizeof(uint32_t));
    uint32_t* halfres_smooth = halfres;
    
    if (chroma_smooth_method)
    {
        if (use_fullres)
        {
            fullres_smooth = malloc(w * h * sizeof(uint32_t));
        }
        halfres_smooth = malloc(w * h * sizeof(uint32_t));
    }
    
    /* overexposure map */
    uint16_t * overexposed = malloc(w * h * sizeof(uint16_t));
    memset(overexposed, 0, w * h * sizeof(uint16_t));
    
    uint16_t* alias_map = NULL;
    if(use_alias_map)
    {
        alias_map = malloc(w * h * sizeof(uint16_t));
        memset(alias_map, 0, w * h * sizeof(uint16_t));
    }
    
    //~ printf("Exposure matching...\n");
    /* estimate ISO difference between bright and dark exposures */
    double corr_ev = 0;
    int white_darkened = white_bright;
    if(match_exposures(raw_info, raw_buffer_32, &corr_ev, &white_darkened, is_bright))
    {
        /* estimate dynamic range */
        double lowiso_dr = log2(white - black) - dark_noise_ev;
        double highiso_dr = log2(white_bright - black) - bright_noise_ev;
#ifndef STDOUT_SILENT
        printf("Dynamic range   : %.02f (+) %.02f => %.02f EV (in theory)\n", lowiso_dr, highiso_dr, highiso_dr + corr_ev);
#endif
        /* correction factor for the bright exposure, which was just darkened */
        double corr = pow(2, corr_ev);
        
        /* update bright noise measurements, so they can be compared after scaling */
        bright_noise /= corr;
        bright_noise_ev -= corr_ev;
        
        if(interp_method == 0)
        {
            amaze_interpolate(raw_info, raw_buffer_32, dark, bright, black, white, white_darkened, is_bright);
        }
        else
        {
            mean23_interpolate(raw_info, raw_buffer_32, dark, bright, black, white, white_darkened, is_bright);
        }
        
        border_interpolate(raw_info, raw_buffer_32, dark, bright, is_bright);
        
        if (use_fullres) fullres_reconstruction(raw_info, fullres, dark, bright, white_darkened, is_bright);
        
        if(mix_images(raw_info, fullres, fullres_smooth, halfres, halfres_smooth, alias_map, dark, bright, overexposed, dark_noise, white_darkened, corr_ev, lowiso_dr, black, white, chroma_smooth_method))
        {
            /* let's check the ideal noise levels (on the halfres image, which in black areas is identical to the bright one) */
            for (int y = 3; y < h-2; y ++)
                for (int x = 2; x < w-2; x ++)
                    raw_set_pixel32(x, y, bright[x + y*w]);
            compute_black_noise(raw_info, image_data, 8, raw_info.active_area.x1 - 8, raw_info.active_area.y1 + 20, raw_info.active_area.y2 - 20, 1, 1, &noise_avg, &noise_std[0]);
            double ideal_noise_std = noise_std[0];
            
            final_blend(raw_info, raw_buffer_32, fullres, fullres_smooth, halfres_smooth, dark, bright, overexposed, alias_map, black, white, dark_noise);
            
            /* let's see how much dynamic range we actually got */
            compute_black_noise(raw_info, image_data, 8, raw_info.active_area.x1 - 8, raw_info.active_area.y1 + 20, raw_info.active_area.y2 - 20, 1, 1, &noise_avg, &noise_std[0]);
#ifndef STDOUT_SILENT
            printf("Noise level     : %.02f (20-bit), ideally %.02f\n", noise_std[0], ideal_noise_std);
            printf("Dynamic range   : %.02f EV (cooked)\n", log2(white - black) - log2(noise_std[0]));
#endif
            convert_20_to_16bit(raw_info, image_data, raw_buffer_32);
            ret = 1;
        }
    }
    
    if (!rggb) /* back to GBRG */
    {
        raw_info.active_area.y1--;
        raw_info.active_area.y2++;
        raw_info.height++;
        h++;
    }
    
    free(dark);
    free(bright);
    free(fullres);
    free(halfres);
    free(alias_map);
    free(overexposed);
    free(raw_buffer_32);
    if (fullres_smooth && fullres_smooth != fullres) free(fullres_smooth);
    if (halfres_smooth && halfres_smooth != halfres) free(halfres_smooth);
    return ret;
}

