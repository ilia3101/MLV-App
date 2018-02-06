/*
 * Copyright (C) 2014 David Milligan
 *
 * Updated and modified by bouncyball (2016-2017)
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

#include "dng.h"
#include "dng_tag_codes.h"
#include "dng_tag_types.h"
#include "dng_tag_values.h"
#include "../mlv/camid/camera_id.h"

#include "../mlv/liblj92/lj92.h"
#include "../mlv/llrawproc/llrawproc.h"

#define IFD0_COUNT 41
#define EXIF_IFD_COUNT 11
#define PACK(a) (((uint16_t)a[1] << 16) | ((uint16_t)a[0]))
#define PACK2(a,b) (((uint16_t)b << 16) | ((uint16_t)a))
#define STRING_ENTRY(a,b,c) (uint32_t)(strlen(a) + 1), add_string(a, b, c)
#define RATIONAL_ENTRY(a,b,c,d) (d/2), add_array(a, b, c, d)
#define RATIONAL_ENTRY2(a,b,c,d) 1, add_rational(a, b, c, d)
#define ARRAY_ENTRY(a,b,c,d) d, add_array(a, b, c, d)
#define HEADER_SIZE 1536
#define COUNT(x) ((int)(sizeof(x)/sizeof((x)[0])))

#define SOFTWARE_NAME "MLV App"
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define COERCE(x,lo,hi) MAX(MIN((x),(hi)),(lo))
#define ABS(a) ((a) > 0 ? (a) : -(a))
#define ROR32(v,a) ((v) >> (a) | (v) << (32-(a)))
#define ROL32(v,a) ((v) << (a) | (v) >> (32-(a)))
#define ROR16(v,a) ((v) >> (a) | (v) << (16-(a)))
#define ROL16(v,a) ((v) << (a) | (v) >> (16-(a)))
#define log2(x) log((float)(x))/log(2.)

#ifdef __WIN32
#define FMT_SIZE "%u"
#else
#define FMT_SIZE "%zu"
#endif

static uint64_t file_set_pos(FILE *stream, uint64_t offset, int whence)
{
#if defined(__WIN32)
    return fseeko64(stream, offset, whence);
#else
    return fseek(stream, offset, whence);
#endif
}

enum { IMG_SIZE_UNPACKED, IMG_SIZE_PACKED, IMG_SIZE_LOSLESS };

//MLV WB modes
enum
{
    WB_AUTO         = 0,
    WB_SUNNY        = 1,
    WB_SHADE        = 8,
    WB_CLOUDY       = 2,
    WB_TUNGSTEN     = 3,
    WB_FLUORESCENT  = 4,
    WB_FLASH        = 5,
    WB_CUSTOM       = 6,
    WB_KELVIN       = 9
};

/*****************************************************************************************************
 * Kelvin/Green to RGB Multipliers from UFRAW
 *****************************************************************************************************/

#define COLORS 3

/* Convert between Temperature and RGB.
 * Base on information from http://www.brucelindbloom.com/
 * The fit for D-illuminant between 4000K and 23000K are from CIE
 * The generalization to 2000K < T < 4000K and the blackbody fits
 * are my own and should be taken with a grain of salt.
 */
static const double XYZ_to_RGB[3][3] = {
    { 3.24071,    -0.969258,  0.0556352 },
    { -1.53726,    1.87599,    -0.203996 },
    { -0.498571,    0.0415557,  1.05707 }
};

static const double xyz_rgb[3][3] = {
    { 0.412453, 0.357580, 0.180423 },
    { 0.212671, 0.715160, 0.072169 },
    { 0.019334, 0.119193, 0.950227 }
};

static inline void temperature_to_RGB(double T, double RGB[3])
{
    int c;
    double xD, yD, X, Y, Z, max;
    // Fit for CIE Daylight illuminant
    if (T <= 4000)
    {
        xD = 0.27475e9 / (T * T * T) - 0.98598e6 / (T * T) + 1.17444e3 / T + 0.145986;
    }
    else if (T <= 7000)
    {
        xD = -4.6070e9 / (T * T * T) + 2.9678e6 / (T * T) + 0.09911e3 / T + 0.244063;
    }
    else
    {
        xD = -2.0064e9 / (T * T * T) + 1.9018e6 / (T * T) + 0.24748e3 / T + 0.237040;
    }
    yD = -3 * xD * xD + 2.87 * xD - 0.275;
    
    // Fit for Blackbody using CIE standard observer function at 2 degrees
    //xD = -1.8596e9/(T*T*T) + 1.37686e6/(T*T) + 0.360496e3/T + 0.232632;
    //yD = -2.6046*xD*xD + 2.6106*xD - 0.239156;
    
    // Fit for Blackbody using CIE standard observer function at 10 degrees
    //xD = -1.98883e9/(T*T*T) + 1.45155e6/(T*T) + 0.364774e3/T + 0.231136;
    //yD = -2.35563*xD*xD + 2.39688*xD - 0.196035;
    
    X = xD / yD;
    Y = 1;
    Z = (1 - xD - yD) / yD;
    max = 0;
    for (c = 0; c < 3; c++) {
        RGB[c] = X * XYZ_to_RGB[0][c] + Y * XYZ_to_RGB[1][c] + Z * XYZ_to_RGB[2][c];
        if (RGB[c] > max) max = RGB[c];
    }
    for (c = 0; c < 3; c++) RGB[c] = RGB[c] / max;
}

static inline void pseudoinverse (double (*in)[3], double (*out)[3], int size)
{
    double work[3][6], num;
    int i, j, k;
    
    for (i=0; i < 3; i++) {
        for (j=0; j < 6; j++)
            work[i][j] = j == i+3;
        for (j=0; j < 3; j++)
            for (k=0; k < size; k++)
                work[i][j] += in[k][i] * in[k][j];
    }
    for (i=0; i < 3; i++) {
        num = work[i][i];
        for (j=0; j < 6; j++)
            work[i][j] /= num;
        for (k=0; k < 3; k++) {
            if (k==i) continue;
            num = work[k][i];
            for (j=0; j < 6; j++)
                work[k][j] -= work[i][j] * num;
        }
    }
    for (i=0; i < size; i++)
        for (j=0; j < 3; j++)
            for (out[i][j]=k=0; k < 3; k++)
                out[i][j] += work[j][k+3] * in[i][k];
}

static inline void cam_xyz_coeff (double cam_xyz[4][3], float pre_mul[4], float rgb_cam[3][4])
{
    double cam_rgb[4][3], inverse[4][3], num;
    int i, j, k;
    
    for (i=0; i < COLORS; i++)                /* Multiply out XYZ colorspace */
        for (j=0; j < 3; j++)
            for (cam_rgb[i][j] = k=0; k < 3; k++)
                cam_rgb[i][j] += cam_xyz[i][k] * xyz_rgb[k][j];
    
    for (i=0; i < COLORS; i++) {                /* Normalize cam_rgb so that */
        for (num=j=0; j < 3; j++)                /* cam_rgb * (1,1,1) is (1,1,1,1) */
            num += cam_rgb[i][j];
        for (j=0; j < 3; j++)
            cam_rgb[i][j] /= num;
        pre_mul[i] = 1 / num;
    }
    pseudoinverse (cam_rgb, inverse, COLORS);
    for (i=0; i < 3; i++)
        for (j=0; j < COLORS; j++)
            rgb_cam[i][j] = inverse[j][i];
}


static void kelvin_green_to_multipliers(double temperature, double green, double chanMulArray[3], uint32_t cam_id)
{
    float pre_mul[4], rgb_cam[3][4];
    double cam_xyz[4][3];
    double rgbWB[3];
    double cam_rgb[3][3];
    double rgb_cam_transpose[4][3];
    int c, cc, i, j;

    int32_t * color_matrix = camidGetColorMatrix2(cam_id);
    for (i = 0; i < 9; i++)
    {
        cam_xyz[i/3][i%3] = (double)color_matrix[i*2] / (double)color_matrix[i*2 + 1];
    }
    
    for (i = 9; i < 12; i++)
    {
        cam_xyz[i/3][i%3] = 0;
    }
    
    cam_xyz_coeff (cam_xyz, pre_mul, rgb_cam);
    
    for (i = 0; i < 4; i++) for (j = 0; j < 3; j++)
    {
        rgb_cam_transpose[i][j] = rgb_cam[j][i];
    }
    
    pseudoinverse(rgb_cam_transpose, cam_rgb, 3);
    
    temperature_to_RGB(temperature, rgbWB);
    rgbWB[1] = rgbWB[1] / green;
    
    for (c = 0; c < 3; c++)
    {
        double chanMulInv = 0;
        for (cc = 0; cc < 3; cc++)
            chanMulInv += 1 / pre_mul[c] * cam_rgb[c][cc] * rgbWB[cc];
        chanMulArray[c] = 1 / chanMulInv;
    }
    
    /* normalize green multiplier */
    chanMulArray[0] /= chanMulArray[1];
    chanMulArray[2] /= chanMulArray[1];
    chanMulArray[1] = 1;
}

static void get_white_balance(mlv_wbal_hdr_t wbal_hdr, int32_t *wbal, uint32_t cam_id)
{
    if(wbal_hdr.wb_mode == WB_CUSTOM)
    {
        wbal[0] = wbal_hdr.wbgain_r; wbal[1] = wbal_hdr.wbgain_g;
        wbal[2] = wbal_hdr.wbgain_g; wbal[3] = wbal_hdr.wbgain_g;
        wbal[4] = wbal_hdr.wbgain_b; wbal[5] = wbal_hdr.wbgain_g;
    }
    else
    {
        double kelvin = 5500;
        double green = 1.0;
        
        //TODO: G/M shift, not sure how this relates to "green" parameter
        if(wbal_hdr.wb_mode == WB_AUTO || wbal_hdr.wb_mode == WB_KELVIN)
        {
            kelvin = wbal_hdr.kelvin;
        }
        else if(wbal_hdr.wb_mode == WB_SUNNY)
        {
            kelvin = 5500;
        }
        else if(wbal_hdr.wb_mode == WB_SHADE)
        {
            kelvin = 7000;
        }
        else if(wbal_hdr.wb_mode == WB_CLOUDY)
        {
            kelvin = 6000;
        }
        else if(wbal_hdr.wb_mode == WB_TUNGSTEN)
        {
            kelvin = 3200;
        }
        else if(wbal_hdr.wb_mode == WB_FLUORESCENT)
        {
            kelvin = 4000;
        }
        else if(wbal_hdr.wb_mode == WB_FLASH)
        {
            kelvin = 5500;
        }
        double chanMulArray[3];
        kelvin_green_to_multipliers(kelvin, green, chanMulArray, cam_id);
        wbal[0] = 1000000; wbal[1] = (int32_t)(chanMulArray[0] * 1000000);
        wbal[2] = 1000000; wbal[3] = (int32_t)(chanMulArray[1] * 1000000);
        wbal[4] = 1000000; wbal[5] = (int32_t)(chanMulArray[2] * 1000000);
    }
}

/*****************************************************************************************************/


static uint16_t tiff_header[] = { byteOrderII, magicTIFF, 8, 0};

struct directory_entry {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value;
};

/* CDNG tag codes */
enum
{
    tcTimeCodes             = 51043,
    tcFrameRate             = 51044,
    tcTStop                 = 51058,
    tcReelName              = 51081,
    tcCameraLabel           = 51105,
};

static uint32_t add_array(int32_t * array, uint8_t * buffer, uint32_t * data_offset, size_t length)
{
    uint32_t result = *data_offset;
    memcpy(buffer + result, array, length * sizeof(int32_t));
    *data_offset += length * sizeof(int32_t);
    return result;
}

static uint32_t add_string(char * str, uint8_t * buffer, uint32_t * data_offset)
{
    uint32_t result = 0;
    size_t length = strlen(str) + 1;
    if(length <= 4)
    {
        //we can fit in 4 bytes, so just pack the string into result
        memcpy(&result, str, length);
    }
    else
    {
        result = *data_offset;
        memcpy(buffer + result, str, length);
        *data_offset += length;
        //align to 2 bytes
        if(*data_offset % 2) *data_offset += 1;
    }
    return result;
}

static uint32_t add_rational(int32_t numerator, int32_t denominator, uint8_t * buffer, uint32_t * data_offset)
{
    uint32_t result = *data_offset;
    memcpy(buffer + *data_offset, &numerator, sizeof(int32_t));
    *data_offset += sizeof(int32_t);
    memcpy(buffer + *data_offset, &denominator, sizeof(int32_t));
    *data_offset += sizeof(int32_t);
    return result;
}

static inline uint8_t to_tc_byte(int value)
{
    return (((value / 10) << 4) | (value % 10));
}

static uint32_t add_timecode(double framerate, int frame, uint8_t * buffer, uint32_t * data_offset)
{
    uint32_t result = *data_offset;
    memset(buffer + *data_offset, 0, 8);
    int hours, minutes, seconds, frames;
    
    /*
    //use drop frame if the framerate is close to 30 / 1.001
    int drop_frame = round((1.0 - framerate / ceil(framerate)) * 1000) == 1 && ceil(framerate) == 30;
    
    if (drop_frame)
    {
        int d = frame / 17982;
        int m = frame % 17982;
        int f = frame + 18 * d + 2 * (MAX(0, m - 2) / 1798);
        
        hours = ((f / 30) / 60) / 60;
        minutes = ((f / 30) / 60) % 60;
        seconds = (f / 30) % 60;
        frames = f % 30;
    }
    */
    
    //round the framerate to the next largest integral framerate
    //tc will not match real world time if framerate is not an integer
    double time = framerate == 0 ? 0 : frame / (framerate > 1 ? round(framerate) : framerate);
    hours = (int)floor(time / 3600);
    minutes = ((int)floor(time / 60)) % 60;
    seconds = ((int)floor(time)) % 60;
    frames = framerate > 1 ? (frame % ((int)round(framerate))) : 0;
    
    buffer[*data_offset] = to_tc_byte(frames) & 0x3F;
    //if(drop_frame) buffer[*data_offset] = buffer[*data_offset] | (1 << 7); //set the drop frame bit
    buffer[*data_offset + 1] = to_tc_byte(seconds) & 0x7F;
    buffer[*data_offset + 2] = to_tc_byte(minutes) & 0x7F;
    buffer[*data_offset + 3] = to_tc_byte(hours) & 0x3F;
    
    *data_offset += 8;
    return result;
}

static void add_ifd(struct directory_entry * ifd, uint8_t * header, size_t * position, int count, uint32_t next_ifd_offset)
{
    *(uint16_t*)(header + *position) = count;
    *position += sizeof(uint16_t);
    memcpy(header + *position, ifd, count * sizeof(struct directory_entry));
    *position += count * sizeof(struct directory_entry);
    memcpy(header + *position, &next_ifd_offset, sizeof(uint32_t));
    *position += sizeof(uint32_t);
}

static char * format_datetime(char * datetime, mlvObject_t * mlv_data)
{
    uint32_t seconds = mlv_data->RTCI.tm_sec + (uint32_t)((mlv_data->VIDF.timestamp - mlv_data->RTCI.timestamp) / 1000000);
    uint32_t minutes = mlv_data->RTCI.tm_min + seconds / 60;
    uint32_t hours = mlv_data->RTCI.tm_hour + minutes / 60;
    uint32_t days = mlv_data->RTCI.tm_mday + hours / 24;
    //TODO: days could also overflow in the month, but this is no longer simple modulo arithmetic like with hr:min:sec
    sprintf(datetime, "%04d:%02d:%02d %02d:%02d:%02d",
            1900 + mlv_data->RTCI.tm_year,
            mlv_data->RTCI.tm_mon + 1,
            days,
            hours % 24,
            minutes % 60,
            seconds % 60);
    return datetime;
}

/* returns the size of uncompressed image data. does not include header */
static size_t dng_get_image_size(mlvObject_t * mlv_data, int size_mode, uint64_t frame_index)
{
    switch(size_mode)
    {
        case IMG_SIZE_PACKED:
            return (size_t)(mlv_data->RAWI.xRes * mlv_data->RAWI.yRes * mlv_data->RAWI.raw_info.bits_per_pixel / 8);
            break;
        case IMG_SIZE_LOSLESS:
            return mlv_data->video_index[frame_index].frame_size;
            break;
        case IMG_SIZE_UNPACKED:
        default:
            return mlv_data->RAWI.xRes * mlv_data->RAWI.yRes * 2;
            break;
    }
}

/* generates the CDNG header. The result is written into dng_data struct */
static void dng_fill_header(mlvObject_t * mlv_data, dngObject_t * dng_data, uint32_t frame_index)
{
    uint8_t * header = dng_data->header_buf;
    size_t position = 0;
    if(header)
    {
        memset(header, 0 , HEADER_SIZE);
        memcpy(header + position, tiff_header, sizeof(tiff_header));
        position += sizeof(tiff_header);
        
        uint32_t exif_ifd_offset = (uint32_t)(position + sizeof(uint16_t) + IFD0_COUNT * sizeof(struct directory_entry) + sizeof(uint32_t));
        uint32_t data_offset = exif_ifd_offset + sizeof(uint16_t) + EXIF_IFD_COUNT * sizeof(struct directory_entry) + sizeof(uint32_t);

        /* 'Make' Tag */
        char make[33] = { 0 };
        memcpy(make, mlv_data->IDNT.cameraName, 32);
        char * space = strchr(make, ' ');
        if(space) *space = 0x0;

        /* 'Camera Model Name' Tag */
        char model[33] = { 0 };
        memcpy(model, mlv_data->IDNT.cameraName, 32);

        /* 'Camera Serial Number' Tag */
        char serial[33] = { 0 };
        memcpy(serial, mlv_data->IDNT.cameraSerial, 32);
        
        /* 'Unique Camera Model' Tag */
        char * unique_model = NULL;
        const char * unique_name = camidGetCameraName(mlv_data->IDNT.cameraModel, UNIQ);
        if (unique_name)
        {
            unique_model = (char *)unique_name;
        }
        else
        {
            unique_model = model;
        }

        /* Focal resolution stuff */
        int32_t * focal_resolution_x = camidGetHFocalResolution(mlv_data->IDNT.cameraModel);
        int32_t * focal_resolution_y = camidGetVFocalResolution(mlv_data->IDNT.cameraModel);

        /* Picture aspect ratio */
        int manual_ar = 0;
        int32_t pic_ar[4] = {1,1,1,1};
        int32_t * par = NULL;
        if(dng_data->par[0])
        {
#ifndef STDOUT_SILENT
            //printf("cDNG: manual aspect ratio used\n");
#endif
            manual_ar = 1;
            par = dng_data->par;
        }
        else
        {
#ifndef STDOUT_SILENT
            printf("cDNG: auto detected aspect ratio used\n");
#endif
            par = pic_ar;
        }

        /* If RAWC block present calculate AR and FR from binning/skipping values */
        if(mlv_data->RAWC.blockType[0])
        {
            int sampling_x = mlv_data->RAWC.binning_x + mlv_data->RAWC.skipping_x;
            int sampling_y = mlv_data->RAWC.binning_y + mlv_data->RAWC.skipping_y;

            if(!manual_ar)
            {
                par[2] = sampling_y; par[3] = sampling_x;
            }

            focal_resolution_x[1] = focal_resolution_x[1] * sampling_x;
            focal_resolution_y[1] = focal_resolution_y[1] * sampling_y;
        }
        else // use old method to calculate aspect ratio and detect crop_rec
        {
            double rawW = mlv_data->RAWI.raw_info.active_area.x2 - mlv_data->RAWI.raw_info.active_area.x1;
            double rawH = mlv_data->RAWI.raw_info.active_area.y2 - mlv_data->RAWI.raw_info.active_area.y1;
            double aspect_ratio = rawW / rawH;
            //check the aspect ratio of the original raw buffer, if it's > 2 and we're not in crop mode, then this is probably squeezed footage
            if(aspect_ratio > 2.0 && rawH <= 720 && llrpDetectFocusDotFixMode(mlv_data) != 2)
            {
                if(!manual_ar)
                {
                    // 5x3 line skpping
                    par[2] = 5; par[3] = 3;
                }

                focal_resolution_x[1] = focal_resolution_x[1] * 3;
                focal_resolution_y[1] = focal_resolution_y[1] * 5;
            }
            //if the width is larger than 2000, we're probably not in crop mode
            else if(rawW < 2000)
            {
                focal_resolution_x[1] = focal_resolution_x[1] * 3;
                focal_resolution_y[1] = focal_resolution_y[1] * 3;
            }
        }

        //we get the active area of the original raw source, not the recorded data, so overwrite the active area if the recorded data does
        //not contain the OB areas
        if(mlv_data->RAWI.xRes < mlv_data->RAWI.raw_info.active_area.x2 ||
           mlv_data->RAWI.yRes < mlv_data->RAWI.raw_info.active_area.y2)
        {
            mlv_data->RAWI.raw_info.active_area.x1 = 0;
            mlv_data->RAWI.raw_info.active_area.y1 = 0;
            mlv_data->RAWI.raw_info.active_area.x2 = mlv_data->RAWI.xRes;
            mlv_data->RAWI.raw_info.active_area.y2 = mlv_data->RAWI.yRes;
        }
        
        /* FPS stuff*/
        int32_t frame_rate[2] = {mlv_data->MLVI.sourceFpsNom, mlv_data->MLVI.sourceFpsDenom};
        if(dng_data->fps_float > 0)
        {
            frame_rate[0] = (int32_t)(dng_data->fps_float * 1000);
            frame_rate[1] = 1000;
        }
        double frame_rate_f = frame_rate[1] == 0 ? 0 : (double)frame_rate[0] / (double)frame_rate[1];
        
        /* Date */
        char datetime[255];

        /* Baseline exposure stuff */
        int32_t basline_exposure[2] = {mlv_data->RAWI.raw_info.exposure_bias[0],mlv_data->RAWI.raw_info.exposure_bias[1]};
        if(basline_exposure[1] == 0)
        {
            basline_exposure[0] = 0;
            basline_exposure[1] = 1;
        }

        /* Time code stuff */
        //number of frames since midnight
        int tc_frame = (int)mlv_data->video_index[frame_index].frame_number;// + (uint64_t)((mlv_data->RTCI.tm_hour * 3600 + mlv_data->RTCI.tm_min * 60 + mlv_data->RTCI.tm_sec) * mlv_data->MLVI.sourceFpsNom) / (uint64_t)mlv_data->MLVI.sourceFpsDenom;
        
        /* White balance stuff */
        int32_t wbal[6];
        get_white_balance(mlv_data->WBAL, wbal, mlv_data->IDNT.cameraModel);

        /* tcReelName */
        #ifdef _WIN32
        char * reel_name = strrchr(mlv_data->path, '\\');
        #else
        char * reel_name = strrchr(mlv_data->path, '/');
        #endif
        (!reel_name) ? (reel_name = mlv_data->path) : ++reel_name;
        char * ext_dot = strrchr(reel_name, '.');
        if(ext_dot) *ext_dot = '\000';

        /* Fill up IFD structs */
        struct directory_entry IFD0[IFD0_COUNT] =
        {
            {tcNewSubFileType,              ttLong,     1,      sfMainImage},
            {tcImageWidth,                  ttLong,     1,      mlv_data->RAWI.xRes},
            {tcImageLength,                 ttLong,     1,      mlv_data->RAWI.yRes},
            {tcBitsPerSample,               ttShort,    1,      (llrpHQDualIso(mlv_data)) ? 16 : mlv_data->RAWI.raw_info.bits_per_pixel},
            {tcCompression,                 ttShort,    1,      (!(dng_data->raw_output_state % 2)) ? ccUncompressed : ccJPEG},
            {tcPhotometricInterpretation,   ttShort,    1,      piCFA},
            {tcFillOrder,                   ttShort,    1,      1},
            {tcMake,                        ttAscii,    STRING_ENTRY(make, header, &data_offset)},
            {tcModel,                       ttAscii,    STRING_ENTRY(model, header, &data_offset)},
            {tcStripOffsets,                ttLong,     1,      (uint32_t)HEADER_SIZE},
            {tcOrientation,                 ttShort,    1,      1},
            {tcSamplesPerPixel,             ttShort,    1,      1},
            {tcRowsPerStrip,                ttShort,    1,      mlv_data->RAWI.yRes},
            {tcStripByteCounts,             ttLong,     1,      dng_data->image_size},
            {tcPlanarConfiguration,         ttShort,    1,      pcInterleaved},
            {tcSoftware,                    ttAscii,    STRING_ENTRY(SOFTWARE_NAME, header, &data_offset)},
            {tcDateTime,                    ttAscii,    STRING_ENTRY(format_datetime(datetime, mlv_data), header, &data_offset)},
            {tcCFARepeatPatternDim,         ttShort,    2,      0x00020002}, //2x2
            {tcCFAPattern,                  ttByte,     4,      0x02010100}, //RGGB
            {tcExifIFD,                     ttLong,     1,      exif_ifd_offset},
            {tcDNGVersion,                  ttByte,     4,      0x00000401}, //1.4.0.0 in little endian
            {tcUniqueCameraModel,           ttAscii,    STRING_ENTRY(unique_model, header, &data_offset)},
            {tcBlackLevel,                  ttLong,     1,      (llrpHQDualIso(mlv_data)) ? mlv_data->RAWI.raw_info.black_level << (16 - mlv_data->RAWI.raw_info.bits_per_pixel) : mlv_data->RAWI.raw_info.black_level},
            {tcWhiteLevel,                  ttLong,     1,      (llrpHQDualIso(mlv_data)) ? mlv_data->RAWI.raw_info.white_level << (16 - mlv_data->RAWI.raw_info.bits_per_pixel) : mlv_data->RAWI.raw_info.white_level},
            {tcDefaultScale,                ttRational, RATIONAL_ENTRY(par, header, &data_offset, 4)},
            {tcDefaultCropOrigin,           ttShort,    2,      PACK(mlv_data->RAWI.raw_info.crop.origin)},
            {tcDefaultCropSize,             ttShort,    2,      PACK2((mlv_data->RAWI.raw_info.active_area.x2 - mlv_data->RAWI.raw_info.active_area.x1), (mlv_data->RAWI.raw_info.active_area.y2 - mlv_data->RAWI.raw_info.active_area.y1))},
            {tcColorMatrix1,                ttSRational,RATIONAL_ENTRY(camidGetColorMatrix1(mlv_data->IDNT.cameraModel), header, &data_offset, 18)},
            {tcColorMatrix2,                ttSRational,RATIONAL_ENTRY(camidGetColorMatrix2(mlv_data->IDNT.cameraModel), header, &data_offset, 18)},
            {tcAsShotNeutral,               ttRational, RATIONAL_ENTRY(wbal, header, &data_offset, 6)},
            {tcBaselineExposure,            ttSRational,RATIONAL_ENTRY(basline_exposure, header, &data_offset, 2)},
            {tcCameraSerialNumber,          ttAscii,    STRING_ENTRY(serial, header, &data_offset)},
            {tcCalibrationIlluminant1,      ttShort,    1,      lsStandardLightA},
            {tcCalibrationIlluminant2,      ttShort,    1,      lsD65},
            {tcActiveArea,                  ttLong,     ARRAY_ENTRY(mlv_data->RAWI.raw_info.dng_active_area, header, &data_offset, 4)},
            {tcForwardMatrix1,              ttSRational,RATIONAL_ENTRY(camidGetForwardMatrix1(mlv_data->IDNT.cameraModel), header, &data_offset, 18)},
            {tcForwardMatrix2,              ttSRational,RATIONAL_ENTRY(camidGetForwardMatrix2(mlv_data->IDNT.cameraModel), header, &data_offset, 18)},
            {tcTimeCodes,                   ttByte,     8,      add_timecode(frame_rate_f, tc_frame, header, &data_offset)},
            {tcFrameRate,                   ttSRational,RATIONAL_ENTRY(frame_rate, header, &data_offset, 2)},
            {tcReelName,                    ttAscii,    STRING_ENTRY(reel_name, header, &data_offset)},
            {tcBaselineExposureOffset,      ttSRational,RATIONAL_ENTRY2(0, 1, header, &data_offset)},
        };
        
        struct directory_entry EXIF_IFD[EXIF_IFD_COUNT] =
        {
            {tcExposureTime,                ttRational, RATIONAL_ENTRY2((int32_t)mlv_data->EXPO.shutterValue/1000, 1000, header, &data_offset)},
            {tcFNumber,                     ttRational, RATIONAL_ENTRY2(mlv_data->LENS.aperture, 100, header, &data_offset)},
            {tcISOSpeedRatings,             ttShort,    1,      mlv_data->EXPO.isoValue},
            {tcSensitivityType,             ttShort,    1,      stISOSpeed},
            {tcExifVersion,                 ttUndefined,4,      0x30333230},
            {tcSubjectDistance,             ttRational, RATIONAL_ENTRY2(mlv_data->LENS.focalDist, 1, header, &data_offset)},
            {tcFocalLength,                 ttRational, RATIONAL_ENTRY2(mlv_data->LENS.focalLength, 1, header, &data_offset)},
            {tcFocalPlaneXResolutionExif,   ttRational, RATIONAL_ENTRY(focal_resolution_x, header, &data_offset, 2)},
            {tcFocalPlaneYResolutionExif,   ttRational, RATIONAL_ENTRY(focal_resolution_x, header, &data_offset, 2)},
            {tcFocalPlaneResolutionUnitExif,ttShort,    1,      camidGetFocalUnit(mlv_data->IDNT.cameraModel)}, //inches
            {tcLensModelExif,               ttAscii,    STRING_ENTRY((char*)mlv_data->LENS.lensName, header, &data_offset)},
        };
        
        /* update the StripOffsets to the correct location
           the image data starts where our extra data ends */
        IFD0[9].value = data_offset;

        add_ifd(IFD0, header, &position, IFD0_COUNT, 0);
        add_ifd(EXIF_IFD, header, &position, EXIF_IFD_COUNT, 0);
        
        /* set real header size */
        dng_data->header_size = data_offset;
    }
}

/* unpack bits to 16 bit little endian
   output_buffer - the buffer where the result will be written
   input_buffer - a buffer containing the packed imaged data
   width - image width
   height - image height
   bpp - raw data bits per pixel
*/
void dng_unpack_image_bits(uint16_t * output_buffer, uint16_t * input_buffer, int width, int height, uint32_t bpp)
{
    uint32_t pixel_count = width * height;
    uint32_t mask = (1 << bpp) - 1;
    uint16_t *packed_bits = input_buffer;
    uint16_t *unpacked_bits = output_buffer;

    for (uint32_t pixel_index = 0; pixel_index < pixel_count; pixel_index++)
    {
        uint32_t bits_offset = pixel_index * bpp;
        uint32_t bits_address = bits_offset / 16;
        uint32_t bits_shift = bits_offset % 16;

        /* fetch two 16 bit words into a 32 bit register and correct it plus shift it as needed.
        after the 32 bit fetch, the two 16 bit words will be swapped, so use a ROR to align them correctly.
        ROR by 16 to swap 16 bit words plus the bits needed to put the needed pixel bits to right position */
        uint32_t rotate_value = 16 + ((32 - bpp) - bits_shift);
        uint32_t uncorrected_data = *((uint32_t *)&packed_bits[bits_address]);
        uint32_t data = ROR32(uncorrected_data, rotate_value);

        unpacked_bits[pixel_index] = (uint16_t)(data & mask);
    }
}

/* pack bits to 16 bit little endian and convert to big endian (raw payload DNG spec)
   output_buffer - the buffer where the result will be written
   input_buffer - a buffer containing the unpacked imaged data
   width - image width
   height - image height
   bpp - raw data bits per pixel 
*/
void dng_pack_image_bits(uint16_t * output_buffer, uint16_t * input_buffer, int width, int height, uint32_t bpp, int big_endian)
{
    uint32_t pixel_count = width * height;
    uint32_t bits_free = 16 - bpp;
    uint16_t *unpacked_bits = input_buffer;
    uint16_t *packed_bits = output_buffer;

    packed_bits[0] = unpacked_bits[0] << bits_free;
    for (uint32_t pixel_index = 1; pixel_index < pixel_count; pixel_index++)
    {
        uint32_t bits_offset = (pixel_index * bits_free) % 16;
        uint32_t bits_to_rol = bits_free + bits_offset + (bits_offset > 0) * 16;

        /* increment pointer by two bytes but fetch 32 bit words from input and outbut buffers.
        after the 32 bit fetch, the two 16 bit words will be swapped, so use a ROL by 16 to swap
        16 bit words plus shift to the left to put the needed pixel bits to right position.
        mask/zero high 16 bits of 32 bit word of packed buffer and do logical OR to ROLed unpacked one.
        make current packed 16 bit word big endian to satisfy DNG spec */
        uint32_t data = ROL32((uint32_t)unpacked_bits[pixel_index], bits_to_rol);
        *(uint32_t *)packed_bits = (*(uint32_t *)packed_bits & 0x0000FFFF) | data;

        if(bits_offset > 0 && bits_offset <= bpp)
        {
            if(big_endian) *(uint16_t *)packed_bits = ROL16(*(uint16_t *)packed_bits, 8);
            packed_bits++;
        }
    }
}

/* decompress LJ92 image to output_buffer */
int dng_decompress_image(uint16_t * output_buffer, uint16_t * input_buffer, size_t input_buffer_size, int width, int height, uint32_t bpp)
{
    int components = 1;
    lj92 decoder_object;

    int ret = lj92_open(&decoder_object, (uint8_t*)input_buffer, input_buffer_size, &width, &height, (int*)&bpp, &components);
    if(ret != LJ92_ERROR_NONE)
    {
#ifndef STDOUT_SILENT
        printf("LJ92 decoder: Failed with error code (%d)\n", ret);
#endif
        memset(output_buffer, 0, width * height * sizeof(uint16_t));
        return ret;
    }

    ret = lj92_decode(decoder_object, output_buffer, width * height * components, 0, NULL, 0);
    if(ret != LJ92_ERROR_NONE)
    {
#ifndef STDOUT_SILENT
        printf("LJ92 decoder: Failed with error code (%d)\n", ret);
#endif
        memset(output_buffer, 0, width * height * sizeof(uint16_t));
    }

    lj92_close(decoder_object);
    return ret;
}

/* compress input_buffer to LJ92 image */
int dng_compress_image(uint16_t * output_buffer, uint16_t * input_buffer, size_t * output_buffer_size, int width, int height, uint32_t bpp)
{
    uint8_t * compressed = NULL;
    int new_width = width * 2;
    int new_height = height / 2;

    int ret = lj92_encode(input_buffer, new_width, new_height, (int)bpp, new_width * new_height, 0, NULL, 0, &compressed, (int*)output_buffer_size);
    if(ret == LJ92_ERROR_NONE)
    {
        memcpy(output_buffer, compressed, *output_buffer_size);
#ifndef STDOUT_SILENT
        size_t input_buffer_size = width * height * 2;
        printf("LJ92 encoder: "FMT_SIZE" -> "FMT_SIZE" (%2.2f%% ratio)\n", *output_buffer_size, input_buffer_size, ((float)*output_buffer_size * 100.0f) / (float)input_buffer_size);
#endif
    }
    else
    {
        *output_buffer_size = width * height * sizeof(uint16_t);
        memset(output_buffer, 0, *output_buffer_size);
#ifndef STDOUT_SILENT
        printf("LJ92 encoder: failed with error code (%d)\n", ret);
#endif
    }

    if(compressed) free(compressed);
    return ret;
}

/* changes endianness of the 16 bit buffer values
   DNG spec: 10/12/14bit raw should be big endian, 8/16/32bit raw can be little endian
   input_buffer - pointer to the buffer
   buf_size - the size of the buffer in bytes
*/
static void dng_reverse_byte_order(uint16_t * input_buffer, size_t buf_size)
{
    uint32_t index_max = buf_size / 2;

    for (uint32_t index = 0; index < index_max; index++)
    {
        input_buffer[index] = ROL16(input_buffer[index], 8);
    }
}

/* build whole DNG frame (header + image), process image if needed and put to the dng struct ready to save */
static int dng_get_frame(mlvObject_t * mlv_data, dngObject_t * dng_data, uint32_t frame_index)
{
    int ret = 0;
    /* Move to start of frame in file and read the RAW data */
    file_set_pos(mlv_data->file[mlv_data->video_index[frame_index].chunk_num], mlv_data->video_index[frame_index].frame_offset, SEEK_SET);

    if (dng_data->raw_input_state == COMPRESSED_RAW) /* If losless, decompress or pass trough */
    {
        dng_data->image_size = dng_get_image_size(mlv_data, IMG_SIZE_LOSLESS, frame_index);
        if(fread(dng_data->image_buf, dng_data->image_size, 1, mlv_data->file[mlv_data->video_index[frame_index].chunk_num]) != 1)
        {
#ifndef STDOUT_SILENT
            printf("Can not read raw frame from %s\n", mlv_data->path);
#endif
        }

        if(dng_data->raw_output_state == COMPRESSED_ORIG)
        {
            // do nothing, compressed raw data is ready to save unchanged
        }
        else
        {
            ret = dng_decompress_image(dng_data->image_buf_unpacked,
                                       dng_data->image_buf,
                                       dng_data->image_size,
                                       mlv_data->RAWI.xRes,
                                       mlv_data->RAWI.yRes,
                                       mlv_data->RAWI.raw_info.bits_per_pixel);

            /* apply low level raw processing to the unpacked_frame */
            applyLLRawProcObject(mlv_data, dng_data->image_buf_unpacked, dng_data->image_size_unpacked);

            if(dng_data->raw_output_state == COMPRESSED_RAW)
            {
                ret = dng_compress_image(dng_data->image_buf,
                                         dng_data->image_buf_unpacked,
                                         &dng_data->image_size,
                                         mlv_data->RAWI.xRes,
                                         mlv_data->RAWI.yRes,
                                         (llrpHQDualIso(mlv_data)) ? 16 : mlv_data->RAWI.raw_info.bits_per_pixel);
            }
            else
            {
                if(!llrpHQDualIso(mlv_data))
                {
                    dng_data->image_size = dng_get_image_size(mlv_data, IMG_SIZE_PACKED, frame_index);
                    dng_pack_image_bits(dng_data->image_buf,
                                        dng_data->image_buf_unpacked,
                                        mlv_data->RAWI.xRes,
                                        mlv_data->RAWI.yRes,
                                        mlv_data->RAWI.raw_info.bits_per_pixel,
                                        1);
                }
                else
                {
                    dng_data->image_size = dng_get_image_size(mlv_data, IMG_SIZE_UNPACKED, frame_index);
                    memcpy(dng_data->image_buf, dng_data->image_buf_unpacked, dng_data->image_size);
                }
            }
        }
    }
    else /* If uncompressed, unpack to 16bit or pass trough */
    {
        dng_data->image_size = dng_get_image_size(mlv_data, IMG_SIZE_PACKED, frame_index);
        if(fread(dng_data->image_buf, dng_data->image_size, 1, mlv_data->file[mlv_data->video_index[frame_index].chunk_num]) != 1)
        {
#ifndef STDOUT_SILENT
            printf("Can not read raw frame from %s\n", mlv_data->path);
#endif
        }

        if(dng_data->raw_output_state == UNCOMPRESSED_ORIG)
        {
            dng_reverse_byte_order(dng_data->image_buf, dng_data->image_size);
        }
        else
        {
            dng_unpack_image_bits(dng_data->image_buf_unpacked,
                                  dng_data->image_buf,
                                  mlv_data->RAWI.xRes,
                                  mlv_data->RAWI.yRes,
                                  mlv_data->RAWI.raw_info.bits_per_pixel);

            /* apply low level raw processing to the unpacked_frame */
            applyLLRawProcObject(mlv_data, dng_data->image_buf_unpacked, dng_data->image_size_unpacked);

            if(dng_data->raw_output_state == COMPRESSED_RAW)
            {
                ret = dng_compress_image(dng_data->image_buf,
                                         dng_data->image_buf_unpacked,
                                         &dng_data->image_size,
                                         mlv_data->RAWI.xRes,
                                         mlv_data->RAWI.yRes,
                                         (llrpHQDualIso(mlv_data)) ? 16 : mlv_data->RAWI.raw_info.bits_per_pixel);
            }
            else
            {
                if(!llrpHQDualIso(mlv_data))
                {
                    dng_data->image_size = dng_get_image_size(mlv_data, IMG_SIZE_PACKED, frame_index);
                    dng_pack_image_bits(dng_data->image_buf,
                                        dng_data->image_buf_unpacked,
                                        mlv_data->RAWI.xRes,
                                        mlv_data->RAWI.yRes,
                                        mlv_data->RAWI.raw_info.bits_per_pixel,
                                        1);
                }
                else
                {
                    dng_data->image_size = dng_get_image_size(mlv_data, IMG_SIZE_UNPACKED, frame_index);
                    memcpy(dng_data->image_buf, dng_data->image_buf_unpacked, dng_data->image_size);
                }

            }
        }
    }

    dng_fill_header(mlv_data, dng_data, frame_index);
    return ret;
}

/* init DNG data struct */
dngObject_t * initDngObject(mlvObject_t * mlv_data, int raw_state, double fps, int32_t par[4])
{
    dngObject_t * dng_data = calloc(1, sizeof(dngObject_t));

    dng_data->fps_float = fps;
    memcpy(dng_data->par, par, sizeof(int32_t) * 4);

    dng_data->raw_input_state = (mlv_data->MLVI.videoClass & MLV_VIDEO_CLASS_FLAG_LJ92) ? COMPRESSED_RAW : UNCOMPRESSED_RAW;
    dng_data->raw_output_state = (dng_data->raw_input_state && (raw_state == 2)) ? COMPRESSED_ORIG : raw_state;

    dng_data->header_size = HEADER_SIZE;
    dng_data->header_buf = malloc(dng_data->header_size);

    dng_data->image_size = dng_get_image_size(mlv_data, IMG_SIZE_UNPACKED, 0);
    dng_data->image_buf = malloc(dng_data->image_size);

    dng_data->image_size_unpacked = dng_get_image_size(mlv_data, IMG_SIZE_UNPACKED, 0);
    dng_data->image_buf_unpacked = malloc(dng_data->image_size_unpacked);

    return dng_data;
}

/* save DNG file */
int saveDngFrame(mlvObject_t * mlv_data, dngObject_t * dng_data, uint32_t frame_index, char * dng_filename)
{
    FILE* dngf = fopen(dng_filename, "wb");
    if (!dngf)
    {
        return 1;
    }

    /* get filled dng_data struct */
    if(dng_get_frame(mlv_data, dng_data, frame_index) != 0)
    {
        fclose(dngf);
        return 1;
    }

    /* write DNG header */
    if (fwrite(dng_data->header_buf, dng_data->header_size, 1, dngf) != 1)
    {
        fclose(dngf);
        return 1;
    }
    
    /* write DNG image data */
    if (fwrite(dng_data->image_buf, dng_data->image_size, 1, dngf) != 1)
    {
        fclose(dngf);
        return 1;
    }

    fclose(dngf);
#ifndef STDOUT_SILENT
    if (!frame_index)
    {
        switch (dng_data->raw_output_state)
        {
            case UNCOMPRESSED_RAW:
                printf("\nWriting uncompressed frames...\n");
                break;
            case COMPRESSED_RAW:
                printf("\nWriting losless frames...\n");
                break;
            case UNCOMPRESSED_ORIG:
                printf("\nPassing through original uncompressed raw...\n");
                break;
            case COMPRESSED_ORIG:
                printf("\nPassing through original lossless raw...\n");
                break;
        }
    }
    printf("Current frame '%s' (frames saved: %lu)\n", dng_filename, frame_index + 1);
#endif
    return 0;
}

/* free all buffers used for DNG creation */
void freeDngObject(dngObject_t * dng_data)
{
    if(dng_data->header_buf) free(dng_data->header_buf);
    if(dng_data->image_buf) free(dng_data->image_buf);
    if(dng_data->image_buf_unpacked) free(dng_data->image_buf_unpacked);
    free(dng_data);
}
