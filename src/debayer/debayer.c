#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

#include "debayer.h"
#include "librtprocesswrapper.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define LIMIT16(X) MAX(MIN(X, 65535), 0)

#if defined(_MSC_VER)
#define DEBAYER_TLS __declspec(thread)
#else
#define DEBAYER_TLS __thread
#endif

#define DEBAYER_TLS_CAP_BYTES (32 * 1024 * 1024) /* 32 MB cap per plane */

typedef struct {
    float * red1d;
    float * green1d;
    float * blue1d;
    float ** red2d;
    float ** green2d;
    float ** blue2d;
    float ** imagefloat2d;
    size_t allocated_pixels;
    int allocated_height;
} debayer_tls_t;

static DEBAYER_TLS debayer_tls_t g_debayer_tls = {0};

void debayer_cleanup_tls(void)
{
    free(g_debayer_tls.red1d);
    free(g_debayer_tls.green1d);
    free(g_debayer_tls.blue1d);
    free(g_debayer_tls.imagefloat2d);
    free(g_debayer_tls.red2d);
    free(g_debayer_tls.green2d);
    free(g_debayer_tls.blue2d);
    memset(&g_debayer_tls, 0, sizeof(g_debayer_tls));
}

void convert_to_log(void * data)
{
    // float
}

/* AmAZeMEmE debayer easier to use */
void debayerAmaze(uint16_t * __restrict debayerto, float * __restrict bayerdata, int width, int height, int threads, int blacklevel)
{
    int pixelsize = width * height;
    size_t plane_bytes = (size_t)pixelsize * sizeof(float);
    int use_tls = (plane_bytes <= DEBAYER_TLS_CAP_BYTES);

    float * red1d = NULL;
    float * green1d = NULL;
    float * blue1d = NULL;
    float ** imagefloat2d = NULL;
    float ** red2d = NULL;
    float ** green2d = NULL;
    float ** blue2d = NULL;

    if (use_tls)
    {
        if (g_debayer_tls.allocated_pixels < (size_t)pixelsize)
        {
            float * new_r = (float *)realloc(g_debayer_tls.red1d, (size_t)pixelsize * sizeof(float));
            float * new_g = (float *)realloc(g_debayer_tls.green1d, (size_t)pixelsize * sizeof(float));
            float * new_b = (float *)realloc(g_debayer_tls.blue1d, (size_t)pixelsize * sizeof(float));
            if (!new_r || !new_g || !new_b)
            {
                if (new_r) g_debayer_tls.red1d = new_r;
                if (new_g) g_debayer_tls.green1d = new_g;
                if (new_b) g_debayer_tls.blue1d = new_b;
                fprintf(stderr, "MLV App Error: Out of memory allocating debayer TLS float planes (%llu bytes)\n", (unsigned long long)(pixelsize * sizeof(float)));
                return;
            }
            g_debayer_tls.red1d = new_r;
            g_debayer_tls.green1d = new_g;
            g_debayer_tls.blue1d = new_b;
            g_debayer_tls.allocated_pixels = (size_t)pixelsize;
        }

        if (g_debayer_tls.allocated_height < height)
        {
            float ** new_im = (float **)realloc(g_debayer_tls.imagefloat2d, height * sizeof(float *));
            float ** new_r2 = (float **)realloc(g_debayer_tls.red2d, height * sizeof(float *));
            float ** new_g2 = (float **)realloc(g_debayer_tls.green2d, height * sizeof(float *));
            float ** new_b2 = (float **)realloc(g_debayer_tls.blue2d, height * sizeof(float *));
            if (!new_im || !new_r2 || !new_g2 || !new_b2)
            {
                if (new_im) g_debayer_tls.imagefloat2d = new_im;
                if (new_r2) g_debayer_tls.red2d = new_r2;
                if (new_g2) g_debayer_tls.green2d = new_g2;
                if (new_b2) g_debayer_tls.blue2d = new_b2;
                fprintf(stderr, "MLV App Error: Out of memory allocating debayer TLS pointer arrays (%llu bytes)\n", (unsigned long long)(height * sizeof(float *)));
                return;
            }
            g_debayer_tls.imagefloat2d = new_im;
            g_debayer_tls.red2d = new_r2;
            g_debayer_tls.green2d = new_g2;
            g_debayer_tls.blue2d = new_b2;
            g_debayer_tls.allocated_height = height;
        }

        red1d = g_debayer_tls.red1d;
        green1d = g_debayer_tls.green1d;
        blue1d = g_debayer_tls.blue1d;
        imagefloat2d = g_debayer_tls.imagefloat2d;
        red2d = g_debayer_tls.red2d;
        green2d = g_debayer_tls.green2d;
        blue2d = g_debayer_tls.blue2d;
    }
    else
    {
        /* Buffer exceeds 32 MB cap (e.g. 5.4K) - clean up any idle TLS and allocate temporarily */
        debayer_cleanup_tls();

        red1d = (float *)malloc((size_t)pixelsize * sizeof(float));
        green1d = (float *)malloc((size_t)pixelsize * sizeof(float));
        blue1d = (float *)malloc((size_t)pixelsize * sizeof(float));
        imagefloat2d = (float **)malloc(height * sizeof(float *));
        red2d = (float **)malloc(height * sizeof(float *));
        green2d = (float **)malloc(height * sizeof(float *));
        blue2d = (float **)malloc(height * sizeof(float *));

        if (!red1d || !green1d || !blue1d || !imagefloat2d || !red2d || !green2d || !blue2d)
        {
            free(red1d); free(green1d); free(blue1d);
            free(imagefloat2d); free(red2d); free(green2d); free(blue2d);
            fprintf(stderr, "MLV App Error: Out of memory allocating oversize debayer planes\n");
            return;
        }
    }

    for (int y = 0; y < height; ++y)
    {
        imagefloat2d[y] = (float *)(bayerdata + (y * width));
        red2d[y]        = (float *)(red1d + (y * width));
        green2d[y]      = (float *)(green1d + (y * width));
        blue2d[y]       = (float *)(blue1d + (y * width));
    }

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
        if (threads > 64) threads = 64;
        int startchunk_y[64];
        int endchunk_y[64];

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

        if (threads <= 1)
        {
            demosaic( & (amazeinfo_t) {
                      imagefloat2d,
                      red2d,
                      green2d,
                      blue2d,
                      0, 0,
                      width, height,
                      0,
                      blacklevel} );
        }
        else
        {
            /* Calculate chunks of image for each thread */
            for (int thread = 0; thread < threads; ++thread)
            {
                startchunk_y[thread] = chunk_height * thread;
                endchunk_y[thread] = chunk_height * (thread + 1);
            }

            /* Last chunk must reach end of frame */
            endchunk_y[threads-1] = height;

            amazeinfo_t amaze_arguments[64];
            for (int thread = 0; thread < threads; ++thread)
            {
                amaze_arguments[thread] = (amazeinfo_t) {
                    imagefloat2d,
                    red2d,
                    green2d,
                    blue2d,
                    0, startchunk_y[thread],    /* crop window for demosaicing */
                    width, (endchunk_y[thread] - startchunk_y[thread]),
                    0,
                    blacklevel };
            }

#ifdef __APPLE__
            amazeinfo_t * args = amaze_arguments;
            dispatch_apply(threads, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t thread) {
                demosaic(&args[thread]);
            });
#else
            #pragma omp parallel for schedule(static) num_threads(threads)
            for (int thread = 0; thread < threads; ++thread)
            {
                demosaic(&amaze_arguments[thread]);
            }
#endif
        }
    }

    /* Give back as RGB, not separate channels */
#ifdef __APPLE__
    if (threads > 1)
    {
        dispatch_apply(threads, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t t) {
            int chunk = pixelsize / threads;
            int start = (int)t * chunk;
            int end = (t == (size_t)(threads - 1)) ? pixelsize : (start + chunk);
            for (int i = start; i < end; ++i)
            {
                int j = i * 3;
                debayerto[j]     = LIMIT16((uint32_t)red1d[i]);
                debayerto[j + 1] = LIMIT16((uint32_t)green1d[i]);
                debayerto[j + 2] = LIMIT16((uint32_t)blue1d[i]);
            }
        });
    }
    else
#else
    #pragma omp parallel for schedule(static) if(threads > 1) num_threads(threads)
#endif
    {
        for (int i = 0; i < pixelsize; i++)
        {
            int j = i * 3;
            debayerto[ j ] = LIMIT16((uint32_t)red1d[i]);
            debayerto[j+1] = LIMIT16((uint32_t)green1d[i]);
            debayerto[j+2] = LIMIT16((uint32_t)blue1d[i]);
        }
    }

    if (!use_tls)
    {
        free(red1d);
        free(green1d);
        free(blue1d);
        free(imagefloat2d);
        free(red2d);
        free(green2d);
        free(blue2d);
    }
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

void debayerLibRtProcess(uint16_t *debayerto, float *bayerdata, int width, int height, int algorithm, double camMatrix[9])
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
        lrtpLmmseDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height );
    else if( algorithm == 5 )
        lrtpIgvDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height );
    else if( algorithm == 6 )
        lrtpAhdDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height, camMatrix );
    else if( algorithm == 7 )
        lrtpRcdDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height );
    else if( algorithm == 8 )
        lrtpDcbDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height );
    else //AMaZE
        lrtpAmazeDemosaic( imagefloat2d, red2d, green2d, blue2d, width, height );

    //int rgb_pixels = pixelsize * 3;

    /* Giv back as RGB, not separate channels */
    for (int i = 0; i < pixelsize; i++)
    {
        int j = i * 3;
        debayerto[ j ] = LIMIT16((uint32_t)red1d[i]);
        debayerto[j+1] = LIMIT16((uint32_t)green1d[i]);
        debayerto[j+2] = LIMIT16((uint32_t)blue1d[i]);
    }

    free(red1d);
    free(red2d);
    free(green1d);
    free(green2d);
    free(blue1d);
    free(blue2d);
    free(imagefloat2d);
}
