#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

#include "debayer.h"
#include "librtprocesswrapper.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

static uint16_t float_to_uint16(float value)
{
    if(!(value > 0.0f)) {
        return 0;
    }
    if(value >= 65535.0f) {
        return 65535;
    }
    return (uint16_t)value;
}

static float median9_float(float *values)
{
    for(int i = 1; i < 9; ++i)
    {
        float value = values[i];
        int j = i - 1;
        while(j >= 0 && values[j] > value)
        {
            values[j + 1] = values[j];
            --j;
        }
        values[j + 1] = value;
    }

    return values[4];
}

static float median_float(float *values, int value_count)
{
    for(int index = 1; index < value_count; ++index)
    {
        float value = values[index];
        int insert_at = index - 1;
        while(insert_at >= 0 && values[insert_at] > value)
        {
            values[insert_at + 1] = values[insert_at];
            --insert_at;
        }
        values[insert_at + 1] = value;
    }

    int middle = value_count / 2;
    if(value_count & 1) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) * 0.5f;
}

static int highlight_is_protected(const uint8_t *highlight_row, int column)
{
    return highlight_row && (highlight_row[column] & DEBAYER_FCS_HIGHLIGHT_PROTECT);
}

static void convert_row_to_yiq(const float *frame, int width, int row, float *Y, float *I, float *Q)
{
    const float *pix = frame + (row * width * 3);

    for(int x = 0; x < width; ++x, pix += 3)
    {
        float red = pix[0];
        float green = pix[1];
        float blue = pix[2];

        Y[x] = 0.299f * red + 0.587f * green + 0.114f * blue;
        I[x] = 0.596f * red - 0.275f * green - 0.321f * blue;
        Q[x] = 0.212f * red - 0.523f * green + 0.311f * blue;
    }
}

static void median_chroma_row(
    int width,
    const float *prev_i,
    const float *curr_i,
    const float *next_i,
    const float *prev_q,
    const float *curr_q,
    const float *next_q,
    const uint8_t *prev_highlights,
    const uint8_t *curr_highlights,
    const uint8_t *next_highlights,
    float *out_i,
    float *out_q)
{
    out_i[0] = curr_i[0];
    out_q[0] = curr_q[0];

    if(!curr_highlights)
    {
        for(int x = 1; x < width - 1; ++x)
        {
            float values_i[9] = {
                prev_i[x - 1], prev_i[x], prev_i[x + 1],
                curr_i[x - 1], curr_i[x], curr_i[x + 1],
                next_i[x - 1], next_i[x], next_i[x + 1]
            };
            float values_q[9] = {
                prev_q[x - 1], prev_q[x], prev_q[x + 1],
                curr_q[x - 1], curr_q[x], curr_q[x + 1],
                next_q[x - 1], next_q[x], next_q[x + 1]
            };

            out_i[x] = median9_float(values_i);
            out_q[x] = median9_float(values_q);
        }
    }
    else
    {
        const float *i_rows[3] = { prev_i, curr_i, next_i };
        const float *q_rows[3] = { prev_q, curr_q, next_q };
        const uint8_t *highlight_rows[3] = { prev_highlights, curr_highlights, next_highlights };

        for(int x = 1; x < width - 1; ++x)
        {
            if(highlight_is_protected(curr_highlights, x))
            {
                out_i[x] = curr_i[x];
                out_q[x] = curr_q[x];
                continue;
            }

            float values_i[9];
            float values_q[9];
            int value_count = 0;

            for(int sample_row = 0; sample_row < 3; ++sample_row)
            {
                for(int sample_column = x - 1; sample_column <= x + 1; ++sample_column)
                {
                    if(highlight_is_protected(highlight_rows[sample_row], sample_column)) continue;

                    values_i[value_count] = i_rows[sample_row][sample_column];
                    values_q[value_count] = q_rows[sample_row][sample_column];
                    ++value_count;
                }
            }

            if(value_count == 9)
            {
                out_i[x] = median9_float(values_i);
                out_q[x] = median9_float(values_q);
            }
            else if(value_count > 0)
            {
                out_i[x] = median_float(values_i, value_count);
                out_q[x] = median_float(values_q, value_count);
            }
            else
            {
                out_i[x] = curr_i[x];
                out_q[x] = curr_q[x];
            }
        }
    }

    out_i[width - 1] = curr_i[width - 1];
    out_q[width - 1] = curr_q[width - 1];
}

static float luminance_similarity_weight(float center_y, float sample_y)
{
    float luminance_scale = 1024.0f + 0.05f * MAX(center_y, sample_y);
    float ratio = fabsf(sample_y - center_y) / luminance_scale;
    return 1.0f / (1.0f + ratio * ratio);
}

static void store_edge_aware_pixel(
    float *pixel,
    float luminance,
    float original_i,
    float original_q,
    float candidate_i,
    float candidate_q,
    const float channel_max[3])
{
    float original_chroma_squared = original_i * original_i + original_q * original_q;
    float chroma_dot = original_i * candidate_i + original_q * candidate_q;

    if(original_chroma_squared > 4096.0f && chroma_dot < 0.0f)
    {
        candidate_i = 0.0f;
        candidate_q = 0.0f;
    }

    float candidate[3] = {
        luminance + 0.956f * candidate_i + 0.621f * candidate_q,
        luminance - 0.272f * candidate_i - 0.647f * candidate_q,
        luminance - 1.105f * candidate_i + 1.702f * candidate_q
    };
    float original[3] = { pixel[0], pixel[1], pixel[2] };
    float correction_blend = 1.0f;

    for(int channel = 0; channel < 3; ++channel)
    {
        float correction = candidate[channel] - original[channel];
        float channel_blend = 1.0f;

        if(candidate[channel] < 0.0f && correction < 0.0f) {
            channel_blend = original[channel] / -correction;
        } else if(candidate[channel] > channel_max[channel] && correction > 0.0f) {
            channel_blend = (channel_max[channel] - original[channel]) / correction;
        }

        correction_blend = MIN(correction_blend, channel_blend);
    }

    correction_blend = MAX(0.0f, MIN(1.0f, correction_blend));
    for(int channel = 0; channel < 3; ++channel) {
        pixel[channel] = original[channel] + correction_blend * (candidate[channel] - original[channel]);
    }
}

static void finalize_false_color_row(
    float *frame,
    int width,
    int row,
    const float *prev_y,
    const float *curr_y,
    const float *next_y,
    const float *original_i,
    const float *original_q,
    const float *prev_i,
    const float *curr_i,
    const float *next_i,
    const float *prev_q,
    const float *curr_q,
    const float *next_q,
    const uint8_t *prev_highlights,
    const uint8_t *curr_highlights,
    const uint8_t *next_highlights,
    int edge_aware,
    const float channel_max[3])
{
    float *pix = frame + (row * width * 3);

    if(!highlight_is_protected(curr_highlights, 0))
    {
        pix[0] = curr_y[0] + 0.956f * curr_i[0] + 0.621f * curr_q[0];
        pix[1] = curr_y[0] - 0.272f * curr_i[0] - 0.647f * curr_q[0];
        pix[2] = curr_y[0] - 1.105f * curr_i[0] + 1.702f * curr_q[0];
    }

    if(!edge_aware)
    {
        if(!curr_highlights)
        {
            #pragma omp simd
            for(int x = 1; x < width - 1; ++x)
            {
                float out_i = (prev_i[x - 1] + prev_i[x] + prev_i[x + 1]
                             + curr_i[x - 1] + curr_i[x] + curr_i[x + 1]
                             + next_i[x - 1] + next_i[x] + next_i[x + 1]) / 9.0f;
                float out_q = (prev_q[x - 1] + prev_q[x] + prev_q[x + 1]
                             + curr_q[x - 1] + curr_q[x] + curr_q[x + 1]
                             + next_q[x - 1] + next_q[x] + next_q[x + 1]) / 9.0f;
                float *dst = pix + (x * 3);

                dst[0] = curr_y[x] + 0.956f * out_i + 0.621f * out_q;
                dst[1] = curr_y[x] - 0.272f * out_i - 0.647f * out_q;
                dst[2] = curr_y[x] - 1.105f * out_i + 1.702f * out_q;
            }
        }
        else
        {
            const float *i_rows[3] = { prev_i, curr_i, next_i };
            const float *q_rows[3] = { prev_q, curr_q, next_q };
            const uint8_t *highlight_rows[3] = { prev_highlights, curr_highlights, next_highlights };

            for(int x = 1; x < width - 1; ++x)
            {
                if(highlight_is_protected(curr_highlights, x)) continue;

                int value_count = 0;
                for(int sample_row = 0; sample_row < 3; ++sample_row)
                {
                    for(int sample_column = x - 1; sample_column <= x + 1; ++sample_column)
                    {
                        if(!highlight_is_protected(highlight_rows[sample_row], sample_column)) {
                            ++value_count;
                        }
                    }
                }

                float out_i;
                float out_q;
                if(value_count == 9)
                {
                    out_i = (prev_i[x - 1] + prev_i[x] + prev_i[x + 1]
                           + curr_i[x - 1] + curr_i[x] + curr_i[x + 1]
                           + next_i[x - 1] + next_i[x] + next_i[x + 1]) / 9.0f;
                    out_q = (prev_q[x - 1] + prev_q[x] + prev_q[x + 1]
                           + curr_q[x - 1] + curr_q[x] + curr_q[x + 1]
                           + next_q[x - 1] + next_q[x] + next_q[x + 1]) / 9.0f;
                }
                else if(value_count > 0)
                {
                    float sum_i = 0.0f;
                    float sum_q = 0.0f;
                    for(int sample_row = 0; sample_row < 3; ++sample_row)
                    {
                        for(int sample_column = x - 1; sample_column <= x + 1; ++sample_column)
                        {
                            if(highlight_is_protected(highlight_rows[sample_row], sample_column)) continue;

                            sum_i += i_rows[sample_row][sample_column];
                            sum_q += q_rows[sample_row][sample_column];
                        }
                    }
                    out_i = sum_i / value_count;
                    out_q = sum_q / value_count;
                }
                else
                {
                    continue;
                }

                float *dst = pix + (x * 3);
                dst[0] = curr_y[x] + 0.956f * out_i + 0.621f * out_q;
                dst[1] = curr_y[x] - 0.272f * out_i - 0.647f * out_q;
                dst[2] = curr_y[x] - 1.105f * out_i + 1.702f * out_q;
            }
        }
    }
    else
    {
        const float *y_rows[3] = { prev_y, curr_y, next_y };
        const float *i_rows[3] = { prev_i, curr_i, next_i };
        const float *q_rows[3] = { prev_q, curr_q, next_q };
        const uint8_t *highlight_rows[3] = { prev_highlights, curr_highlights, next_highlights };

        for(int x = 1; x < width - 1; ++x)
        {
            if(highlight_is_protected(curr_highlights, x)) continue;

            float weighted_i = 0.0f;
            float weighted_q = 0.0f;
            float total_weight = 0.0f;

            for(int sample_row = 0; sample_row < 3; ++sample_row)
            {
                for(int sample_x = x - 1; sample_x <= x + 1; ++sample_x)
                {
                    if(highlight_is_protected(highlight_rows[sample_row], sample_x)) continue;

                    float weight = luminance_similarity_weight(curr_y[x], y_rows[sample_row][sample_x]);
                    weighted_i += weight * i_rows[sample_row][sample_x];
                    weighted_q += weight * q_rows[sample_row][sample_x];
                    total_weight += weight;
                }
            }

            if(!(total_weight > 0.0f)) continue;

            float candidate_i = weighted_i / total_weight;
            float candidate_q = weighted_q / total_weight;
            store_edge_aware_pixel(
                pix + (x * 3), curr_y[x], original_i[x], original_q[x],
                candidate_i, candidate_q, channel_max);
        }
    }

    pix += (width - 1) * 3;
    if(!highlight_is_protected(curr_highlights, width - 1))
    {
        pix[0] = curr_y[width - 1] + 0.956f * curr_i[width - 1] + 0.621f * curr_q[width - 1];
        pix[1] = curr_y[width - 1] - 0.272f * curr_i[width - 1] - 0.647f * curr_q[width - 1];
        pix[2] = curr_y[width - 1] - 1.105f * curr_i[width - 1] + 1.702f * curr_q[width - 1];
    }
}

void debayerFalseColorCorrection(uint16_t *frame, int width, int height, int steps, const double wb_multipliers[3], int edge_aware, const uint8_t *highlight_map)
{
    if(!frame || !wb_multipliers || steps <= 0 || width < 3 || height < 4) {
        return;
    }
    if(steps > 5) {
        steps = 5;
    }

    double max_wb = MAX(wb_multipliers[0], MAX(wb_multipliers[1], wb_multipliers[2]));
    if(!(max_wb > 0.0)) {
        return;
    }

    float normalized_wb[3];
    for(int channel = 0; channel < 3; ++channel)
    {
        normalized_wb[channel] = (float)(wb_multipliers[channel] / max_wb);
        if(!(normalized_wb[channel] > 0.0f)) {
            return;
        }
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    size_t frame_values = pixel_count * 3;
    float *buffer = (float *)malloc(pixel_count * 8 * sizeof(float));
    if(!buffer) {
        return;
    }

    float *working_frame = buffer;
    float *Y = working_frame + frame_values;
    float *I = Y + pixel_count;
    float *Q = I + pixel_count;
    float *filtered_i = Q + pixel_count;
    float *filtered_q = filtered_i + pixel_count;
    float channel_max[3];

    for(int channel = 0; channel < 3; ++channel) {
        channel_max[channel] = 65535.0f * normalized_wb[channel];
    }

#pragma omp parallel for
    for(size_t pixel = 0; pixel < pixel_count; ++pixel)
    {
        size_t offset = pixel * 3;
        working_frame[offset] = frame[offset] * normalized_wb[0];
        working_frame[offset + 1] = frame[offset + 1] * normalized_wb[1];
        working_frame[offset + 2] = frame[offset + 2] * normalized_wb[2];
    }

    for(int step = 0; step < steps; ++step)
    {
#pragma omp parallel
        {
#pragma omp for
            for(int y = 0; y < height; ++y)
            {
                convert_row_to_yiq(working_frame, width, y, Y + (y * width), I + (y * width), Q + (y * width));
            }

#pragma omp for
            for(int y = 0; y < height; ++y)
            {
                float *out_i = filtered_i + (y * width);
                float *out_q = filtered_q + (y * width);

                if(y == 0 || y == height - 1)
                {
                    memcpy(out_i, I + (y * width), width * sizeof(float));
                    memcpy(out_q, Q + (y * width), width * sizeof(float));
                }
                else
                {
                    median_chroma_row(
                        width,
                        I + ((y - 1) * width),
                        I + (y * width),
                        I + ((y + 1) * width),
                        Q + ((y - 1) * width),
                        Q + (y * width),
                        Q + ((y + 1) * width),
                        highlight_map ? highlight_map + ((y - 1) * width) : NULL,
                        highlight_map ? highlight_map + (y * width) : NULL,
                        highlight_map ? highlight_map + ((y + 1) * width) : NULL,
                        out_i,
                        out_q);
                }
            }

#pragma omp for
            for(int y = 1; y < height - 1; ++y)
            {
                finalize_false_color_row(
                    working_frame, width, y,
                    Y + ((y - 1) * width),
                    Y + (y * width),
                    Y + ((y + 1) * width),
                    I + (y * width),
                    Q + (y * width),
                    filtered_i + ((y - 1) * width),
                    filtered_i + (y * width),
                    filtered_i + ((y + 1) * width),
                    filtered_q + ((y - 1) * width),
                    filtered_q + (y * width),
                    filtered_q + ((y + 1) * width),
                    highlight_map ? highlight_map + ((y - 1) * width) : NULL,
                    highlight_map ? highlight_map + (y * width) : NULL,
                    highlight_map ? highlight_map + ((y + 1) * width) : NULL,
                    edge_aware,
                    channel_max);
            }
        }
    }

#pragma omp parallel for
    for(size_t pixel = 0; pixel < pixel_count; ++pixel)
    {
        if(highlight_map && (highlight_map[pixel] & DEBAYER_FCS_HIGHLIGHT_PROTECT)) {
            continue;
        }

        size_t offset = pixel * 3;
        frame[offset] = float_to_uint16(working_frame[offset] / normalized_wb[0]);
        frame[offset + 1] = float_to_uint16(working_frame[offset + 1] / normalized_wb[1]);
        frame[offset + 2] = float_to_uint16(working_frame[offset + 2] / normalized_wb[2]);
    }

    free(buffer);
}

void convert_to_log(void * data)
{
    (void)data;
    // float
}

/* AmAZeMEmE debayer easier to use */
void debayerAmaze(uint16_t * __restrict debayerto, float * __restrict bayerdata, int width, int height, int threads, int blacklevel)
{
    int pixelsize = width * height;

    /* AmAZeMEmE wants an image as floating points and 2d arrey as well */
    float ** __restrict imagefloat2d = (float **)malloc(height * sizeof(float *));
    for (int y = 0; y < height; ++y) imagefloat2d[y] = (float *)(bayerdata+(y*width));

    /* AmAZe also wants to return floats, so heres memeory 4 it */
    float  * __restrict red1d = (float *)malloc(pixelsize * sizeof(float));
    float ** __restrict red2d = (float **)malloc(height * sizeof(float *));
    for (int y = 0; y < height; ++y) red2d[y] = (float *)(red1d+(y*width));
    float  * __restrict green1d = (float *)malloc(pixelsize * sizeof(float));
    float ** __restrict green2d = (float **)malloc(height * sizeof(float *));
    for (int y = 0; y < height; ++y) green2d[y] = (float *)(green1d+(y*width));
    float  * __restrict blue1d = (float *)malloc(pixelsize * sizeof(float));
    float ** __restrict blue2d = (float **)malloc(height * sizeof(float *));
    for (int y = 0; y < height; ++y) blue2d[y] = (float *)(blue1d+(y*width));

    /* If threads is < 2 just do a normal amaze */
    if (threads < 2)
    {
        /* run the Amaze */
        demosaic( & (amazeinfo_t) {
                  imagefloat2d,
                  red2d,
                  green2d,
                  blue2d,
                  0, 0, /* crop window for demosaicing */
                  width, height,
                  0,
                  blacklevel} );
    }

    /* Else do multithreading */
    else
    {
        int startchunk_y[threads];
        int endchunk_y[threads];

        /* How big each thread's chunk is, multiple of 2 - or debayer
         * would start on wrong pixel and magenta stripes appear */
        int chunk_height = height / threads;
        chunk_height -= chunk_height % 2;

        /* To small chunk heights bring AMaZE module to crash */
        while( chunk_height <= 32 )
        {
            if( threads <= 1 ) break;
            threads--;
            chunk_height = height / threads;
            chunk_height -= chunk_height % 2;
        }

        /* Calculate chunks of image for each thread */
        for (int thread = 0; thread < threads; ++thread)
        {
            startchunk_y[thread] = chunk_height * thread;
            endchunk_y[thread] = chunk_height * (thread + 1);
        }

        /* Last chunk must reach end of frame */
        endchunk_y[threads-1] = height;

        pthread_t thread_id[threads];
        amazeinfo_t amaze_arguments[threads];

        /* Create amaze pthreads */
        for (int thread = 0; thread < threads; ++thread)
        {
            /* Amaze arguments */
            amaze_arguments[thread] = (amazeinfo_t) {
                imagefloat2d,
                red2d,
                green2d,
                blue2d,
                /* Crop out a part for each thread */
                0, startchunk_y[thread],    /* crop window for demosaicing */
                width, (endchunk_y[thread] - startchunk_y[thread]),
                0,
                blacklevel };

            /* Create pthread! */
            pthread_create( &thread_id[thread], NULL, (void *)&demosaic, (void *)&amaze_arguments[thread] );
        }

        /* let all threads finish */
        for (int thread = 0; thread < threads; ++thread)
        {
            pthread_join( thread_id[thread], NULL );
        }

    }

    //int rgb_pixels = pixelsize * 3;

    /* Giv back as RGB, not separate channels */
    for (int i = 0; i < pixelsize; i++)
    {
        int j = i * 3;
        debayerto[ j ] = float_to_uint16(red1d[i]);
        debayerto[j+1] = float_to_uint16(green1d[i]);
        debayerto[j+2] = float_to_uint16(blue1d[i]);
    }

    free(red1d);
    free(red2d);
    free(green1d);
    free(green2d);
    free(blue1d);
    free(blue2d);
    free(imagefloat2d);
}



/* Quite quick bilinear debayer, floating point sadly; threads argument is unused */
void debayerBasic(uint16_t * __restrict debayerto, float * __restrict bayerdata, int width, int height, int threads)
{
    /* Hide warning */
    (void)threads;

    /* Debayer pixel size(limit with 1 pixel border to avoid seg fault, fix blank pixels L8ter) */
    int pixelsizeDB;
    /* when odd height, do less... */
    if( height % 2 == 0 )
        pixelsizeDB = width * (height - 1); /* How many pixels to go through in debayer (height - 1 to avoid bottom row) */
    else
        pixelsizeDB = width * (height - 2); /* How many pixels to go through in debayer (height - 2 to avoid bottom row) */
    int widthDB = width - 1; /* Debayering width */

    int step = width * 2; /* How many pixels to skip each time(2 rows worth) */
    int nextRowRGB = width * 3; /* Size of a row in colour, so it does not need to be calculated 1000x */

    /* Debayer main chunk, start 1 row in to avoid ze segfault :D */
    #pragma omp parallel for
    for (int Y = width; Y < pixelsizeDB; Y += step)
    {
        for (int x = 1; x < widthDB; x += 2) /* Stepping in rows */
        {
            /* Indexes of bayer pixels:
             *
             * R  G  R  G
             * G (B)(G) B
             * R (G)(R) G
             * G  B  G  B
             *
             * Middle 4 are current pixels we are working on */

            int pix = Y + x; /* Current pixel(RED) */
            int pixm1 = pix - width; /* Pixel of previous row by 1 */
            int pixp1 = pix + width; /* Next row pixel */
            int pixp2 = pixp1 + width; /* Row + 2 */

            /* Bayer pixel indexes */
            int bPix[16] = {
                ( pixm1-1 ), ( pixm1 ), ( pixm1+1 ), ( pixm1+2 ),
                ( pix - 1 ), (  pix  ), ( pix + 1 ), ( pix + 2 ),
                ( pixp1-1 ), ( pixp1 ), ( pixp1+1 ), ( pixp1+2 ),
                ( pixp2-1 ), ( pixp2 ), ( pixp2+1 ), ( pixp2+2 ),
            };

            /* Indexes of our four pixels in RGB(not every colour) */
            int rgbPix[4] = {
                (bPix[5] * 3), (bPix[ 6] * 3),
                (bPix[9] * 3), (bPix[10] * 3)
            };

            /* TOP LEFT pixel (BLUE on bayer) */
            /* Doing top left corner - RED on bayer */
            debayerto[ rgbPix[0] ] = (uint32_t)(
                  bayerdata[ bPix[0] ] + bayerdata[ bPix[ 2] ]
                + bayerdata[ bPix[8] ] + bayerdata[ bPix[10] ]
            ) >> 2;
            /* GREEN */
            debayerto[ rgbPix[0]+1 ] = (uint32_t)(
                  bayerdata[ bPix[1] ] + bayerdata[ bPix[6] ]
                + bayerdata[ bPix[4] ] + bayerdata[ bPix[9] ]
            ) >> 2;
            /* BLUE */
            debayerto[ rgbPix[0]+2 ] = (uint16_t)bayerdata[ bPix[5] ]; /* Just BLUE - no DBAYERING needed */

            /* TOP RIGHT pixel (GREEN on bayer) */
            /* RED */
            debayerto[ rgbPix[1] ] = (uint32_t)(
                bayerdata[ bPix[2] ] + bayerdata[ bPix[10] ]
            ) >> 1;
            /* GREEN */
            debayerto[ rgbPix[1]+1 ] = (uint16_t)bayerdata[ bPix[6] ];
            /* BLUE */
            debayerto[ rgbPix[1]+2 ] = (uint32_t)(
                bayerdata[ bPix[5] ] + bayerdata[ bPix[7] ]
            ) >> 1;

            /* BOTTOM LEFT pixel (GREEN on bayer) */
            /* RED */
            debayerto[ rgbPix[2] ] = (uint32_t)(
                bayerdata[ bPix[8] ] + bayerdata[ bPix[10] ]
            ) >> 1;
            /* GREEN */
            debayerto[ rgbPix[2]+1 ] = (uint16_t)bayerdata[ bPix[9] ];
            /* BLUE */
            debayerto[ rgbPix[2]+2 ] = (uint32_t)(
                bayerdata[ bPix[5] ] + bayerdata[ bPix[13] ]
            ) >> 1;

            /* BOTTOM RIGHT pixel (RED on bayer) */
            /* RED */
            debayerto[ rgbPix[3] ] = (uint16_t)bayerdata[ bPix[10] ];
            /* GREEN */
            debayerto[ rgbPix[3]+1 ] = (uint32_t)(
                  bayerdata[ bPix[ 6] ] + bayerdata[ bPix[ 9] ]
                + bayerdata[ bPix[11] ] + bayerdata[ bPix[14] ]
            ) >> 2;
            /* BLUE */
            debayerto[ rgbPix[3]+2 ] = (uint32_t)(
                  bayerdata[ bPix[ 5] ] + bayerdata[ bPix[ 7] ]
                + bayerdata[ bPix[13] ] + bayerdata[ bPix[15] ]
            ) >> 2;
        }

        /* Fix broken pixels at the edges by copying from the ones next to them */
        uint16_t * edgePixel = debayerto + (3 * Y); /* So we don't need more calculating later */
        /* Now fix them */
        edgePixel[0] = edgePixel[3];
        edgePixel[1] = edgePixel[4];
        edgePixel[2] = edgePixel[5];
        /* Move pointer one row along */
        edgePixel += nextRowRGB;
        /* Fix left pixel */
        edgePixel[0] = edgePixel[3];
        edgePixel[1] = edgePixel[4];
        edgePixel[2] = edgePixel[5];
        /* Fix right pixel (comes just before left 1) */
        edgePixel[-1] = edgePixel[-4];
        edgePixel[-2] = edgePixel[-5];
        edgePixel[-3] = edgePixel[-6];
        /* Move pointer one row along */
        edgePixel += nextRowRGB;
        /* Fix last right pixel */
        edgePixel[-1] = edgePixel[-4];
        edgePixel[-2] = edgePixel[-5];
        edgePixel[-3] = edgePixel[-6];

    }

    /* Copy to top/bottom rows */
    memcpy(debayerto, debayerto + (width * 3), width * 3 * sizeof(uint16_t));
    memcpy(debayerto + (width * (height - 1) * 3), debayerto + (width * (height - 2) * 3), width * 3 * sizeof(uint16_t));
}

/* Simple debayer single thread: one RGB pixel is 2x2 RAW pixels */
void debayerSimpleThread( easydebayerinfo_t * data )
{
    /* single lines can't be handled */
    if( data->height % 2 ) data->height--;

    int start = data->width * data->offsetY;
    int end = data->width * data->height;
    int pixelSkipR = 3 * data->width;
    int pixelSkipB = 3 * data->width - 2;

    for( int i = start, o = start*3; i < end; i++, o+=3 )
    {
        /* copy colors always to the whole 2x2 pixel */
        if( (i % 2) == 0 && ( ( i / data->width ) % 2 ) == 0 ) //R
        {
            data->debayerto[o] = (uint16_t)data->bayerdata[i];
            data->debayerto[o+3] = (uint16_t)data->bayerdata[i]; // +1pixel
            data->debayerto[o+pixelSkipR] = (uint16_t)data->bayerdata[i]; // +1line
            data->debayerto[o+pixelSkipR+3] = (uint16_t)data->bayerdata[i]; // +1line +1pixel
        }
        else if( (i % 2) == 1 && ( ( i / data->width ) % 2 ) == 1 ) //B
        {
            data->debayerto[o+2] = (uint16_t)data->bayerdata[i];
            data->debayerto[o-1] = (uint16_t)data->bayerdata[i]; // -1pixel
            data->debayerto[o-pixelSkipB] = (uint16_t)data->bayerdata[i]; // -1line
            data->debayerto[o-pixelSkipB-3] = (uint16_t)data->bayerdata[i]; // -1line -1pixel
        }
        else //G
        {
            data->debayerto[o+1] = (uint16_t)data->bayerdata[i];
            if( (i % 2) == 1 ) data->debayerto[o-2] = (uint16_t)data->bayerdata[i]; // -1pixel
            else data->debayerto[o+4] = (uint16_t)data->bayerdata[i]; // +1pixel
        }
    }
}

/* no debayer single thread, just copy some bytes to somewhere else :-P */
void debayerNoneThread( easydebayerinfo_t * data )
{
    int start = data->width * data->offsetY;
    int end = data->width * data->height;

    for( int i = start, o = start*3; i < end; i++, o+=3 )
    {
        /* no idea what I do here, but I get a B/W picture */
        data->debayerto[o] = (uint16_t)data->bayerdata[i];
        data->debayerto[o+1] = (uint16_t)data->bayerdata[i];
        data->debayerto[o+2] = (uint16_t)data->bayerdata[i];
    }
}

/* easy debayer types, threaded */
void debayerEasy(uint16_t * __restrict debayerto, float * __restrict bayerdata, int width, int height, int threads, int type)
{
    /* If threads is < 2 just do it normal */
    if (threads < 2)
    {
        if( type == 2 ) debayerNoneThread( & (easydebayerinfo_t) { debayerto, bayerdata, width, height, 0 } );
        else debayerSimpleThread( & (easydebayerinfo_t) { debayerto, bayerdata, width, height, 0 } );
    }
    else
    {
        int startchunk_y[threads];
        int endchunk_y[threads];

        /* How big each thread's chunk is, multiple of 2 - or debayer
         * would start on wrong pixel and magenta stripes appear */
        int chunk_height = height / threads;
        chunk_height -= chunk_height % 2;

        /* Calculate chunks of image for each thread */
        for (int thread = 0; thread < threads; ++thread)
        {
            startchunk_y[thread] = chunk_height * thread;
            endchunk_y[thread] = chunk_height * (thread + 1);
        }

        /* Last chunk must reach end of frame */
        endchunk_y[threads-1] = height;

        pthread_t thread_id[threads];
        easydebayerinfo_t none_arguments[threads];

        /* Create pthreads */
        for (int thread = 0; thread < threads; ++thread)
        {
            /* Amaze arguments */
            none_arguments[thread] = (easydebayerinfo_t) {
                debayerto,
                bayerdata,
                /* Crop out a part for each thread */
                width,
                endchunk_y[thread],
                startchunk_y[thread] };

            /* Create pthread! */
            if( type == 2 ) pthread_create( &thread_id[thread], NULL, (void *)&debayerNoneThread, (void *)&none_arguments[thread] );
            else pthread_create( &thread_id[thread], NULL, (void *)&debayerSimpleThread, (void *)&none_arguments[thread] );
        }

        /* let all threads finish */
        for (int thread = 0; thread < threads; ++thread)
        {
            pthread_join( thread_id[thread], NULL );
        }
    }
}

void debayerLibRtProcess(uint16_t *debayerto, float *bayerdata, int width, int height, int algorithm, double camMatrix[9], int lmmseIterations, int dcbIterations)
{
    int pixelsize = width * height;

    /* lrtp wants an image as floating points and 2d arrey as well */
    float ** __restrict imagefloat2d = (float **)malloc(height * sizeof(float *));
    for (int y = 0; y < height; ++y) imagefloat2d[y] = (float *)(bayerdata+(y*width));

    /* lrtp also wants to return floats, so heres memeory 4 it */
    float  * __restrict red1d = (float *)malloc(pixelsize * sizeof(float));
    float ** __restrict red2d = (float **)malloc(height * sizeof(float *));
    for (int y = 0; y < height; ++y) red2d[y] = (float *)(red1d+(y*width));
    float  * __restrict green1d = (float *)malloc(pixelsize * sizeof(float));
    float ** __restrict green2d = (float **)malloc(height * sizeof(float *));
    for (int y = 0; y < height; ++y) green2d[y] = (float *)(green1d+(y*width));
    float  * __restrict blue1d = (float *)malloc(pixelsize * sizeof(float));
    float ** __restrict blue2d = (float **)malloc(height * sizeof(float *));
    for (int y = 0; y < height; ++y) blue2d[y] = (float *)(blue1d+(y*width));

    if( algorithm == 4)
        lrtpLmmseDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height, lmmseIterations );
    else if( algorithm == 5 )
        lrtpIgvDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height );
    else if( algorithm == 6 )
        lrtpAhdDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height, camMatrix );
    else if( algorithm == 7 )
        lrtpRcdDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height );
    else if( algorithm == 8 )
        lrtpDcbDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height, dcbIterations );
    else //AMaZE
        lrtpAmazeDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height );

    //int rgb_pixels = pixelsize * 3;

    /* Giv back as RGB, not separate channels */
    for (int i = 0; i < pixelsize; i++)
    {
        int j = i * 3;
        debayerto[ j ] = float_to_uint16(red1d[i]);
        debayerto[j+1] = float_to_uint16(green1d[i]);
        debayerto[j+2] = float_to_uint16(blue1d[i]);
    }

    free(red1d);
    free(red2d);
    free(green1d);
    free(green2d);
    free(blue1d);
    free(blue2d);
    free(imagefloat2d);
}
