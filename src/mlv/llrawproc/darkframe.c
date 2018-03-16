/*
 * Copyright (C) 2018 Bouncyball
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

#include "darkframe.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define COERCE(x,lo,hi) MAX(MIN((x),(hi)),(lo))

static uint64_t file_set_pos(FILE *stream, uint64_t offset, int whence)
{
#if defined(__WIN32)
    return fseeko64(stream, offset, whence);
#else
    return fseek(stream, offset, whence);
#endif
}

/* load dark frame from external averaged MLV file */
static int df_load_ext(mlvObject_t * video, char * error_message)
{
    /* If file name is not set return error */
    if(!video->llrawproc->dark_frame_filename) return 1;
    /* Parse dark frame MLV */
    mlvObject_t df_mlv = { 0 };
    char err_msg[256] = { 0 };
    int ret = openMlvClip(&df_mlv, video->llrawproc->dark_frame_filename, 2, err_msg);
    if(ret != 0)
    {
#ifndef STDOUT_SILENT
        printf("DF: %s\n", err_msg);
#endif
        if(error_message != NULL) strcpy(error_message, err_msg);
        return ret;
    }

    /* if lossless MLV return error */
    if(df_mlv.MLVI.videoClass & MLV_VIDEO_CLASS_FLAG_LJ92)
    {
        sprintf(err_msg, "Can not use lossless MLV as a dark frame:\n\n%s", video->llrawproc->dark_frame_filename);
#ifndef STDOUT_SILENT
        printf("DF: %s\n", err_msg);
#endif
        if(error_message != NULL) strcpy(error_message, err_msg);
        return 1;
    }
    /* if resolution mismatch detected */
    if( (df_mlv.RAWI.xRes != video->RAWI.xRes) || (df_mlv.RAWI.yRes != video->RAWI.yRes) )
    {
        sprintf(err_msg, "Video clip and dark frame resolutions have not matched:\n\n%s", video->llrawproc->dark_frame_filename);
#ifndef STDOUT_SILENT
        printf("DF: %s\n", err_msg);
#endif
        if(error_message != NULL) strcpy(error_message, err_msg);
        return 1;
    }
    /* if MLV has more than one frame just show the warning */
    if( df_mlv.MLVI.videoFrameCount > 1 )
    {
        sprintf(err_msg, "For proper use as a dark frame all frames of this MLV have to be averaged first:\n\n%s", video->llrawproc->dark_frame_filename);
#ifndef STDOUT_SILENT
        printf("DF: %s\n", err_msg);
#endif
        if(error_message != NULL) strcpy(error_message, err_msg);
    }

    /* Allocate dark frame data buffer */
    uint8_t * df_packed_buf = calloc(df_mlv.video_index[0].frame_size, 1);
    if(!df_packed_buf)
    {
        sprintf(err_msg, "Packed buffer allocation error");
#ifndef STDOUT_SILENT
        printf("DF: %s\n", err_msg);
#endif
        if(error_message != NULL) strcpy(error_message, err_msg);
        return 1;
    }
    /* Load dark frame data to the allocated buffer */
    file_set_pos(df_mlv.file[0], df_mlv.video_index[0].frame_offset, SEEK_SET);
    if ( fread(df_packed_buf, df_mlv.video_index[0].frame_size, 1, df_mlv.file[0]) != 1 )
    {
        sprintf(err_msg, "Could not read dark frame from the file:\n\n%s", video->llrawproc->dark_frame_filename);
#ifndef STDOUT_SILENT
        printf("DF: %s\n", err_msg);
#endif
        if(error_message != NULL) strcpy(error_message, err_msg);
        free(df_packed_buf);
        return 1;
    }
    /* Free all data related to the dark frame if needed */
    df_free(video);
    /* Fill DARK block header */
    memcpy(&video->llrawproc->dark_frame_hdr.blockType, "DARK", 4);
    video->llrawproc->dark_frame_hdr.blockSize = sizeof(mlv_dark_hdr_t) + df_mlv.video_index[0].frame_size;
    video->llrawproc->dark_frame_hdr.timestamp = 0xFFFFFFFFFFFFFFFF;
    video->llrawproc->dark_frame_hdr.samplesAveraged = MAX(df_mlv.VIDF.frameNumber + 1, df_mlv.MLVI.videoFrameCount);
    video->llrawproc->dark_frame_hdr.cameraModel = df_mlv.IDNT.cameraModel;
    video->llrawproc->dark_frame_hdr.xRes = df_mlv.RAWI.xRes;
    video->llrawproc->dark_frame_hdr.yRes = df_mlv.RAWI.yRes;
    video->llrawproc->dark_frame_hdr.rawWidth = df_mlv.RAWI.raw_info.width;
    video->llrawproc->dark_frame_hdr.rawHeight = df_mlv.RAWI.raw_info.height;
    video->llrawproc->dark_frame_hdr.bits_per_pixel = df_mlv.RAWI.raw_info.bits_per_pixel;
    video->llrawproc->dark_frame_hdr.black_level = df_mlv.RAWI.raw_info.black_level;
    video->llrawproc->dark_frame_hdr.white_level = df_mlv.RAWI.raw_info.white_level;
    video->llrawproc->dark_frame_hdr.sourceFpsNom = df_mlv.MLVI.sourceFpsNom;
    video->llrawproc->dark_frame_hdr.sourceFpsDenom = df_mlv.MLVI.sourceFpsDenom;
    video->llrawproc->dark_frame_hdr.isoMode = df_mlv.EXPO.isoMode;
    video->llrawproc->dark_frame_hdr.isoValue = df_mlv.EXPO.isoValue;
    video->llrawproc->dark_frame_hdr.isoAnalog = df_mlv.EXPO.isoAnalog;
    video->llrawproc->dark_frame_hdr.digitalGain = df_mlv.EXPO.digitalGain;
    video->llrawproc->dark_frame_hdr.shutterValue = df_mlv.EXPO.shutterValue;
    video->llrawproc->dark_frame_hdr.binning_x = df_mlv.RAWC.binning_x;
    video->llrawproc->dark_frame_hdr.skipping_x = df_mlv.RAWC.skipping_x;
    video->llrawproc->dark_frame_hdr.binning_y = df_mlv.RAWC.binning_y;
    video->llrawproc->dark_frame_hdr.skipping_y = df_mlv.RAWC.skipping_y;
    /* Allocate the dark frame 16bit buffer */
    video->llrawproc->dark_frame_size = df_mlv.RAWI.xRes * df_mlv.RAWI.yRes * 2;
    video->llrawproc->dark_frame_data = calloc(video->llrawproc->dark_frame_size + 4, 1);
    dng_unpack_image_bits(video->llrawproc->dark_frame_data, (uint16_t*)df_packed_buf, df_mlv.RAWI.xRes, df_mlv.RAWI.yRes, df_mlv.RAWI.raw_info.bits_per_pixel);
#ifndef STDOUT_SILENT
    printf("DF: initialized Ext mode\n");
#endif
    free(df_packed_buf);
    return 0;
}

/* load dark frame from current MLVs internal DARK block header */
static int df_load_int(mlvObject_t * video)
{
    /* if DARK block is not found return error */
    if(!video->DARK.blockType[0]) return 1;
    /* Allocate dark frame data buffer */
    size_t df_packed_size = video->DARK.blockSize - sizeof(mlv_dark_hdr_t);
    uint8_t * df_packed_buf = calloc(df_packed_size, 1);
    if(!df_packed_buf)
    {
#ifndef STDOUT_SILENT
        printf("DF: packed buffer allocation error\n");
#endif
        return 1;
    }
    /* Load dark frame data to the allocated buffer */
    file_set_pos(video->file[0], video->dark_frame_offset, SEEK_SET);
    if ( fread(df_packed_buf, df_packed_size, 1, video->file[0]) != 1 )
    {
#ifndef STDOUT_SILENT
        printf("DF: could not read frame: %s\n", video->llrawproc->dark_frame_filename);
#endif
        free(df_packed_buf);
        return 1;
    }
    /* Free all data related to the dark frame if needed */
    df_free(video);
    /* Copy DARK block header */
    memcpy(&video->llrawproc->dark_frame_hdr, &video->DARK, sizeof(mlv_dark_hdr_t));
    /* Allocate the dark frame 16bit buffer */
    video->llrawproc->dark_frame_size = video->DARK.xRes * video->DARK.yRes * 2;
    video->llrawproc->dark_frame_data = calloc(video->llrawproc->dark_frame_size + 4, 1);
    dng_unpack_image_bits(video->llrawproc->dark_frame_data, (uint16_t*)df_packed_buf, video->DARK.xRes, video->DARK.yRes, video->DARK.bits_per_pixel);
#ifndef STDOUT_SILENT
    printf("DF: initialized Int mode\n");
#endif
    free(df_packed_buf);
    return 0;
}

/* copy filename of external dark frame MLV to llrawproc structure */
void df_init_filename(mlvObject_t * video, char * df_filename)
{
    size_t df_filename_size = strlen(df_filename);
    video->llrawproc->dark_frame_filename = calloc(df_filename_size + 1, 1);
    memcpy(video->llrawproc->dark_frame_filename, df_filename, df_filename_size);
}

/* delete filename of external dark frame MLV from llrawproc structure */
void df_free_filename(mlvObject_t * video)
{
    if(video->llrawproc->dark_frame_filename) free(video->llrawproc->dark_frame_filename);
    video->llrawproc->dark_frame_filename = NULL;
}

/* subtract dark frame from the current frame */
void df_subtract(mlvObject_t * video, uint16_t * raw_image_buff, size_t raw_image_size)
{
    if( !video->llrawproc->dark_frame_data || (raw_image_size != video->llrawproc->dark_frame_size) )
    {
#ifndef STDOUT_SILENT
    printf("DF: subtracting is impossible, invalid dark frame'\n\n");
#endif
        return;
    }
#ifndef STDOUT_SILENT
    printf("Subtracting dark frame...'\n\n");
#endif
    uint16_t * dark_frame_data = video->llrawproc->dark_frame_data;
    uint32_t black_level = video->RAWI.raw_info.black_level;
    uint16_t white_level = (1 << video->RAWI.raw_info.bits_per_pixel) - 1;

    uint32_t pixel_count = raw_image_size / 2;
    for(uint32_t i = 0; i < pixel_count; i++)
    {
        int32_t orig_val = raw_image_buff[i];
        int32_t dark_val = dark_frame_data[i];

        raw_image_buff[i] = COERCE( orig_val - dark_val + black_level, 0, white_level );
    }
}

/* validate external dark frame file */
int df_validate(mlvObject_t * video, char * df_filename, char * error_message)
{
    df_init_filename(video, df_filename);
    int ret = df_load_ext(video, error_message);
    df_free_filename(video);
    df_free(video);

    return ret;
}

/* process DF modes: Off, Ext or Int, if Off just free all DF data */
int df_init(mlvObject_t * video)
{
    switch(video->llrawproc->dark_frame)
    {
        case DF_EXT:
            return df_load_ext(video, NULL);
        case DF_INT:
            return df_load_int(video);
        default:
            df_free(video);
            return 1; // DF mode = Off
    }
}

/* free all DF data */
void df_free(mlvObject_t * video)
{
    if(video->llrawproc->dark_frame_data)
    {
#ifndef STDOUT_SILENT
        printf("DF: all data freed\n");
#endif
        free(video->llrawproc->dark_frame_data);
        video->llrawproc->dark_frame_data = NULL;
        video->llrawproc->dark_frame_size = 0;
        memset(&video->llrawproc->dark_frame_hdr, 0, sizeof(mlv_dark_hdr_t));
    }
}
