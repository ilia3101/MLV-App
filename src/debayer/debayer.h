#ifndef _debayer_
#define _debayer_

/* Quite quick bilinear debayer, floating point sadly; threads argument is unused */
void debayerBasic(uint16_t * debayerto, float * bayerdata, int width, int height, int threads);
/* More useable amaze, threads number should be the number of cores(or threads if >= i7) your cpu has */
void debayerAmaze(uint16_t * debayerto, float * bayerdata, int width, int height, int threads);

/* AMaZe input as struct for posixz */
typedef struct {
    float ** rawData;    /* holds preprocessed pixel values, rawData[i][j] corresponds to the ith row and jth column */
    float ** red;        /* the interpolated red plane */
    float ** green;      /* the interpolated green plane */
    float ** blue;       /* the interpolated blue plane */
    int winx; int winy; /* crop window for demosaicing */
    int winw; int winh;
    int cfa;
} amazeinfo_t;

/* Amaze demosaic */
void
#ifdef __MINGW32__
/* Needed for win32/mingw (might need include gaurd with another compiler) */
__attribute__ ((force_align_arg_pointer))
#endif
demosaic(amazeinfo_t * inputdata);

#endif