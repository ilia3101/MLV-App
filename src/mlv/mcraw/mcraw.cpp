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

#include "mcraw.h"
#include <motioncam/Decoder.hpp>
#include <motioncam/Container.hpp>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath> // lround

using namespace motioncam;

// Internal context structure
struct mr_ctx_s {
    Decoder* decoder;
    FILE* fd; // Kept for legacy reasons

    // Metadata caches
    double color_matrix1[9];
    double color_matrix2[9];
    double forward_matrix1[9];
    double forward_matrix2[9];
    double calibration_matrix1[9];
    double calibration_matrix2[9];
    double as_shot_neutral[3];

    int16_t white_level;
    int16_t black_level;
    double aperture;
    double focal_length;
    int32_t audio_sample_rate;
    int32_t audio_channels;
    uint32_t cfa_pattern;

    char model[MR_MAX_STRING + 1];
    char color_illuminant1[MR_MAX_STRING + 1];
    char color_illuminant2[MR_MAX_STRING + 1];

    // Derived
    mr_frame_data_t frame_data;
    mr_rational_t frame_rate;

    int last_error;
    char error_message[256];
};

// Helper for JSON - Exception Safe
template<typename T>
T get_meta(const nlohmann::json& j, const char* key, T def) {
    try {
        if (j.contains(key) && !j[key].is_null()) {
            if (j[key].is_number()) return j[key].get<T>();
            if (j[key].is_string()) return j[key].get<T>();
            return j[key].get<T>();
        }
    } catch (...) {}
    return def;
}

template<typename T>
void get_matrix(const nlohmann::json& j, const char* key, double* dst, int size) {
    try {
        if (j.contains(key) && j[key].is_array()) {
            std::vector<T> v = j[key].get<std::vector<T>>();
            for(int i=0; i<size && i<(int)v.size(); i++) dst[i] = (double)v[i];
        }
    } catch (...) {}
}

extern "C" {

mr_ctx_t* mr_decoder_new(int verbose) {
    mr_ctx_t* ctx = new mr_ctx_t();
    std::memset(ctx, 0, sizeof(mr_ctx_t));
    return ctx;
}

void mr_decoder_free(mr_ctx_t *ctx) {
    if (ctx) {
        if (ctx->decoder) delete ctx->decoder;
        delete ctx;
    }
}

int mr_decoder_open(mr_ctx_t *ctx, const char *filename) {
    if (!ctx) return -1;

    // We let Decoder handle file opening fully.
    // We keep ctx->fd null or sync it if we can access it from Decoder, but Decoder owns it.
    // Decoder(string) opens file.

    try {
        ctx->decoder = new Decoder(filename);
        // Note: Decoder closes its own file.
        return 0;
    } catch (const std::exception& e) {
        snprintf(ctx->error_message, 255, "%s", e.what());
        return -1;
    }
}

void mr_decoder_close(mr_ctx_t *ctx) {
    if (ctx && ctx->decoder) {
        delete ctx->decoder;
        ctx->decoder = nullptr;
    }
}

int mr_decoder_parse(mr_ctx_t *ctx) {
    if (!ctx || !ctx->decoder) return -1;

    try {
        const auto& meta = ctx->decoder->getContainerMetadata();

        if (meta.contains("deviceSpecificProfile")) {
            const auto& deviceProfile = meta["deviceSpecificProfile"];
            auto mod = get_meta<std::string>(deviceProfile, "deviceModel", "");
            strncpy(ctx->model, mod.c_str(), MR_MAX_STRING);
        } else {
            strncpy(ctx->model, "", MR_MAX_STRING);
        }

        get_matrix<float>(meta, "colorMatrix1", ctx->color_matrix1, 9);
        get_matrix<float>(meta, "colorMatrix2", ctx->color_matrix2, 9);
        get_matrix<float>(meta, "forwardMatrix1", ctx->forward_matrix1, 9);
        get_matrix<float>(meta, "forwardMatrix2", ctx->forward_matrix2, 9);
        get_matrix<float>(meta, "calibrationMatrix1", ctx->calibration_matrix1, 9);
        get_matrix<float>(meta, "calibrationMatrix2", ctx->calibration_matrix2, 9);

        strncpy(ctx->color_illuminant1, get_meta<std::string>(meta, "colorIlluminant1", "").c_str(), MR_MAX_STRING);
        strncpy(ctx->color_illuminant2, get_meta<std::string>(meta, "colorIlluminant2", "").c_str(), MR_MAX_STRING);

        ctx->white_level = (int16_t)get_meta<double>(meta, "whiteLevel", 1023.0);

        ctx->black_level = 0;
        if (meta.contains("blackLevel")) {
            const auto& bl = meta["blackLevel"];
            if (bl.is_array() && !bl.empty()) {
                if (bl[0].is_number()) ctx->black_level = (int16_t)bl[0].get<double>();
            } else if (bl.is_number()) {
                ctx->black_level = (int16_t)bl.get<double>();
            }
        }

        // Audio Info
        ctx->audio_sample_rate = ctx->decoder->audioSampleRateHz();
        ctx->audio_channels = ctx->decoder->numAudioChannels();

        ctx->aperture = 0.0;
        if (meta.contains("apertures") && meta["apertures"].is_array() && !meta["apertures"].empty()) {
            ctx->aperture = meta["apertures"][0].get<double>();
        }

        ctx->focal_length = 0.0;
        if (meta.contains("focalLengths") && meta["focalLengths"].is_array() && !meta["focalLengths"].empty()) {
            ctx->focal_length = meta["focalLengths"][0].get<double>();
        }

        auto cfa = get_meta<std::string>(meta, "sensorArrangment", "");
        if (cfa.empty()) cfa = get_meta<std::string>(meta, "sensorArrangement", "rggb");

        ctx->cfa_pattern = 0;
        const char* p = cfa.c_str();
        for (size_t i = 0; i < 4 && i < cfa.length(); i++) {
            uint32_t n = (p[i] == 'g') ? 1 : (p[i] == 'b') ? 2 : 0;
            ctx->cfa_pattern |= (n << (i * 8));
        }

        // Frame Data (First Frame) - Use Timestamps from Index
        const auto& offsets = ctx->decoder->getOffsets();
        if (!offsets.empty()) {
            nlohmann::json frameMeta;
            ctx->decoder->loadFrameMetadata(offsets[0].timestamp, frameMeta);

            ctx->frame_data.iso = get_meta<int>(frameMeta, "iso", 0);
            ctx->frame_data.width = get_meta<int>(frameMeta, "width", 0);
            ctx->frame_data.height = get_meta<int>(frameMeta, "height", 0);
            ctx->frame_data.exposure_time = get_meta<int64_t>(frameMeta, "exposureTime", 0);
            ctx->frame_data.orientation = get_meta<int>(frameMeta, "orientation", 0);
            ctx->frame_data.compression_type = get_meta<int>(frameMeta, "compressionType", 0);
            auto tsStr = get_meta<std::string>(frameMeta, "recvdTimestampMs", "");
            ctx->frame_data.timestamp = std::stoll(tsStr);

            std::string pf = "";
            if(frameMeta.contains("pixelFormat")) pf = frameMeta["pixelFormat"].get<std::string>();
            ctx->frame_data.stored_pixel_format = (pf == "raw16") ? 16 : 10;

            get_matrix<double>(frameMeta, "asShotNeutral", ctx->as_shot_neutral, 3);
            memcpy(ctx->frame_data.as_shot_neutral, ctx->as_shot_neutral, 3*sizeof(double));
        }

        // Frame Rate
        size_t count = offsets.size();
        if (count > 1) {
            int64_t total = offsets[count - 1].timestamp - offsets[0].timestamp;
            double frame_duration = ((double)(total) / 1000000.) / (count - 1);
            double fps = 1000. / frame_duration;
            ctx->frame_rate.den = 1;
            ctx->frame_rate.num = (fps < 1.5) ? 1 : (int)lround(fps);
        } else {
            ctx->frame_rate.num = 30; ctx->frame_rate.den = 1;
        }

    } catch (const std::exception& e) {
        snprintf(ctx->error_message, 255, "Metadata Warn: %s", e.what());
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Getters (Now using Decoder internal vectors directly!)
// ---------------------------------------------------------------------------

uint32_t mr_get_frame_count(mr_ctx_t *ctx) {
    return ctx && ctx->decoder ? ctx->decoder->getOffsets().size() : 0;
}

mr_buffer_offset_t* mr_get_index(mr_ctx_t *ctx) {
    if (!ctx || !ctx->decoder) return nullptr;
    // BufferOffset matches mr_buffer_offset_t layout (int64, int64).
    return (mr_buffer_offset_t*)ctx->decoder->getOffsets().data();
}

uint32_t mr_get_audio_packet_count(mr_ctx_t *ctx) {
    return ctx && ctx->decoder ? ctx->decoder->getAudioOffsets().size() : 0;
}

mr_buffer_offset_t* mr_get_audio_index(mr_ctx_t *ctx) {
    if (!ctx || !ctx->decoder) return nullptr;
    return (mr_buffer_offset_t*)ctx->decoder->getAudioOffsets().data();
}

// ... other getters are just struct access ...
int32_t mr_get_width(mr_ctx_t *ctx) { return ctx->frame_data.width; }
int32_t mr_get_height(mr_ctx_t *ctx) { return ctx->frame_data.height; }
int32_t mr_get_stored_format(mr_ctx_t *ctx) { return ctx->frame_data.stored_pixel_format; }
int32_t mr_get_bits_per_pixel(mr_ctx_t *ctx) {
    return ctx->white_level <= 0x3FF ? 10 : ctx->white_level <= 0xFFF ? 12 : 14;
}
int mr_get_frame_rate(mr_ctx_t *ctx, int *num, int *den) {
    *num = ctx->frame_rate.num; *den = ctx->frame_rate.den; return 0;
}
int16_t mr_get_black_level(mr_ctx_t *ctx) { return ctx->black_level; }
int16_t mr_get_white_level(mr_ctx_t *ctx) { return ctx->white_level; }

double* mr_get_color_matrix1(mr_ctx_t *ctx) { return ctx->color_matrix1; }
double* mr_get_color_matrix2(mr_ctx_t *ctx) { return ctx->color_matrix2; }
double* mr_get_forward_matrix1(mr_ctx_t *ctx) { return ctx->forward_matrix1; }
double* mr_get_forward_matrix2(mr_ctx_t *ctx) { return ctx->forward_matrix2; }

double mr_get_focal_length(mr_ctx_t *ctx) { return ctx->focal_length; }
double mr_get_aperture(mr_ctx_t *ctx) { return ctx->aperture; }
int mr_get_iso(mr_ctx_t *ctx) { return ctx->frame_data.iso; }
int64_t mr_get_exposure_time(mr_ctx_t *ctx) { return ctx->frame_data.exposure_time; }
double* mr_get_as_shot_neutral(mr_ctx_t *ctx) { return ctx->frame_data.as_shot_neutral; }
int64_t mr_get_timestamp(mr_ctx_t *ctx) { return ctx->frame_data.timestamp; }

const char* mr_get_model(mr_ctx_t *ctx) { return ctx->model; }

int32_t mr_get_audio_sample_rate(mr_ctx_t *ctx) { return ctx->audio_sample_rate; }
int32_t mr_get_audio_channels(mr_ctx_t *ctx) { return ctx->audio_channels; }
uint32_t mr_get_cfa_pattern(mr_ctx_t *ctx) { return ctx->cfa_pattern; }
int mr_get_compression_type(mr_ctx_t *ctx) { return ctx->frame_data.compression_type; }

FILE* mr_get_file_handle(mr_ctx_t *ctx) { return ctx && ctx->decoder ? ctx->decoder->getFileHandle() : nullptr; }

// ---------------------------------------------------------------------------

void mr_packet_free(mr_packet_t *pkt) {
    if(pkt->data) free(pkt->data);
}

// Deprecated RAW IO
int mr_read_video_frame(FILE *fd, int64_t offset, mr_packet_t *pkt) { return -1; }
int mr_read_audio_packet(FILE *fd, int64_t offset, mr_packet_t *pkt) { return -1; }
int mr_read_frame_metadata(FILE *fd, mr_frame_data_t *frame_data) { return -1; }

// High-Level API (Using Decoder Internal Logic)
int mr_load_frame(mr_ctx_t* ctx, uint32_t index, uint8_t* dest_buffer, mr_frame_data_t* dest_metadata) {
    if (!ctx || !ctx->decoder) return -1;

    try {
        const auto& offsets = ctx->decoder->getOffsets();
        if (index >= offsets.size()) return -1;

        Timestamp ts = offsets[index].timestamp;
        std::vector<uint8_t> data;
        nlohmann::json j;

        ctx->decoder->loadFrame(ts, data, j);

        if (dest_buffer) {
            memcpy(dest_buffer, data.data(), data.size());
        }

        // Populate dest_metadata if needed...
        return 0;
    } catch (const std::exception& e) {
        snprintf(ctx->error_message, 255, "Load Frame Error: %s", e.what());
        return -1;
    }
}

} // extern "C"
