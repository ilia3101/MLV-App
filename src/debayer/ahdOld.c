/* AHD ported from libdc1394 to MLVApp by masc */
/* I borrowed the code from the libdc1394 project (http://sourceforge.net/projects/libdc1394), it is licensed under LGPL. */
/* AHD interpolation ported from dcraw to libdc1394 by Samuel Audet */
/*
   Adaptive Homogeneity-Directed interpolation is based on
   the work of Keigo Hirakawa, Thomas Parks, and Paul Lee.
 */

#include "stdlib.h"
#include "debayer.h"
#include "string.h"
#include "math.h"

#define TS 256 /* Tile Size */
#define DC1394_FALSE  0
#define DC1394_TRUE   1

#define FORC3 for (c=0; c < 3; c++)
#define SQR(x) ((x)*(x))
#define ABS(x) (((int)(x) ^ ((int)(x) >> 31)) - ((int)(x) >> 31))
#ifndef MIN
  #define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
  #define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#define LIM(x,min,max) MAX(min,MIN(x,max))
#define ULIM(x,y,z) ((y) < (z) ? LIM(x,y,z) : LIM(x,z,y))

static uint8_t ahd_inited = DC1394_FALSE; /* WARNING: not multi-processor safe */

#define CLIPOUT(x)        LIM(x,0,255)
#define CLIPOUT16(x,bits) LIM(x,0,((1<<bits)-1))

#define FC(row,col) \
(filters >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3)

static const double xyz_rgb[3][3] = {                        /* XYZ from RGB */
  { 0.412453, 0.357580, 0.180423 },
  { 0.212671, 0.715160, 0.072169 },
  { 0.019334, 0.119193, 0.950227 } };
static const float d65_white[3] = { 0.950456, 1, 1.088754 };

static void cam_to_cielab (uint16_t cam[3], float lab[3]) /* [SA] */
{
    int c, i, j;
    float r, xyz[3];
    static float cbrt[0x10000], xyz_cam[3][4];

    if (cam == NULL) {
        for (i=0; i < 0x10000; i++) {
            r = i / 65535.0;
            cbrt[i] = r > 0.008856 ? pow(r,1/3.0) : 7.787*r + 16/116.0;
        }
        for (i=0; i < 3; i++)
            for (j=0; j < 3; j++)                           /* [SA] */
                xyz_cam[i][j] = xyz_rgb[i][j] / d65_white[i]; /* [SA] */
    } else {
        xyz[0] = xyz[1] = xyz[2] = 0.5;
        FORC3 { /* [SA] */
            xyz[0] += xyz_cam[0][c] * cam[c];
            xyz[1] += xyz_cam[1][c] * cam[c];
            xyz[2] += xyz_cam[2][c] * cam[c];
        }
        xyz[0] = cbrt[CLIPOUT16((int) xyz[0],16)];        /* [SA] */
        xyz[1] = cbrt[CLIPOUT16((int) xyz[1],16)];        /* [SA] */
        xyz[2] = cbrt[CLIPOUT16((int) xyz[2],16)];        /* [SA] */
        lab[0] = 116 * xyz[1] - 16;
        lab[1] = 500 * (xyz[0] - xyz[1]);
        lab[2] = 200 * (xyz[1] - xyz[2]);
    }
}

void debayerAhd(uint16_t *__restrict debayerto, float *__restrict bayerdata, int width, int height)
{
    int bits = 16;
    int i, j, top, left, row, col, tr, tc, fc, c, d, val, hm[2];
    /* the following has the same type as the image */
    uint16_t (*pix)[3], (*rix)[3];      /* [SA] */
    static const int dir[4] = { -1, 1, -TS, TS };
    unsigned ldiff[2][4], abdiff[2][4], leps, abeps;
    float flab[3];
    uint16_t (*rgb)[TS][TS][3];         /* [SA] */
    short (*lab)[TS][TS][3];
    char (*homo)[TS][TS], *buffer;

    /* start - new code for libdc1394 */
    uint32_t filters;
    int x, y;

    if (ahd_inited==DC1394_FALSE) {
        /* WARNING: this might not be multi-processor safe */
        cam_to_cielab (NULL,NULL);
        ahd_inited = DC1394_TRUE;
    }

    filters = 0x94949494;

    /* fill-in destination with known exact values */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int channel = FC(y,x);
            debayerto[(y*width+x)*3 + channel] = bayerdata[y*width+x];
        }
    }
    /* end - new code for libdc1394 */

    /* start - code from border_interpolate(int border) */
    {
        int border = 3;
        unsigned y, x, f, c, sum[8];

        for (row=0; row < height; row++)
            for (col=0; col < width; col++) {
                if (col==border && row >= border && row < height-border)
        col = width-border;
                memset (sum, 0, sizeof sum);
                for (y=row-1; y != (unsigned)row+2; y++)
                    for (x=col-1; x != (unsigned)col+2; x++)
                        if (y < (unsigned)height && x < (unsigned)width) {
                            f = FC(y,x);
                            sum[f] += debayerto[(y*width+x)*3 + f];           /* [SA] */
                            sum[f+4]++;
                        }
                f = FC(row,col);
                FORC3 if (c != f && sum[c+4])                     /* [SA] */
                    debayerto[(row*width+col)*3 + c] = sum[c] / sum[c+4]; /* [SA] */
            }
    }
    /* end - code from border_interpolate(int border) */


    buffer = (char *) malloc (26*TS*TS);                /* 1664 kB */
    /* merror (buffer, "ahd_interpolate()"); */
    rgb  = (uint16_t(*)[TS][TS][3]) buffer;               /* [SA] */
    lab  = (short (*)[TS][TS][3])(buffer + 12*TS*TS);
    homo = (char  (*)[TS][TS])   (buffer + 24*TS*TS);

    for (top=0; top < height; top += TS-6)
        for (left=0; left < width; left += TS-6) {
            memset (rgb, 0, 12*TS*TS);

            /*  Interpolate green horizontally and vertically:                */
            for (row = top < 2 ? 2:top; row < top+TS && row < height-2; row++) {
                col = left + (FC(row,left) == 1);
                if (col < 2) col += 2;
                for (fc = FC(row,col); col < left+TS && col < width-2; col+=2) {
                    pix = (uint16_t (*)[3])debayerto + (row*width+col);          /* [SA] */
                    val = ((pix[-1][1] + pix[0][fc] + pix[1][1]) * 2
                           - pix[-2][fc] - pix[2][fc]) >> 2;
                    rgb[0][row-top][col-left][1] = ULIM(val,pix[-1][1],pix[1][1]);
                    val = ((pix[-width][1] + pix[0][fc] + pix[width][1]) * 2
                           - pix[-2*width][fc] - pix[2*width][fc]) >> 2;
                    rgb[1][row-top][col-left][1] = ULIM(val,pix[-width][1],pix[width][1]);
                }
            }
            /*  Interpolate red and blue, and convert to CIELab:                */
            for (d=0; d < 2; d++)
                for (row=top+1; row < top+TS-1 && row < height-1; row++)
                    for (col=left+1; col < left+TS-1 && col < width-1; col++) {
                        pix = (uint16_t (*)[3])debayerto + (row*width+col);        /* [SA] */
                        rix = &rgb[d][row-top][col-left];
                        if ((c = 2 - FC(row,col)) == 1) {
                            c = FC(row+1,col);
                            val = pix[0][1] + (( pix[-1][2-c] + pix[1][2-c]
                                                 - rix[-1][1] - rix[1][1] ) >> 1);
                            rix[0][2-c] = CLIPOUT16(val, bits); /* [SA] */
                            val = pix[0][1] + (( pix[-width][c] + pix[width][c]
                                                 - rix[-TS][1] - rix[TS][1] ) >> 1);
                        } else
                            val = rix[0][1] + (( pix[-width-1][c] + pix[-width+1][c]
                                                 + pix[+width-1][c] + pix[+width+1][c]
                                                 - rix[-TS-1][1] - rix[-TS+1][1]
                                                 - rix[+TS-1][1] - rix[+TS+1][1] + 1) >> 2);
                        rix[0][c] = CLIPOUT16(val, bits);     /* [SA] */
                        c = FC(row,col);
                        rix[0][c] = pix[0][c];
                        cam_to_cielab (rix[0], flab);
                        FORC3 lab[d][row-top][col-left][c] = 64*flab[c];
                    }
            /*  Build homogeneity maps from the CIELab images:                */
            memset (homo, 0, 2*TS*TS);
            for (row=top+2; row < top+TS-2 && row < height; row++) {
                tr = row-top;
                for (col=left+2; col < left+TS-2 && col < width; col++) {
                    tc = col-left;
                    for (d=0; d < 2; d++)
                        for (i=0; i < 4; i++)
                            ldiff[d][i] = ABS(lab[d][tr][tc][0]-lab[d][tr][tc+dir[i]][0]);
                    leps = MIN(MAX(ldiff[0][0],ldiff[0][1]),
                               MAX(ldiff[1][2],ldiff[1][3]));
                    for (d=0; d < 2; d++)
                        for (i=0; i < 4; i++)
                            if (i >> 1 == d || ldiff[d][i] <= leps)
                                abdiff[d][i] = SQR(lab[d][tr][tc][1]-lab[d][tr][tc+dir[i]][1])
                                    + SQR(lab[d][tr][tc][2]-lab[d][tr][tc+dir[i]][2]);
                    abeps = MIN(MAX(abdiff[0][0],abdiff[0][1]),
                                MAX(abdiff[1][2],abdiff[1][3]));
                    for (d=0; d < 2; d++)
                        for (i=0; i < 4; i++)
                            if (ldiff[d][i] <= leps && abdiff[d][i] <= abeps)
                                homo[d][tr][tc]++;
                }
            }
            /*  Combine the most homogenous pixels for the final result:        */
            for (row=top+3; row < top+TS-3 && row < height-3; row++) {
                tr = row-top;
                for (col=left+3; col < left+TS-3 && col < width-3; col++) {
                    tc = col-left;
                    for (d=0; d < 2; d++)
                        for (hm[d]=0, i=tr-1; i <= tr+1; i++)
                            for (j=tc-1; j <= tc+1; j++)
                                hm[d] += homo[d][i][j];
                    if (hm[0] != hm[1])
                        FORC3 debayerto[(row*width+col)*3 + c] = CLIPOUT16(rgb[hm[1] > hm[0]][tr][tc][c], bits); /* [SA] */
                    else
                        FORC3 debayerto[(row*width+col)*3 + c] =
                            CLIPOUT16((rgb[0][tr][tc][c] + rgb[1][tr][tc][c]) >> 1, bits); /* [SA] */
                }
            }
        }
    free (buffer);

    return;
}

