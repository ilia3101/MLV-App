#ifndef _debayer_
#define _debayer_

#include <stdint.h>

/* Easy debayer types */
void debayerEasy(uint16_t * __restrict debayerto, float * __restrict bayerdata, int width, int height, int threads, int type);
/* Quite quick bilinear debayer, floating point sadly; threads argument is unused */
void debayerBasic(uint16_t * __restrict debayerto, float * __restrict bayerdata, int width, int height, int threads);
/* More useable amaze, threads number should be the number of cores(or threads if >= i7) your cpu has */
void debayerAmaze(uint16_t * __restrict debayerto, float * __restrict bayerdata, int width, int height, int threads, int blacklevel);
/* Use LMMSE debayer */
void debayerLmmse(uint16_t * __restrict debayerto, float * __restrict bayerdata, int width, int height, int threads, int blacklevel);
/* More useable IGV */
void debayerIgv(uint16_t * __restrict debayerto, float * __restrict bayerdata, int width, int height, int blacklevel);
/* AHD debayer */
void debayerAhd(uint16_t *__restrict debayerto, float *__restrict bayerdata, int width, int height);

/* None debayer structure for multithread */
typedef struct {
    uint16_t * __restrict debayerto; //output
    float * __restrict bayerdata; //input
    int width; //complete width
    int height; //cropped height to render
    int offsetY; //start of crop for thread in height
} easydebayerinfo_t;

/* AMaZe input as struct for posixz */
typedef struct {
    float ** __restrict rawData;    /* holds preprocessed pixel values, rawData[i][j] corresponds to the ith row and jth column */
    float ** __restrict red;        /* the interpolated red plane */
    float ** __restrict green;      /* the interpolated green plane */
    float ** __restrict blue;       /* the interpolated blue plane */
    int winx; int winy; /* crop window for demosaicing */
    int winw; int winh;
    int cfa;
    int blacklevel;
} amazeinfo_t;

/* RCD input as struct for posixz */
typedef struct {
    float ** __restrict rawData;    /* holds preprocessed pixel values, rawData[i][j] corresponds to the ith row and jth column */
    float ** __restrict red;        /* the interpolated red plane */
    float ** __restrict green;      /* the interpolated green plane */
    float ** __restrict blue;       /* the interpolated blue plane */
    int winw; int winh;
} rcdinfo_t;

/* LMMSE input as struct for posixz */
typedef struct {
    float * __restrict Input;
    float * __restrict Output;
    int winx; int winy; /* crop window for demosaicing */
    int winw; int winh;
    int numOverallPixels;
    int blacklevel;
} lmmseinfo_t;

/* Amaze demosaic */
void
#ifdef __MINGW32__
/* Needed for win32/mingw (might need include gaurd with another compiler) */
__attribute__ ((force_align_arg_pointer))
#endif

/*AMaZE algo*/
demosaic(amazeinfo_t * inputdata);

/* igv algo */
void igv_demosaic(amazeinfo_t * inputdata);

/*RCD algo*/
void rcd_demosaic(rcdinfo_t * inputdata);

#endif
