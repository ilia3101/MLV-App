/*
 * Copyright 2024 libmcraw
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

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include "mcraw.h"
#include "cJSON.h"

struct mr_ctx_s
{
    FILE                  *fd;

    mr_buffer_offset_t    *offsets;
    size_t                nb_offsets;

    mr_buffer_offset_t    *audio_offsets;
    size_t                nb_audio_offsets;

    int64_t               index_data_offset;

    int                   last_error;
    char                  error_message[256];

    int                   verbose;

    // metadata

    mr_rational_t         frame_rate;

    double                color_matrix1[9];
    double                color_matrix2[9];
    double                forward_matrix1[9];
    double                forward_matrix2[9];
    double                calibration_matrix1[9];
    double                calibration_matrix2[9];
    char                  color_illuminant1[MR_MAX_STRING + 1];
    char                  color_illuminant2[MR_MAX_STRING + 1];

    int16_t               white_level;
    int16_t               black_level;

    uint32_t              cfa_pattern;   // color filter array

    double                aperture;
    double                focal_length;

    int32_t               audio_sample_rate;
    int32_t               audio_channels;

    char                  manufacturer[MR_MAX_STRING + 1];
    char                  model[MR_MAX_STRING + 1];

    mr_frame_data_t       frame_data;   // frame data of first frame

};

static int read_first_frame(mr_ctx_t *ctx);

//-----------------------------------------------------------------------------
void mr_packet_free(mr_packet_t *pkt)
{
    if (pkt->data) {
        free(pkt->data);
    }
}

//-----------------------------------------------------------------------------
mr_ctx_t* mr_decoder_new(int verbose)
{
    mr_ctx_t *ctx = malloc(sizeof(mr_ctx_t));

    if (ctx != NULL)
    {
        memset(ctx, 0, sizeof(mr_ctx_t));

        ctx->verbose = verbose;
    }

    return ctx;
}

//-----------------------------------------------------------------------------
void mr_decoder_free(mr_ctx_t *ctx)
{
    if (ctx != NULL)
    {
        // fd must be closed with mr_decoder_close,
        // a calling app might use it after decoder is deleted.

        if (ctx->offsets != NULL) {
            free(ctx->offsets);
        }

        if (ctx->audio_offsets != NULL) {
            free(ctx->audio_offsets);
        }

        free(ctx);
    }
}

//-----------------------------------------------------------------------------
void mr_decoder_close(mr_ctx_t *ctx)
{
    if (ctx && ctx->fd)
    {
        fclose(ctx->fd);
        ctx->fd = NULL;
    }
}

//-----------------------------------------------------------------------------
static int mr_fseek(FILE *stream, uint64_t offset, int whence)
{
#if defined(__WIN32)
    return fseeko64(stream, offset, whence);
#else
    return fseek(stream, offset, whence);
#endif
}

//-----------------------------------------------------------------------------
static int mr_set_error(mr_ctx_t *ctx, int error, const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    vprintf(msg, args);
    va_end(args);

    ctx->last_error = error;

    return error;
}

static const cJSON *mr_json_first(const cJSON *item)
{
    if (item == NULL) {
        return NULL;
    }

    if (cJSON_IsArray(item)) {
        return cJSON_GetArrayItem(item, 0);
    }

    return item;
}

//-----------------------------------------------------------------------------
static const cJSON *mr_json_get_first(const cJSON *root, const char *name)
{
    return mr_json_first(cJSON_GetObjectItemCaseSensitive(root, name));
}

//-----------------------------------------------------------------------------
static int mr_json_get_int64(const cJSON *root, const char *name, int64_t *dst)
{
    const cJSON *item = mr_json_get_first(root, name);

    if (item == NULL) {
        return 0;
    }

    if (cJSON_IsNumber(item)) {
        *dst = (int64_t)item->valuedouble;
        return 1;
    }

    if (cJSON_IsString(item) && item->valuestring != NULL) {
        *dst = (int64_t)strtoll(item->valuestring, NULL, 10);
        return 1;
    }

    return 0;
}

//-----------------------------------------------------------------------------
static int mr_json_get_int32(const cJSON *root, const char *name, int32_t *dst)
{
    int64_t value;

    if (!mr_json_get_int64(root, name, &value)) {
        return 0;
    }

    *dst = (int32_t)value;
    return 1;
}

//-----------------------------------------------------------------------------
static int mr_json_get_int16(const cJSON *root, const char *name, int16_t *dst)
{
    int64_t value;

    if (!mr_json_get_int64(root, name, &value)) {
        return 0;
    }

    *dst = (int16_t)value;
    return 1;
}

//-----------------------------------------------------------------------------
static int mr_json_get_double(const cJSON *root, const char *name, double *dst)
{
    const cJSON *item = mr_json_get_first(root, name);

    if (item == NULL) {
        return 0;
    }

    if (cJSON_IsNumber(item)) {
        *dst = item->valuedouble;
        return 1;
    }

    if (cJSON_IsString(item) && item->valuestring != NULL) {
        *dst = strtod(item->valuestring, NULL);
        return 1;
    }

    return 0;
}

//-----------------------------------------------------------------------------
static int mr_json_get_string(const cJSON *root, const char *name, const char **dst)
{
    const cJSON *item = mr_json_get_first(root, name);

    if (item == NULL || !cJSON_IsString(item) || item->valuestring == NULL) {
        return 0;
    }

    *dst = item->valuestring;
    return 1;
}

//-----------------------------------------------------------------------------
static int mr_json_copy_string(const cJSON *root, const char *name, char *dst, size_t max_len)
{
    const char *src = NULL;
    size_t copy_len;

    if (!mr_json_get_string(root, name, &src)) {
        return 0;
    }

    copy_len = strlen(src);
    if (copy_len > max_len) {
        copy_len = max_len;
    }

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';

    return 1;
}

//-----------------------------------------------------------------------------
static int mr_json_copy_matrix(const cJSON *root, const char *name, double *dst, int size)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(root, name);
    int matrix_size = 0;
    int copy_count = 0;

    if (!cJSON_IsArray(array)) {
        return 0;
    }

    matrix_size = cJSON_GetArraySize(array);
    copy_count = (matrix_size < size) ? matrix_size : size;

    for (int i = 0; i < copy_count; i++)
    {
        const cJSON *entry = cJSON_GetArrayItem(array, i);

        if (cJSON_IsNumber(entry)) {
            dst[i] = entry->valuedouble;
        }
        else if (cJSON_IsString(entry) && entry->valuestring != NULL) {
            dst[i] = strtod(entry->valuestring, NULL);
        }
    }

    return copy_count;
}

// Color filter array (CFA) geometric pattern
// Red   = 0
// Green = 1
// Blue  = 2
//    rggb -> 0x02010100
//    gbrg -> 0x01000201
//-----------------------------------------------------------------------------
static void parse_sensor_arrangment(uint32_t *cfa_pattern, const char *sensor_arrangement)
{
    uint32_t pattern = 0;

    if (sensor_arrangement == NULL) {
        return;
    }

    for (int i = 0; i < 4 && sensor_arrangement[i] != '\0'; i++)
    {
        uint32_t n = sensor_arrangement[i] == 'g' ? 1 : sensor_arrangement[i] == 'b' ? 2 : 0;
        pattern |= (n << (i * 8));
    }

    *cfa_pattern = pattern;
}

//-----------------------------------------------------------------------------
static int mr_read_file_metadata(mr_ctx_t *ctx)
{
    // Read camera metadata
    mr_item_t metadataItem = {};
    char *metadataJson = NULL;
    cJSON *metadata = NULL;
    int res = 0;
    const char *sensor_arrangement = NULL;

    if (fread(&metadataItem, sizeof(mr_item_t), 1, ctx->fd) != 1 || metadataItem.type != METADATA) {
        return mr_set_error(ctx, -1, "Invalid camera metadata");
    }

    metadataJson = malloc(metadataItem.size);
    if (metadataJson == NULL) {
        return mr_set_error(ctx, kMrErrorRead, "Failed to allocate metadata buffer");
    }

    if (fread(metadataJson, metadataItem.size, 1, ctx->fd) != 1) {
        res = mr_set_error(ctx, kMrErrorRead, "File read error");
        goto done;
    }

    metadata = cJSON_ParseWithLength(metadataJson, metadataItem.size);
    if (metadata == NULL || !cJSON_IsObject(metadata)) {
        res = mr_set_error(ctx, kMrErrorMetadata, "Failed to parse metadata JSON");
        goto done;
    }

    mr_json_get_double(metadata, "apertures", &ctx->aperture);
    mr_json_get_double(metadata, "focalLengths", &ctx->focal_length);
    mr_json_get_int16(metadata, "whiteLevel", &ctx->white_level);
    mr_json_get_int16(metadata, "blackLevel", &ctx->black_level);
    mr_json_get_int32(metadata, "audioChannels", &ctx->audio_channels);
    mr_json_get_int32(metadata, "audioSampleRate", &ctx->audio_sample_rate);

    mr_json_copy_string(metadata, "build.manufacturer", ctx->manufacturer, MR_MAX_STRING);
    mr_json_copy_string(metadata, "deviceSpecificProfile.deviceModel", ctx->model, MR_MAX_STRING);
    mr_json_copy_string(metadata, "colorIlluminant1", ctx->color_illuminant1, MR_MAX_STRING);
    mr_json_copy_string(metadata, "colorIlluminant2", ctx->color_illuminant2, MR_MAX_STRING);

    int cm1_len = mr_json_copy_matrix(metadata, "colorMatrix1", ctx->color_matrix1, 9);
    int cm2_len = mr_json_copy_matrix(metadata, "colorMatrix2", ctx->color_matrix2, 9);
    int fm1_len = mr_json_copy_matrix(metadata, "forwardMatrix1", ctx->forward_matrix1, 9);
    int fm2_len = mr_json_copy_matrix(metadata, "forwardMatrix2", ctx->forward_matrix2, 9);

    mr_json_copy_matrix(metadata, "calibrationMatrix1", ctx->calibration_matrix1, 9);
    mr_json_copy_matrix(metadata, "calibrationMatrix2", ctx->calibration_matrix2, 9);

    // Normalize color matrices (cross-pollinate or identity)
    if (cm1_len < 9 && cm2_len >= 9) memcpy(ctx->color_matrix1, ctx->color_matrix2, 9 * sizeof(double));
    else if (cm1_len < 9) {
        memset(ctx->color_matrix1, 0, 9 * sizeof(double));
        ctx->color_matrix1[0] = ctx->color_matrix1[4] = ctx->color_matrix1[8] = 1.0;
    }

    if (cm2_len < 9 && cm1_len >= 9) memcpy(ctx->color_matrix2, ctx->color_matrix1, 9 * sizeof(double));
    else if (cm2_len < 9) {
        memset(ctx->color_matrix2, 0, 9 * sizeof(double));
        ctx->color_matrix2[0] = ctx->color_matrix2[4] = ctx->color_matrix2[8] = 1.0;
    }

    // Normalize forward matrices
    if (fm1_len < 9 && fm2_len >= 9) memcpy(ctx->forward_matrix1, ctx->forward_matrix2, 9 * sizeof(double));
    else if (fm1_len < 9) {
        memset(ctx->forward_matrix1, 0, 9 * sizeof(double));
        ctx->forward_matrix1[0] = ctx->forward_matrix1[4] = ctx->forward_matrix1[8] = 1.0;
    }

    if (fm2_len < 9 && fm1_len >= 9) memcpy(ctx->forward_matrix2, ctx->forward_matrix1, 9 * sizeof(double));
    else if (fm2_len < 9) {
        memset(ctx->forward_matrix2, 0, 9 * sizeof(double));
        ctx->forward_matrix2[0] = ctx->forward_matrix2[4] = ctx->forward_matrix2[8] = 1.0;
    }

    if (!mr_json_get_string(metadata, "sensorArrangement", &sensor_arrangement)) {
        mr_json_get_string(metadata, "sensorArrangment", &sensor_arrangement);
    }

    if (sensor_arrangement != NULL) {
        parse_sensor_arrangment(&ctx->cfa_pattern, sensor_arrangement);
    }

done:
    if (metadata != NULL) {
        cJSON_Delete(metadata);
    }

    free(metadataJson);
    return res;
}

//-----------------------------------------------------------------------------
int sort_index(const void * elem1, const void * elem2)
{
    mr_buffer_offset_t *a = (mr_buffer_offset_t*)elem1;
    mr_buffer_offset_t *b = (mr_buffer_offset_t*)elem2;
    return b->timestamp < a->timestamp;
}

//-----------------------------------------------------------------------------
void mr_dump(mr_ctx_t *ctx)
{
    const char type_str[8][24] = {
        "BUFFER_INDEX,       ",
        "BUFFER_INDEX_DATA,  ",
        "BUFFER,             ",
        "METADATA,           ",
        "AUDIO_INDEX,        ",
        "AUDIO_DATA,         ",
        "AUDIO_DATA_METADATA,",
        "undefined,          "
    };

    mr_fseek(ctx->fd, 0, SEEK_END);
    int64_t file_size = ftell(ctx->fd);
    mr_fseek(ctx->fd, 0, SEEK_SET);

    mr_hdr_t file_hdr = {};

    if (fread(&file_hdr, sizeof(mr_hdr_t), 1, ctx->fd) != 1)
    {
        printf("File is too short to be valid");
        return;
    }

    file_hdr.ident[6] = 0;

    printf("File size: %ld\n", file_size);
    printf("    Ident: %s\n", file_hdr.ident);
    printf("  Version: %d\n\n", file_hdr.version);

    int64_t pos = ftell(ctx->fd);

    mr_item_t item = {};

    while (pos < ctx->index_data_offset)
    {
        if (fread(&item, sizeof(mr_item_t), 1, ctx->fd) != 1)
        {
            printf("Failed to read item\n");
            return;
        }

        printf("type: %s size: %8d, offset: %ld\n", type_str[item.type & 7], item.size, pos);

        pos += (sizeof(mr_item_t) + item.size);

        mr_fseek(ctx->fd, item.size, SEEK_CUR);
    }

    printf("Index offset: %ld\n", file_size - (sizeof(mr_buffer_index_t) + sizeof(mr_item_t)));
}

//-----------------------------------------------------------------------------
static int mr_read_audio_offset(mr_ctx_t *ctx)
{
    if (ctx->nb_offsets == 0) {
        return 0;
    }

    int64_t curOffset = ctx->offsets[ctx->nb_offsets - 1].offset;

    int res = mr_fseek(ctx->fd, curOffset, SEEK_SET);

    if (res != 0) {
        return mr_set_error(ctx, kMrErrorSeek, "Failed to seek to audio index");
    }

    while (1)
    {
        mr_item_t item = {};

        fread(&item, sizeof(mr_item_t), 1, ctx->fd);

        // Skip things we don't need
        if (item.type == BUFFER ||
            item.type == METADATA ||
            item.type == AUDIO_DATA ||
            item.type == AUDIO_DATA_METADATA)
        {
            res = mr_fseek(ctx->fd, item.size, SEEK_CUR);

            if (res != 0) {
                break;
            }
        }
        else if (item.type == AUDIO_INDEX)
        {
            mr_audio_index_t index = {};

            fread(&index, sizeof(mr_audio_index_t), 1, ctx->fd);

            // Read all audio offsets
            ctx->nb_audio_offsets = index.numOffsets;
            ctx->audio_offsets    = calloc(index.numOffsets, sizeof(mr_buffer_offset_t));

            fread(ctx->audio_offsets, index.numOffsets * sizeof(mr_buffer_offset_t), 1, ctx->fd);

            break;
        }
        else {
            break;
        }
    }

    return 0;
}

//-----------------------------------------------------------------------------
static void sort_offsets(mr_buffer_offset_t *offsets, uint32_t nb_offsets)
{
    uint32_t n = nb_offsets;

    do
    {
        uint32_t new_n = 1;

        for (uint32_t i = 0; i < n-1; ++i)
        {
            if (offsets[i].timestamp > offsets[i+1].timestamp)
            {
                mr_buffer_offset_t tmp = offsets[i+1];
                offsets[i+1] = offsets[i];
                offsets[i] = tmp;
                new_n = i + 1;
            }
        }

        n = new_n;

    } while (n > 1);
}

//-----------------------------------------------------------------------------
static int mr_read_index(mr_ctx_t *ctx)
{
    uint64_t fread_err = 1;

    // Seek to index item (at the end of file)

    int res = mr_fseek(ctx->fd, -(sizeof(mr_buffer_index_t) + sizeof(mr_item_t)), SEEK_END);

    if (res < 0) {
        return mr_set_error(ctx, res, "Failed to seek to index");
    }

    mr_item_t bufferIndexItem = {};
    fread_err &= fread(&bufferIndexItem, sizeof(mr_item_t), 1, ctx->fd);

    if (bufferIndexItem.type != BUFFER_INDEX) {
        return mr_set_error(ctx, -1, "Invalid index");
    }

    mr_buffer_index_t index = {};
    fread_err &= fread(&index, sizeof(mr_buffer_index_t), 1, ctx->fd);

    // Check validity of index
    if (index.magicNumber != MCRAW_INDEX_MAGIC_NUMBER) {
        return mr_set_error(ctx, -1, "Corrupted file");
    }

    ctx->nb_offsets = index.numOffsets;
    ctx->offsets    = calloc(index.numOffsets, sizeof(mr_buffer_offset_t));

    ctx->index_data_offset = index.indexDataOffset;

    mr_fseek(ctx->fd, index.indexDataOffset, SEEK_SET);

    fread_err &= fread(ctx->offsets, index.numOffsets * sizeof(mr_buffer_offset_t), 1, ctx->fd);

    if (!fread_err) {
        return mr_set_error(ctx, kMrErrorRead, "File read error");
    }

    sort_offsets(ctx->offsets, index.numOffsets);

    if (ctx->verbose)
    {
        printf("Video frames: %d\n", index.numOffsets);

        int64_t prev = 0;

        for (int i = 0; i < index.numOffsets; i++)
        {
            int64_t timestamp = ctx->offsets[i].timestamp;
            int64_t delta = timestamp - prev;

            printf("block: %ld, timestamp: %ld, delta: %ld\n",
                   ctx->offsets[i].offset, ctx->offsets[i].timestamp, delta / 1000000);

            prev = ctx->offsets[i].timestamp;
        }
    }

    int64_t total = ctx->offsets[index.numOffsets - 1].timestamp - ctx->offsets[0].timestamp;

    double frame_duration = ((double)(total) / 1000000.) / (index.numOffsets - 1);
    double fps = 1000. / frame_duration;

    ctx->frame_rate.den = 1;

    if (fps < 1.5) {
        ctx->frame_rate.num = 1;
    }
    else {
        ctx->frame_rate.num = (int)lround(fps);
    }

    if (ctx->verbose)
    {
        printf("avg frame rate: %d/%d (%f)\n",
               ctx->frame_rate.num, ctx->frame_rate.den, 1000. / frame_duration);
    }

    return 0;
}

//-----------------------------------------------------------------------------
int mr_decoder_open(mr_ctx_t *ctx, const char *filename)
{
    ctx->fd = fopen(filename, "rb");

    if (!ctx->fd)
    {
        sprintf(ctx->error_message, "Failed to open file: %s", filename);
        return -1;
    }

    return 0;
}

//-----------------------------------------------------------------------------
int mr_decoder_parse(mr_ctx_t *ctx)
{
    mr_hdr_t file_hdr;

    mr_fseek(ctx->fd, 0, SEEK_SET);

    while (1)
    {
        // Read file header
        if (fread(&file_hdr, sizeof(mr_hdr_t), 1, ctx->fd) != 1)
        {
            mr_set_error(ctx, -1, "File is too short to be a valid mcraw");
            break;
        }

        if ((file_hdr.version != MCRAW_CONTAINER_VERSION))
        {
            mr_set_error(ctx, -1, "Invalid container version %d", file_hdr.version);
            break;
        }

        if (memcmp(file_hdr.ident, MCRAW_CONTAINER_ID, 7) != 0)
        {
            mr_set_error(ctx, -1, "File header is missing, invalid file:  %s", ctx->fd);
            break;
        }

        if (mr_read_file_metadata(ctx) != 0) {
            break;
        }

        if (mr_read_index(ctx) != 0) {
            break;
        }

        if (mr_read_audio_offset(ctx) != 0) {
            break;
        }

        if (read_first_frame(ctx) != 0) {
            break;
        }

        break;
    }

    if (ctx->last_error != 0)
    {
        return ctx->last_error;
    }

    return 0;
}

//-----------------------------------------------------------------------------
static uint8_t* packet_resize(mr_packet_t *pkt, uint32_t size)
{
    if (size > pkt->allocated)
    {
        pkt->allocated = size + (size >> 2);

        if (pkt->data != NULL) {
            pkt->data = realloc(pkt->data, pkt->allocated);
        }
        else {
            pkt->data = malloc(pkt->allocated);
        }
    }

    pkt->size = size;

    return pkt->data;
}

//-----------------------------------------------------------------------------
int mr_read_audio_packet(FILE *fd, int64_t offset, mr_packet_t *pkt)
{
    int res = mr_fseek(fd, offset, SEEK_SET);

    if (res != 0) {
        return kMrErrorSeek;
    }

    mr_item_t item = {};
    if (fread(&item, sizeof(mr_item_t), 1, fd) != 1) {
        return kMrErrorRead;
    }

    packet_resize(pkt, item.size);

    if (fread(pkt->data, pkt->size, 1, fd) != 1) {
        return kMrErrorRead;
    }

    pkt->timestamp = -1;

    // Read metadata if available

    mr_audio_metadata_t metadata = {};
    if (fread(&metadata, sizeof(mr_audio_metadata_t), 1, fd) == 1)
    {
        if (metadata.item.type == AUDIO_DATA_METADATA) {
            pkt->timestamp = metadata.timestampNs;
        }
    }

    return 0;
}

//-----------------------------------------------------------------------------
int mr_read_video_frame(FILE *fd, int64_t offset, mr_packet_t *pkt)
{
    int res = mr_fseek(fd, offset, SEEK_SET);

    if (res != 0) {
        return kMrErrorSeek;
    }

    mr_item_t item = {};
    if (fread(&item, sizeof(mr_item_t), 1, fd) != 1) {
        return kMrErrorRead;
    }

    packet_resize(pkt, item.size);

    if (fread(pkt->data, pkt->size, 1, fd) != 1) {
        return kMrErrorRead;
    }

    pkt->timestamp = -1;

    return 0;
}

//-----------------------------------------------------------------------------
int mr_read_frame_metadata(FILE *fd, mr_frame_data_t *frame_data)
{
    mr_item_t item = {};
    if (fread(&item, sizeof(mr_item_t), 1, fd) != 1) {
        return kMrErrorRead;
    }

    int res = 0;
    mr_packet_t pkt = {};

    packet_resize(&pkt, item.size);

    if (fread(pkt.data, pkt.size, 1, fd) == 1)
    {
        cJSON *metadata = cJSON_ParseWithLength((const char *)pkt.data, pkt.size);
        const char *pixel_format = NULL;

        if (metadata == NULL || !cJSON_IsObject(metadata)) {
            res = kMrErrorMetadata;
        }
        else {
            mr_json_get_int32(metadata, "iso", &frame_data->iso);
            mr_json_get_int32(metadata, "width", &frame_data->width);
            mr_json_get_int32(metadata, "height", &frame_data->height);
            mr_json_get_int32(metadata, "originalWidth", &frame_data->original_width);
            mr_json_get_int32(metadata, "originalHeight", &frame_data->original_height);
            mr_json_get_int64(metadata, "exposureTime", &frame_data->exposure_time);
            mr_json_get_int64(metadata, "recvdTimestampMs", &frame_data->timestamp);
            mr_json_get_int16(metadata, "orientation", &frame_data->orientation);
            mr_json_get_int16(metadata, "compressionType", &frame_data->compression_type);

            if (mr_json_copy_matrix(metadata, "asShotNeutral", frame_data->as_shot_neutral, 3) < 3) {
                frame_data->as_shot_neutral[0] = 1.0;
                frame_data->as_shot_neutral[1] = 1.0;
                frame_data->as_shot_neutral[2] = 1.0;
            }

            if (mr_json_get_string(metadata, "pixelFormat", &pixel_format) &&
                strncmp(pixel_format, "raw16", 5) == 0)
            {
                frame_data->stored_pixel_format = 16;
            }
        }

        if (metadata != NULL) {
            cJSON_Delete(metadata);
        }
    }
    else {
        res = kMrErrorRead;
    }

    mr_packet_free(&pkt);

    return res;
}

//-----------------------------------------------------------------------------
static int read_first_frame(mr_ctx_t *ctx)
{
    int res = mr_fseek(ctx->fd, ctx->offsets[0].offset, SEEK_SET);

    if (res != 0) {
        return kMrErrorSeek;
    }

    mr_item_t item = {};
    if (fread(&item, sizeof(mr_item_t), 1, ctx->fd) != 1) {
        return kMrErrorRead;
    }

    // Skip frame data
    res = mr_fseek(ctx->fd, item.size, SEEK_CUR);

    if (res != 0) {
        return mr_set_error(ctx, -1, "Invalid offset");
    }

    return mr_read_frame_metadata(ctx->fd, &ctx->frame_data);
}

//-----------------------------------------------------------------------------
uint32_t mr_get_frame_count(mr_ctx_t *ctx)
{
    return ctx->nb_offsets;
}

//-----------------------------------------------------------------------------
mr_buffer_offset_t* mr_get_index(mr_ctx_t *ctx)
{
    return ctx->offsets;
}

//-----------------------------------------------------------------------------
uint32_t mr_get_audio_packet_count(mr_ctx_t *ctx)
{
    return ctx->nb_audio_offsets;
}

//-----------------------------------------------------------------------------
mr_buffer_offset_t* mr_get_audio_index(mr_ctx_t *ctx)
{
    return ctx->audio_offsets;
}

//-----------------------------------------------------------------------------
int32_t mr_get_width(mr_ctx_t *ctx)
{
    return ctx->frame_data.width;
}

//-----------------------------------------------------------------------------
int32_t mr_get_height(mr_ctx_t *ctx)
{
    return ctx->frame_data.height;
}

//-----------------------------------------------------------------------------
int32_t mr_get_stored_format(mr_ctx_t *ctx)
{
    return ctx->frame_data.stored_pixel_format;
}

//-----------------------------------------------------------------------------
int32_t mr_get_bits_per_pixel(mr_ctx_t *ctx)
{
    return ctx->white_level <= 0x3FF ? 10 : ctx->white_level <= 0xFFF ? 12 : 14;
}

//-----------------------------------------------------------------------------
int mr_get_frame_rate(mr_ctx_t *ctx, int *num, int *den)
{
    *num = ctx->frame_rate.num;
    *den = ctx->frame_rate.den;
    return 0;
}

//-----------------------------------------------------------------------------
int16_t mr_get_black_level(mr_ctx_t *ctx)
{
    return ctx->black_level;
}

//-----------------------------------------------------------------------------
int16_t mr_get_white_level(mr_ctx_t *ctx)
{
    return ctx->white_level;
}

//-----------------------------------------------------------------------------
double* mr_get_color_matrix1(mr_ctx_t *ctx)
{
    return ctx->color_matrix1;
}

//-----------------------------------------------------------------------------
double* mr_get_color_matrix2(mr_ctx_t *ctx)
{
    return ctx->color_matrix2;
}

//-----------------------------------------------------------------------------
double* mr_get_forward_matrix1(mr_ctx_t *ctx)
{
    return ctx->forward_matrix1;
}

//-----------------------------------------------------------------------------
double* mr_get_forward_matrix2(mr_ctx_t *ctx)
{
    return ctx->forward_matrix2;
}

//-----------------------------------------------------------------------------
double mr_get_focal_length(mr_ctx_t *ctx)
{
    return ctx->focal_length;
}

//-----------------------------------------------------------------------------
double mr_get_aperture(mr_ctx_t *ctx)
{
    return ctx->aperture;
}

//-----------------------------------------------------------------------------
int mr_get_iso(mr_ctx_t *ctx)
{
    return ctx->frame_data.iso;
}

//-----------------------------------------------------------------------------
int mr_get_exposure_time(mr_ctx_t *ctx)
{
    return ctx->frame_data.exposure_time;
}

//-----------------------------------------------------------------------------
double* mr_get_as_shot_neutral(mr_ctx_t *ctx)
{
    return ctx->frame_data.as_shot_neutral;
}

//-----------------------------------------------------------------------------
int64_t mr_get_timestamp(mr_ctx_t *ctx)
{
    return ctx->frame_data.timestamp;
}

//-----------------------------------------------------------------------------
const char* mr_get_manufacturer(mr_ctx_t *ctx)
{
    return ctx->manufacturer;
}

//-----------------------------------------------------------------------------
const char* mr_get_model(mr_ctx_t *ctx)
{
    return ctx->model;
}

//-----------------------------------------------------------------------------
int32_t mr_get_audio_sample_rate(mr_ctx_t *ctx)
{
    return ctx->audio_sample_rate;
}

//-----------------------------------------------------------------------------
int32_t mr_get_audio_channels(mr_ctx_t *ctx)
{
    return ctx->audio_channels;
}

//-----------------------------------------------------------------------------
uint32_t mr_get_cfa_pattern(mr_ctx_t *ctx)
{
    return ctx->cfa_pattern;
}

//-----------------------------------------------------------------------------
int mr_get_compression_type(mr_ctx_t *ctx)
{
    return ctx->frame_data.compression_type;
}

//-----------------------------------------------------------------------------
FILE* mr_get_file_handle(mr_ctx_t *ctx)
{
    return ctx->fd;
}
