#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "debayer.h"
#include "librtprocesswrapper.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define LIMIT16(X) MAX(MIN(X, 65535), 0)

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

static void convert_row_to_yiq(const uint16_t *frame, int width, int row, float *Y, float *I, float *Q)
{
    const uint16_t *pix = frame + (row * width * 3);

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
    float *out_i,
    float *out_q)
{
    out_i[0] = curr_i[0];
    out_q[0] = curr_q[0];

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

    out_i[width - 1] = curr_i[width - 1];
    out_q[width - 1] = curr_q[width - 1];
}

static void finalize_false_color_row(
    uint16_t *frame,
    int width,
    int row,
    const float *Y,
    const float *prev_i,
    const float *curr_i,
    const float *next_i,
    const float *prev_q,
    const float *curr_q,
    const float *next_q)
{
    uint16_t *pix = frame + (row * width * 3);

    pix[0] = float_to_uint16(Y[0] + 0.956f * curr_i[0] + 0.621f * curr_q[0]);
    pix[1] = float_to_uint16(Y[0] - 0.272f * curr_i[0] - 0.647f * curr_q[0]);
    pix[2] = float_to_uint16(Y[0] - 1.105f * curr_i[0] + 1.702f * curr_q[0]);

    #pragma omp simd
    for(int x = 1; x < width - 1; ++x)
    {
        float out_i = (prev_i[x - 1] + prev_i[x] + prev_i[x + 1]
                     + curr_i[x - 1] + curr_i[x] + curr_i[x + 1]
                     + next_i[x - 1] + next_i[x] + next_i[x + 1]) / 9.0f;
        float out_q = (prev_q[x - 1] + prev_q[x] + prev_q[x + 1]
                     + curr_q[x - 1] + curr_q[x] + curr_q[x + 1]
                     + next_q[x - 1] + next_q[x] + next_q[x + 1]) / 9.0f;
        uint16_t *dst = pix + (x * 3);

        dst[0] = float_to_uint16(Y[x] + 0.956f * out_i + 0.621f * out_q);
        dst[1] = float_to_uint16(Y[x] - 0.272f * out_i - 0.647f * out_q);
        dst[2] = float_to_uint16(Y[x] - 1.105f * out_i + 1.702f * out_q);
    }

    pix += (width - 1) * 3;
    pix[0] = float_to_uint16(Y[width - 1] + 0.956f * curr_i[width - 1] + 0.621f * curr_q[width - 1]);
    pix[1] = float_to_uint16(Y[width - 1] - 0.272f * curr_i[width - 1] - 0.647f * curr_q[width - 1]);
    pix[2] = float_to_uint16(Y[width - 1] - 1.105f * curr_i[width - 1] + 1.702f * curr_q[width - 1]);
}

void debayerFalseColorCorrection(uint16_t *frame, int width, int height, int steps)
{
    if(steps <= 0 || width < 3 || height < 4) {
        return;
    }
    if(steps > 5) {
        steps = 5;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    size_t row_values = (size_t)width * 3;
    size_t frame_values = pixel_count * 3;
    float *buffer = (float *)malloc(pixel_count * 5 * sizeof(float));
    uint16_t *temp_frame = (uint16_t *)malloc(frame_values * sizeof(uint16_t));
    if(!buffer || !temp_frame) {
        free(temp_frame);
        free(buffer);
        return;
    }

    float *Y = buffer;
    float *I = Y + pixel_count;
    float *Q = I + pixel_count;
    float *filtered_i = Q + pixel_count;
    float *filtered_q = filtered_i + pixel_count;

    uint16_t *src = frame;
    uint16_t *dst = temp_frame;

    for(int step = 0; step < steps; ++step)
    {
#pragma omp parallel
        {
#pragma omp for
            for(int y = 0; y < height; ++y)
            {
                convert_row_to_yiq(src, width, y, Y + (y * width), I + (y * width), Q + (y * width));
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
                        out_i,
                        out_q);
                }
            }

#pragma omp for
            for(int y = 1; y < height - 1; ++y)
            {
                finalize_false_color_row(
                    dst, width, y, Y + (y * width),
                    filtered_i + ((y - 1) * width),
                    filtered_i + (y * width),
                    filtered_i + ((y + 1) * width),
                    filtered_q + ((y - 1) * width),
                    filtered_q + (y * width),
                    filtered_q + ((y + 1) * width));
            }
        }

        size_t last_row_offset = (size_t)(height - 1) * row_values;
        memcpy(dst, src, row_values * sizeof(uint16_t));
        memcpy(dst + last_row_offset, src + last_row_offset, row_values * sizeof(uint16_t));

        uint16_t *next_src = dst;
        dst = src;
        src = next_src;
    }

    if(src != frame) {
        memcpy(frame, src, frame_values * sizeof(uint16_t));
    }

    free(temp_frame);
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
