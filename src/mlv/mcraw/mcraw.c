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
#include "jsmn.h"

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

//-----------------------------------------------------------------------------
#define STRING_EQ(json_str, tok, tok_len, name) \
    (!(memcmp(json_str + tok->start, name, tok_len) == 0))

//-----------------------------------------------------------------------------
void cpy_string(char *dst, int max_len, const char *js, jsmntok_t *tok)
{
    int tok_len = tok->end - tok->start;
    strncpy(dst, js + tok->start, tok_len < max_len ? tok_len : max_len);
}

// Color filter array (CFA) geometric pattern
// Red   = 0
// Green = 1
// Blue  = 2
//    rggb -> 0x02010100
//    gbrg -> 0x01000201
//-----------------------------------------------------------------------------
void parse_sensor_arrangment(uint32_t *cfa_pattern, const char *js, jsmntok_t *tok)
{
    const char *p =  js + tok->start;

    for (int i = 0; i < 4; i++, p++)
    {
        uint32_t n = *p == 'g' ? 1 : *p == 'b' ? 2 : 0;
        *cfa_pattern |= (n << (i * 8));
    }
}

// Copy int and double values
//
// tok might be a JSMN_ARRAY, and we just want the first entry.
// In that case skip the starting '['

//-----------------------------------------------------------------------------
void cpy_int64(int64_t *dst, const char *js, jsmntok_t *tok)
{
    *dst = (int64_t)strtol(js + (tok->type != JSMN_ARRAY ? tok->start : tok->start + 1), NULL, 10);
}

//-----------------------------------------------------------------------------
void cpy_int32(int32_t *dst, const char *js, jsmntok_t *tok)
{
    *dst = (int32_t)strtol(js + (tok->type != JSMN_ARRAY ? tok->start : tok->start + 1), NULL, 10);
}

//-----------------------------------------------------------------------------
void cpy_int16(int16_t *dst, const char *js, jsmntok_t *tok)
{
    *dst = (int16_t)strtol(js + (tok->type != JSMN_ARRAY ? tok->start : tok->start + 1), NULL, 10);
}

//-----------------------------------------------------------------------------
void cpy_double(double *dst, const char *js, jsmntok_t *tok)
{
    *dst = strtod(js + (tok->type == JSMN_PRIMITIVE ? tok->start : tok->start + 1), NULL);
}

//-----------------------------------------------------------------------------
void cpy_matrix(double *dst, int size, const char *js, jsmntok_t *tokens, int *idx)
{
    jsmntok_t *t = &tokens[*idx + 1];

    if (t->type == JSMN_ARRAY) {
        size = t->size < size ? t->size : size;
    }
    else {
        return;
    }

    (*idx)++;

    for (int i = 0; i < size; i++)
    {
        (*idx)++;
        t = &tokens[*idx];

        char* pEnd;
        dst[i] = strtod(js + t->start, &pEnd);
    }
}

//-----------------------------------------------------------------------------
static int mr_read_file_metadata(mr_ctx_t *ctx)
{
    // Read camera metadata
    mr_item_t metadataItem = {};

    if (fread(&metadataItem, sizeof(mr_item_t), 1, ctx->fd) != 1 || metadataItem.type != METADATA) {
        return mr_set_error(ctx, -1, "Invalid camera metadata");
    }

    char *metadataJson = malloc(metadataItem.size);

    if (fread(metadataJson, metadataItem.size, 1, ctx->fd) != 1) {
        return mr_set_error(ctx, kMrErrorRead, "File read error");
    }

    jsmn_parser parser;
    jsmntok_t tokens[256];      // We expect no more than 256 tokens
    int nb_tokens;

    jsmn_init(&parser);

    int res = jsmn_parse(&parser, metadataJson, metadataItem.size, tokens, sizeof(tokens) / sizeof(tokens[0]));
    if (res < -1) {
        return mr_set_error(ctx, kMrErrorMetadata, "Failed to parse JSON: %d", res);
    }
    else if (res == -1) {
        nb_tokens = 255;
    }
    else {
        nb_tokens = res - 1;
    }

    const char *js = metadataJson;

    int i = 0;
    while (i < nb_tokens)
    {
        jsmntok_t *t = &tokens[i];

        if (t->type == JSMN_STRING)
        {
            int len = t->end - t->start;

            switch (len)
            {
                case 9:
                    if (STRING_EQ(js, t, len, "apertures") == 0) {
                        cpy_double(&ctx->aperture, js, &tokens[++i]);
                    }
                    break;

                case 10:
                    if (STRING_EQ(js, t, len, "whiteLevel") == 0) {
                        cpy_int16(&ctx->white_level, js, &tokens[++i]);
                    }
                    else if (STRING_EQ(js, t, len, "blackLevel") == 0) {
                        cpy_int16(&ctx->black_level, js, &tokens[++i]);
                    }
                    break;

                case 11:
                    if (STRING_EQ(js, t, len, "build.model") == 0) {
                        cpy_string(ctx->model, MR_MAX_STRING, js, &tokens[++i]);
                    }
                    break;

                case 12:
                    if (STRING_EQ(js, t, len, "colorMatrix1") == 0) {
                        cpy_matrix(ctx->color_matrix1, 9, js, tokens, &i);
                    }
                    else if (STRING_EQ(js, t, len, "colorMatrix2") == 0) {
                        cpy_matrix(ctx->color_matrix2, 9, js, tokens, &i);
                    }
                    else if (STRING_EQ(js, t, len, "focalLengths") == 0) {
                        cpy_double(&ctx->focal_length, js, &tokens[++i]);
                    }
                    break;

                case 13:
                    if (STRING_EQ(js, t, len, "audioChannels") == 0) {
                        cpy_int32(&ctx->audio_channels, js, &tokens[++i]);
                    }
                    break;

                case 14:
                    if (STRING_EQ(js, t, len, "forwardMatrix1") == 0) {
                        cpy_matrix(ctx->forward_matrix1, 9, js, tokens, &i);
                    }
                    else if (STRING_EQ(js, t, len, "forwardMatrix2") == 0) {
                        cpy_matrix(ctx->forward_matrix2, 9, js, tokens, &i);
                    }
                    break;

                case 15:
                    if (STRING_EQ(js, t, len, "audioSampleRate") == 0) {
                        cpy_int32(&ctx->audio_sample_rate, js, &tokens[++i]);
                    }
                    break;

                case 16:
                    if (STRING_EQ(js, t, len, "colorIlluminant1") == 0) {
                        cpy_string(ctx->color_illuminant1, MR_MAX_STRING, js, &tokens[++i]);
                    }
                    else if (STRING_EQ(js, t, len, "colorIlluminant2") == 0) {
                        cpy_string(ctx->color_illuminant2, MR_MAX_STRING, js, &tokens[++i]);
                    }
                    else if (STRING_EQ(js, t, len, "sensorArrangment") == 0) {
                        parse_sensor_arrangment(&ctx->cfa_pattern, js, &tokens[++i]);
                    }
                    break;

                case 18:
                    if (STRING_EQ(js, t, len, "calibrationMatrix1") == 0) {
                        cpy_matrix(ctx->calibration_matrix1, 9, js, tokens, &i);
                    }
                    else if (STRING_EQ(js, t, len, "calibrationMatrix2") == 0) {
                        cpy_matrix(ctx->calibration_matrix2, 9, js, tokens, &i);
                    }
                    else if (STRING_EQ(js, t, len, "build.manufacturer") == 0) {
                        cpy_string(ctx->manufacturer, MR_MAX_STRING, js, &tokens[++i]);
                    }
                    break;

                default:
                    break;
            }
        }

        i++;
    }

    return 0;
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
        jsmn_parser parser;
        jsmntok_t tokens[256];      // We expect no more than 256 tokens

        jsmn_init(&parser);

        int nb_tokens = jsmn_parse(&parser, (const char*)pkt.data, pkt.size, tokens, sizeof(tokens) / sizeof(tokens[0]));
        if (nb_tokens < -1) {
            res = kMrErrorMetadata;
        }
        else if (nb_tokens == -1) {
            nb_tokens = 255;
        }
        else {
            nb_tokens--;   // Process key, value together
        }

        const char *js = (char*)pkt.data;

        int i = 0;
        while (i < nb_tokens)
        {
            jsmntok_t *t = &tokens[i];

            if (t->type == JSMN_STRING)
            {
                int len = t->end - t->start;

                switch (len)
                {
                    case 3:
                        if (STRING_EQ(js, t, len, "iso") == 0) {
                            cpy_int32(&frame_data->iso, js, &tokens[++i]);
                        }
                        break;

                    case 5:
                        if (STRING_EQ(js, t, len, "width") == 0) {
                            cpy_int32(&frame_data->width, js, &tokens[++i]);
                        }
                        break;

                    case 6:
                        if (STRING_EQ(js, t, len, "height") == 0) {
                            cpy_int32(&frame_data->height, js, &tokens[++i]);
                        }
                        break;

                    case 11:
                        if (STRING_EQ(js, t, len, "orientation") == 0) {
                            cpy_int16(&frame_data->orientation, js, &tokens[++i]);
                        }
                        else if (STRING_EQ(js, t, len, "pixelFormat") == 0)
                        {
                            char buf[6] = "";
                            cpy_string(buf, 5, js, &tokens[++i]);
                            if (strncmp(buf, "raw16", 5) == 0) {
                                frame_data->stored_pixel_format = 16;
                            }
                        }
                        break;

                    case 12:
                        if (STRING_EQ(js, t, len, "exposureTime") == 0) {
                            cpy_int64(&frame_data->exposure_time, js, &tokens[++i]);
                        }
                        break;

                    case 13:
                        if (STRING_EQ(js, t, len, "originalWidth") == 0) {
                            cpy_int32(&frame_data->original_width, js, &tokens[++i]);
                        }
                        else if (STRING_EQ(js, t, len, "asShotNeutral") == 0) {
                            cpy_matrix(frame_data->as_shot_neutral, 3, js, tokens, &i);
                        }
                        break;

                    case 14:
                        if (STRING_EQ(js, t, len, "originalHeight") == 0) {
                            cpy_int32(&frame_data->original_height, js, &tokens[++i]);
                        }
                        break;

                    case 15:
                        if (STRING_EQ(js, t, len, "compressionType") == 0) {
                            cpy_int16(&frame_data->compression_type, js, &tokens[++i]);
                        }
                        break;

                    case 16:
                        if (STRING_EQ(js, t, len, "recvdTimestampMs") == 0) {
                            cpy_int64(&frame_data->timestamp, js, &tokens[++i]);
                        }
                        break;

                    default:
                        break;
                }
            }

            i++;
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
FILE* mr_get_file_handle(mr_ctx_t *ctx)
{
    return ctx->fd;
}
