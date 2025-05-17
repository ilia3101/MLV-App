/*
 * Copyright 2024 libmcraw
 * Copyright 2023 MotionCam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Changes:
// - Remove all c++ specific fragments
// - Change naming syntax
// - Add c-style API

#ifndef MCRAW_H
#define MCRAW_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum mr_comp_type
{
    MOTIONCAM_COMPRESSION_TYPE_LEGACY = 6,
    MOTIONCAM_COMPRESSION_TYPE = 7
};

enum mr_item_type
{
    BUFFER_INDEX,
    BUFFER_INDEX_DATA,
    BUFFER,
    METADATA,
    AUDIO_INDEX,
    AUDIO_DATA,
    AUDIO_DATA_METADATA
};

typedef struct
{
    uint8_t ident[7];
    uint8_t version;
} mr_hdr_t;

typedef struct
{
    enum mr_item_type type;
    uint32_t size;
} mr_item_t;

typedef struct
{
    int32_t magicNumber;
    int32_t numOffsets;
    int64_t indexDataOffset;
} mr_buffer_index_t;

typedef struct
{
    int64_t offset;
    int64_t timestamp;
} mr_buffer_offset_t;

typedef struct
{
    int64_t numOffsets;
    int64_t startTimestampMs;
} mr_audio_index_t;

typedef struct
{
    mr_item_t item;
    int64_t timestampNs;
} mr_audio_metadata_t;

typedef struct
{
    uint8_t *data;
    size_t  size;
    size_t  allocated;

    int64_t timestamp;   // nano seconds
} mr_packet_t;

typedef struct
{
    int num;
    int den;
} mr_rational_t;

#define MCRAW_CONTAINER_VERSION   3
#define MCRAW_CONTAINER_ID        "MOTION "
#define MCRAW_INDEX_MAGIC_NUMBER  0x8A905612

#define MOTIONCAM_COMPRESSION_TYPE 7

#define MR_MAX_STRING 32

enum
{
    kMrOk            = 0,
    kMrErrorSeek     = -2,
    kMrErrorRead     = -3,
    kMrErrorMetadata = -4
};

typedef struct
{
    int        width;
    int        height;
    int        original_width;
    int        original_height;
    int        stored_pixel_format;

    int        iso;
    int64_t    exposure_time;

    int16_t    orientation;
    int16_t    compression_type;

    double     as_shot_neutral[3];
    int64_t    timestamp;           // milliseconds

} mr_frame_data_t;

typedef struct mr_ctx_s mr_ctx_t;


mr_ctx_t* mr_decoder_new(int verbose);
void mr_decoder_free(mr_ctx_t *ctx);
int mr_decoder_open(mr_ctx_t *ctx, const char *filename);
int mr_decoder_parse(mr_ctx_t *ctx);
void mr_decoder_close(mr_ctx_t *ctx);

// The following functions might be used without a decoder context.

int mr_read_video_frame(FILE *fd, int64_t offset, mr_packet_t *pkt);
int mr_read_audio_packet(FILE *fd, int64_t offset, mr_packet_t *pkt);
int mr_read_frame_metadata(FILE *fd, mr_frame_data_t *frame_data);
size_t mr_decode_video_frame(uint8_t *dstData, uint8_t *srcData, uint32_t srcSize, int width, int height, int comp_type);

void mr_dump(mr_ctx_t *ctx);
FILE* mr_get_file_handle(mr_ctx_t *ctx);

uint32_t mr_get_frame_count(mr_ctx_t *ctx);
uint32_t mr_get_audio_packet_count(mr_ctx_t *ctx);
mr_buffer_offset_t* mr_get_index(mr_ctx_t *ctx);
mr_buffer_offset_t* mr_get_audio_index(mr_ctx_t *ctx);

int32_t mr_get_width(mr_ctx_t *ctx);
int32_t mr_get_height(mr_ctx_t *ctx);
int32_t mr_get_bits_per_pixel(mr_ctx_t *ctx);
int32_t mr_get_stored_format(mr_ctx_t *ctx);
int16_t mr_get_black_level(mr_ctx_t *ctx);
int16_t mr_get_white_level(mr_ctx_t *ctx);
double* mr_get_color_matrix1(mr_ctx_t *ctx);
double* mr_get_color_matrix2(mr_ctx_t *ctx);
double* mr_get_forward_matrix1(mr_ctx_t *ctx);
double* mr_get_forward_matrix2(mr_ctx_t *ctx);
int mr_get_frame_rate(mr_ctx_t *ctx, int *num, int *den);
double mr_get_focal_length(mr_ctx_t *ctx);
double mr_get_aperture(mr_ctx_t *ctx);
int mr_get_iso(mr_ctx_t *ctx);
int mr_get_exposure_time(mr_ctx_t *ctx);
double* mr_get_as_shot_neutral(mr_ctx_t *ctx);
int64_t mr_get_timestamp(mr_ctx_t *ctx);
const char* mr_get_manufacturer(mr_ctx_t *ctx);
const char* mr_get_model(mr_ctx_t *ctx);
uint32_t mr_get_cfa_pattern(mr_ctx_t *ctx);
int mr_get_compression_type(mr_ctx_t *ctx);

int32_t mr_get_audio_sample_rate(mr_ctx_t *ctx);
int32_t mr_get_audio_channels(mr_ctx_t *ctx);

void mr_packet_free(mr_packet_t *pkt);

#ifdef __cplusplus
}
#endif

#endif // MCRAW_H
