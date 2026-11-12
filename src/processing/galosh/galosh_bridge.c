/*!
 * \file galosh_bridge.c
 * \brief MLV-App bridge for the GALOSH RAW Bayer denoiser
 */

#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#endif

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif

#define GALOSH_RAW_NOMAIN
#define GALOSH_NO_F16_STORAGE
#include "galosh_raw_cpu.c"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "galosh_bridge.h"
#include "galosh_vulkan.h"

static pthread_mutex_t galosh_mutex = PTHREAD_MUTEX_INITIALIZER;

static void galosh_cfa_phase(uint32_t cfa_pattern, int *row, int *col)
{
    /* GALOSH slot 0 is red.  Put red at (0,0) without touching pixels. */
    switch (cfa_pattern)
    {
        case 0x01020001: /* GRBG: red at (0,1) */
            *row = 0; *col = 1;
            break;
        case 0x01000201: /* GBRG: red at (1,0) */
            *row = 1; *col = 0;
            break;
        case 0x00010102: /* BGGR: red at (1,1) */
            *row = 1; *col = 1;
            break;
        case 0x02010100: /* RGGB */
        default:
            *row = 0; *col = 0;
            break;
    }
}

static void galosh_float_to_normalized(const float *src, float *dst,
                                       int width, int height, int dst_stride,
                                       float black_level, float white_level)
{
    const float inv_range = 1.0f / (white_level - black_level);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < height; ++y)
    {
        const float *src_row = src + (size_t)y * width;
        float *dst_row = dst + (size_t)y * dst_stride;
        for (int x = 0; x < width; ++x)
        {
            float v = (src_row[x] - black_level) * inv_range;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            dst_row[x] = v;
        }
    }
}

static void galosh_normalized_to_float(const float *src, float *dst,
                                       int width, int height, int src_stride,
                                       float black_level, float white_level)
{
    const float range = white_level - black_level;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < height; ++y)
    {
        const float *src_row = src + (size_t)y * src_stride;
        float *dst_row = dst + (size_t)y * width;
        for (int x = 0; x < width; ++x)
        {
            float v = src_row[x] * range + black_level;
            if (v < 0.0f) v = 0.0f;
            if (v > 65535.0f) v = 65535.0f;
            dst_row[x] = v;
        }
    }
}

static int galosh_run_cpu(const float *in, float *out, int width, int height,
                          int cfa_row, int cfa_col,
                          float luma_strength, float chroma_strength,
                          int fast)
{
    dt_iop_roi_t roi = { .width = width, .height = height };
    static int kaiser_ready = 0;

    if (!kaiser_ready)
    {
        init_galosh_kaiser();
        kaiser_ready = 1;
    }

    g_galosh_wht_block = fast ? 4 : 8;
    g_galosh_upsample_fast = fast ? 1 : 0;
    g_galosh_super_clean_threshold = 0.0f;

    galosh_raw_denoise(in, out, &roi, luma_strength, chroma_strength,
                       0, cfa_row, cfa_col);
    return 0;
}

/* Run the selected backend.  Caller must hold galosh_mutex. */
static int galosh_run_backend(processingObject_t *processing,
                              const float *in, float *out,
                              int width, int height,
                              int cfa_row, int cfa_col,
                              float luma, float chroma)
{
    if (processing->galoshBackend == GALOSH_BACKEND_GPU
     && galosh_vulkan_available())
    {
        return galosh_vulkan_denoise(in, out, width, height,
                                     luma, chroma, processing->galoshFast,
                                     processing->galoshAlpha,
                                     processing->galoshSigmaSq, 1);
    }

    return galosh_run_cpu(in, out, width, height,
                          cfa_row, cfa_col, luma, chroma,
                          processing->galoshFast);
}

/* CFA-aware horizontal upsampling of a normalized Bayer row.
 *
 * The two horizontal CFA phases are interpolated independently, so no
 * cross-colour mixing occurs.  The factor must be odd: then parity(factor*x)
 * equals parity(x) and the 2x2 CFA pattern (and its phase) is preserved.
 * Catmull-Rom is used for a C1-continuous result; edge samples are clamped. */
static void galosh_upsample_horizontal_cfa(const float *src, float *dst,
                                           int width, int height, int factor)
{
    const int out_width = width * factor;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < height; ++y)
    {
        const float *src_row = src + (size_t)y * width;
        float *dst_row = dst + (size_t)y * out_width;

        for (int xo = 0; xo < out_width; ++xo)
        {
            const int parity = xo & 1;
            const float u = (float)xo / (float)factor;
            const float t = (u - (float)parity) * 0.5f;
            int i = (int)floorf(t);
            const float f = t - (float)i;

            const int nplane = (width - parity + 1) >> 1;
            int n0 = i - 1, n1 = i, n2 = i + 1, n3 = i + 2;
            if (n0 < 0) n0 = 0;
            if (n1 < 0) n1 = 0;
            if (n2 < 0) n2 = 0;
            if (n3 < 0) n3 = 0;
            if (n0 >= nplane) n0 = nplane - 1;
            if (n1 >= nplane) n1 = nplane - 1;
            if (n2 >= nplane) n2 = nplane - 1;
            if (n3 >= nplane) n3 = nplane - 1;

            const float p0 = src_row[parity + 2 * n0];
            const float p1 = src_row[parity + 2 * n1];
            const float p2 = src_row[parity + 2 * n2];
            const float p3 = src_row[parity + 2 * n3];

            const float f2 = f * f;
            const float f3 = f2 * f;
            const float w0 = -0.5f * f3 +       f2 - 0.5f * f;
            const float w1 =  1.5f * f3 - 2.5f * f2 + 1.0f;
            const float w2 = -1.5f * f3 + 2.0f * f2 + 0.5f * f;
            const float w3 =  0.5f * f3 - 0.5f * f2;

            float v = w0 * p0 + w1 * p1 + w2 * p2 + w3 * p3;
            /* GALOSH's raw domain is [0,1]; cubic overshoot outside that
             * range only creates extreme GAT values. */
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            dst_row[xo] = v;
        }
    }
}

/* Sample the denoised upsampled frame back at the original CFA positions.
 * Every original sample has an exact counterpart at x' = factor*x. */
static void galosh_downsample_horizontal_cfa(const float *src, float *dst,
                                             int width, int height,
                                             int src_stride, int factor)
{
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < height; ++y)
    {
        const float *src_row = src + (size_t)y * src_stride;
        float *dst_row = dst + (size_t)y * width;
        for (int x = 0; x < width; ++x)
        {
            dst_row[x] = src_row[(size_t)x * factor];
        }
    }
}

int galosh_bayer_denoise_ex(processingObject_t *processing,
                            float *frame,
                            int width, int height,
                            uint32_t cfa_pattern,
                            float black_level,
                            float white_level,
                            uint64_t frame_index,
                            int anamorphic_x)
{
    if (!processing || !frame || width <= 0 || height <= 0) return 1;
    if (processing->galoshStrength == 0) return 1;
    if (processing->galoshLuma == 0 && processing->galoshChroma == 0) return 1;
    if (width < GALOSH_BLOCK_SIZE * 2 || height < GALOSH_BLOCK_SIZE * 2) return 1;
    if (!(white_level > black_level)) return 1;

    const float strength = processing->galoshStrength / 100.0f;
    const float luma = strength * (processing->galoshLuma / 100.0f);
    const float chroma = strength * (processing->galoshChroma / 100.0f);

    /* The GALOSH pipeline's luma path is the structural driver.  With luma 0
     * the CPU reference bypasses everything; do the same on every backend so
     * the Vulkan path cannot run a degenerate luma stage. */
    if (luma <= 0.0f) return 1;

    /* Only odd factors preserve the 2x2 CFA parity. */
    if (anamorphic_x < 1 || anamorphic_x > 5 || (anamorphic_x & 1) == 0)
        anamorphic_x = 1;

    int cfa_row, cfa_col;
    galosh_cfa_phase(cfa_pattern, &cfa_row, &cfa_col);

    /* GALOSH requires even dimensions.  Pad to the next even size by
     * replicating the nearest same-parity row/column, so the padded edge
     * keeps the CFA phase. */
    const int padded_width = (width + 1) & ~1;
    const int padded_height = (height + 1) & ~1;
    const size_t padded_count = (size_t)padded_width * padded_height;
    const int up_width = padded_width * anamorphic_x;
    const int up_height = padded_height;
    const size_t up_count = (size_t)up_width * up_height;

    float *in = (float *)malloc(padded_count * sizeof(float));
    if (!in) return 1;

    pthread_mutex_lock(&galosh_mutex);

    /* Normalize into the padded buffer. */
    galosh_float_to_normalized(frame, in, width, height, padded_width,
                               black_level, white_level);
    if (padded_width != width)
    {
        const int src_x = (width >= 2) ? width - 2 : width - 1;
        for (int y = 0; y < height; ++y)
        {
            float *row = in + (size_t)y * padded_width;
            row[width] = row[src_x];
        }
    }
    if (padded_height != height)
    {
        const int src_y = (height >= 2) ? height - 2 : height - 1;
        for (int y = height; y < padded_height; ++y)
        {
            memcpy(in + (size_t)y * padded_width,
                   in + (size_t)src_y * padded_width,
                   (size_t)padded_width * sizeof(float));
        }
    }

    /* Reset the held noise model if anything that changes its meaning changed. */
    if (processing->galoshNoiseValid
     && (processing->galoshLastBlack != black_level
      || processing->galoshLastWhite != white_level
      || processing->galoshLastCfa != cfa_pattern
      || processing->galoshLastWidth != (uint16_t)width
      || processing->galoshLastHeight != (uint16_t)height))
    {
        processing->galoshNoiseValid = 0;
    }

    /* Determine the noise model used for this frame. */
    g_galosh_alpha_override = 0.0f;
    g_galosh_sigma_sq_override = 0.0f;

    if (processing->galoshNoiseMode == 1 && processing->galoshNoiseValid)
    {
        /* hold */
    }
    else
    {
        galosh_noise_params_t est = galosh_estimate_noise(in, padded_width, padded_height);

        /* The reference CLI feeds the blind estimate through unchanged.
         * Keep only a finite/positive sanity guard so invalid data cannot
         * poison the GAT lookup tables. */
        if (!isfinite(est.alpha) || !(est.alpha > 0.0f)) est.alpha = 1e-4f;
        if (!isfinite(est.sigma_sq) || est.sigma_sq < 0.0f) est.sigma_sq = 0.0f;

        if (processing->galoshNoiseMode == 2 && processing->galoshNoiseValid)
        {
            const float beta = 0.1f;
            processing->galoshAlpha = beta * est.alpha
                                    + (1.0f - beta) * processing->galoshAlpha;
            processing->galoshSigmaSq = beta * est.sigma_sq
                                      + (1.0f - beta) * processing->galoshSigmaSq;
        }
        else
        {
            processing->galoshAlpha = est.alpha;
            processing->galoshSigmaSq = est.sigma_sq;
        }
        processing->galoshNoiseValid = 1;
        processing->galoshNoiseFrame = frame_index;
    }

    g_galosh_alpha_override = processing->galoshAlpha;
    g_galosh_sigma_sq_override = processing->galoshSigmaSq;

    processing->galoshLastBlack = black_level;
    processing->galoshLastWhite = white_level;
    processing->galoshLastCfa = cfa_pattern;
    processing->galoshLastWidth = (uint16_t)width;
    processing->galoshLastHeight = (uint16_t)height;

    int result = 1;

    if (anamorphic_x == 1)
    {
        float *out = (float *)malloc(padded_count * sizeof(float));
        if (out)
        {
            result = galosh_run_backend(processing, in, out,
                                        padded_width, padded_height,
                                        cfa_row, cfa_col, luma, chroma);
            if (result == 0)
            {
                galosh_normalized_to_float(out, frame, width, height,
                                           padded_width, black_level, white_level);
            }
            free(out);
        }
    }
    else
    {
        float *up_in = (float *)malloc(up_count * sizeof(float));
        float *up_out = (float *)malloc(up_count * sizeof(float));
        if (up_in && up_out)
        {
            galosh_upsample_horizontal_cfa(in, up_in, padded_width, padded_height,
                                           anamorphic_x);
            /* The original-grid buffer is no longer needed and is the
             * smaller one; release it before the backend allocates its own
             * working set. */
            free(in);
            in = NULL;

            result = galosh_run_backend(processing, up_in, up_out,
                                        up_width, up_height,
                                        cfa_row, cfa_col, luma, chroma);
            if (result == 0)
            {
                float *out = (float *)malloc(padded_count * sizeof(float));
                if (out)
                {
                    galosh_downsample_horizontal_cfa(up_out, out,
                                                     padded_width, padded_height,
                                                     up_width, anamorphic_x);
                    galosh_normalized_to_float(out, frame, width, height,
                                               padded_width, black_level, white_level);
                    free(out);
                }
                else
                {
                    result = 1;
                }
            }
        }
        free(up_in);
        free(up_out);
    }

    pthread_mutex_unlock(&galosh_mutex);

    free(in);
    return result;
}

int galosh_bayer_denoise(processingObject_t *processing,
                         float *frame,
                         int width, int height,
                         uint32_t cfa_pattern,
                         float black_level,
                         float white_level,
                         uint64_t frame_index)
{
    return galosh_bayer_denoise_ex(processing, frame, width, height,
                                   cfa_pattern, black_level, white_level,
                                   frame_index, 1);
}

void galosh_shutdown(void)
{
    galosh_vulkan_shutdown();
}
