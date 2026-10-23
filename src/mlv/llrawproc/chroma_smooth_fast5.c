#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#if defined(__APPLE__) && !defined(DISO_SINGLE_THREAD)
#include <dispatch/dispatch.h>
#endif

#ifndef CHROMA_SMOOTH_TYPE
#define CHROMA_SMOOTH_TYPE uint16_t
#endif

typedef int v4i __attribute__((vector_size(16)));

#if defined(__clang__) && __has_builtin(__builtin_elementwise_min) && __has_builtin(__builtin_elementwise_max)
#define VSORT(a,b) { v4i _t = __builtin_elementwise_min((a),(b)); (b) = __builtin_elementwise_max((a),(b)); (a) = _t; }
#else
#define VSORT(a,b) { v4i _m = ((a) < (b)); v4i _min = ((a) & _m) | ((b) & ~_m); v4i _max = ((b) & _m) | ((a) & ~_m); (a) = _min; (b) = _max; }
#endif

static inline v4i vmed5(v4i * p)
{
    VSORT(p[0], p[1]); VSORT(p[3], p[4]); VSORT(p[0], p[3]);
    VSORT(p[1], p[4]); VSORT(p[1], p[2]); VSORT(p[2], p[3]);
    VSORT(p[1], p[2]);
    return p[2];
}

static inline v4i vmed9(v4i * p)
{
    VSORT(p[1], p[2]); VSORT(p[4], p[5]); VSORT(p[7], p[8]);
    VSORT(p[0], p[1]); VSORT(p[3], p[4]); VSORT(p[6], p[7]);
    VSORT(p[1], p[2]); VSORT(p[4], p[5]); VSORT(p[7], p[8]);
    VSORT(p[0], p[3]); VSORT(p[5], p[8]); VSORT(p[4], p[7]);
    VSORT(p[3], p[6]); VSORT(p[1], p[4]); VSORT(p[2], p[5]);
    VSORT(p[4], p[7]); VSORT(p[4], p[2]); VSORT(p[6], p[4]);
    VSORT(p[4], p[2]);
    return p[4];
}

static inline v4i vmed25(v4i * p)
{
    VSORT(p[0], p[1]);   VSORT(p[3], p[4]);   VSORT(p[2], p[4]);   VSORT(p[2], p[3]);
    VSORT(p[6], p[7]);   VSORT(p[5], p[7]);   VSORT(p[5], p[6]);   VSORT(p[9], p[10]);
    VSORT(p[8], p[10]);  VSORT(p[8], p[9]);   VSORT(p[12], p[13]); VSORT(p[11], p[13]);
    VSORT(p[11], p[12]); VSORT(p[15], p[16]); VSORT(p[14], p[16]); VSORT(p[14], p[15]);
    VSORT(p[18], p[19]); VSORT(p[17], p[19]); VSORT(p[17], p[18]); VSORT(p[21], p[22]);
    VSORT(p[20], p[22]); VSORT(p[20], p[21]); VSORT(p[23], p[24]); VSORT(p[2], p[5]);
    VSORT(p[3], p[6]);   VSORT(p[0], p[6]);   VSORT(p[0], p[3]);   VSORT(p[4], p[7]);
    VSORT(p[1], p[7]);   VSORT(p[1], p[4]);   VSORT(p[11], p[14]); VSORT(p[8], p[14]);
    VSORT(p[8], p[11]);  VSORT(p[12], p[15]); VSORT(p[9], p[15]);  VSORT(p[9], p[12]);
    VSORT(p[13], p[16]); VSORT(p[10], p[16]); VSORT(p[10], p[13]); VSORT(p[20], p[23]);
    VSORT(p[17], p[23]); VSORT(p[17], p[20]); VSORT(p[21], p[24]); VSORT(p[18], p[24]);
    VSORT(p[18], p[21]); VSORT(p[19], p[22]); VSORT(p[8], p[17]);  VSORT(p[9], p[18]);
    VSORT(p[0], p[18]);  VSORT(p[0], p[9]);   VSORT(p[10], p[19]); VSORT(p[1], p[19]);
    VSORT(p[1], p[10]);  VSORT(p[11], p[20]); VSORT(p[2], p[20]);  VSORT(p[2], p[11]);
    VSORT(p[12], p[21]); VSORT(p[3], p[21]);  VSORT(p[3], p[12]);  VSORT(p[13], p[22]);
    VSORT(p[4], p[22]);  VSORT(p[4], p[13]);  VSORT(p[14], p[23]); VSORT(p[5], p[23]);
    VSORT(p[5], p[14]);  VSORT(p[15], p[24]); VSORT(p[6], p[24]);  VSORT(p[6], p[15]);
    VSORT(p[7], p[16]);  VSORT(p[7], p[19]);  VSORT(p[13], p[21]); VSORT(p[15], p[23]);
    VSORT(p[7], p[13]);  VSORT(p[7], p[15]);  VSORT(p[1], p[9]);   VSORT(p[3], p[11]);
    VSORT(p[5], p[17]);  VSORT(p[11], p[17]); VSORT(p[9], p[17]);  VSORT(p[4], p[10]);
    VSORT(p[6], p[12]);  VSORT(p[7], p[14]);  VSORT(p[4], p[6]);   VSORT(p[4], p[7]);
    VSORT(p[12], p[14]); VSORT(p[10], p[14]); VSORT(p[6], p[7]);   VSORT(p[10], p[12]);
    VSORT(p[6], p[10]);  VSORT(p[6], p[17]);  VSORT(p[12], p[17]); VSORT(p[7], p[17]);
    VSORT(p[7], p[10]);  VSORT(p[12], p[18]); VSORT(p[7], p[12]);  VSORT(p[10], p[18]);
    VSORT(p[12], p[20]); VSORT(p[10], p[20]); VSORT(p[10], p[12]);
    return p[12];
}

static inline void cs_finish_cell(int w, CHROMA_SMOOTH_TYPE * inp, CHROMA_SMOOTH_TYPE * out,
                                  int * raw2ev, int * ev2raw, int black, int white,
                                  int x, int y, int drh, int dbh, int drv, int dbv, int eh, int ev)
{
    int g1 = raw2ev[inp[x+1 +     y * w]];
    int g2 = raw2ev[inp[x   + (y+1) * w]];
    int g3 = raw2ev[inp[x-1 +     y * w]];
    int g4 = raw2ev[inp[x   + (y-1) * w]];
    int g5 = raw2ev[inp[x+2 + (y+1) * w]];
    int g6 = raw2ev[inp[x+1 + (y+2) * w]];
    int grv = (g2+g4)/2;
    int grh = (g1+g3)/2;
    int gbv = (g1+g6)/2;
    int gbh = (g2+g5)/2;
    int gr = ev < eh ? grv : grh;
    int gb = ev < eh ? gbv : gbh;
    int dr = ev < eh ? drv : drh;
    int db = ev < eh ? dbv : dbh;
    int r0 = inp[x   +     y * w];
    int b0 = inp[x+1 + (y+1) * w];
    int thr = 64;
    if (r0 < black+thr || b0 < black+thr || ABS(drv - drh) < thr || ABS(grv-grh) < thr || ABS(gbv-gbh) < thr)
    {
        dr = (drv+drh)/2;
        db = (dbv+dbh)/2;
        gr = (g1+g2+g3+g4)/4;
        gb = (g1+g2+g5+g6)/4;
    }
    if (out[x   +     y * w] < (unsigned int)white)
        out[x   +     y * w] = ev2raw[COERCE(gr + dr, -10*EV_RESOLUTION, 14*EV_RESOLUTION-1)];
    if (out[x+1 + (y+1) * w] < (unsigned int)white)
        out[x+1 + (y+1) * w] = ev2raw[COERCE(gb + db, -10*EV_RESOLUTION, 14*EV_RESOLUTION-1)];
}

enum { CS_STRIP_H = 32 };

static void cs_process_strip_2x2(int iy0, int iy_end, int nx, int w, CHROMA_SMOOTH_TYPE * inp, CHROMA_SMOOTH_TYPE * out,
                                 int * raw2ev, int * ev2raw, int black, int white, int * planes)
{
    int sh = iy_end - iy0;
    int PW = nx + 2, PH = sh + 2;
    size_t pn = (size_t)PW * PH;
    int * PRH = planes, * PBH = planes + pn, * PRV = planes + 2*pn;
    int * PBV = planes + 3*pn, * EHP = planes + 4*pn, * EVP = planes + 5*pn;

    for (int pcy = 0; pcy < PH; pcy++)
    {
        int Y = 2 + 2 * (iy0 + pcy);
        for (int pcx = 0; pcx < PW; pcx++)
        {
            int X = 2 * (pcx + 1);
            int r  = inp[X   +     Y * w];
            int b  = inp[X+1 + (Y+1) * w];
            int g1 = raw2ev[inp[X+1 +     Y * w]];
            int g2 = raw2ev[inp[X   + (Y+1) * w]];
            int g3 = raw2ev[inp[X-1 +     Y * w]];
            int g4 = raw2ev[inp[X   + (Y-1) * w]];
            int g5 = raw2ev[inp[X+2 + (Y+1) * w]];
            int g6 = raw2ev[inp[X+1 + (Y+2) * w]];
            int er = raw2ev[r], eb = raw2ev[b];
            size_t o = (size_t)pcy * PW + pcx;
            PRH[o] = er - (g1+g3)/2;   PBH[o] = eb - (g2+g5)/2;   EHP[o] = ABS(g1-g3) + ABS(g2-g5);
            PRV[o] = er - (g2+g4)/2;   PBV[o] = eb - (g1+g6)/2;   EVP[o] = ABS(g2-g4) + ABS(g1-g6);
        }
    }

    for (int sy = 0; sy < sh; sy++)
    {
        int y = 4 + 2 * (iy0 + sy);
        int ix = 0;
        for (; ix + 4 <= nx; ix += 4)
        {
            v4i p[5];
            int drh[4], dbh[4], drv[4], dbv[4], eh[4], ev[4];
            v4i eh0, eh1, eh2, eh3, eh4, ev0, ev1, ev2, ev3, ev4;
            __builtin_memcpy(&eh0, EHP + (size_t)(sy+0)*PW + ix + 1, 16);
            __builtin_memcpy(&eh1, EHP + (size_t)(sy+1)*PW + ix + 0, 16);
            __builtin_memcpy(&eh2, EHP + (size_t)(sy+1)*PW + ix + 1, 16);
            __builtin_memcpy(&eh3, EHP + (size_t)(sy+1)*PW + ix + 2, 16);
            __builtin_memcpy(&eh4, EHP + (size_t)(sy+2)*PW + ix + 1, 16);
            v4i sum_eh = eh0 + eh1 + eh2 + eh3 + eh4;

            __builtin_memcpy(&ev0, EVP + (size_t)(sy+0)*PW + ix + 1, 16);
            __builtin_memcpy(&ev1, EVP + (size_t)(sy+1)*PW + ix + 0, 16);
            __builtin_memcpy(&ev2, EVP + (size_t)(sy+1)*PW + ix + 1, 16);
            __builtin_memcpy(&ev3, EVP + (size_t)(sy+1)*PW + ix + 2, 16);
            __builtin_memcpy(&ev4, EVP + (size_t)(sy+2)*PW + ix + 1, 16);
            v4i sum_ev = ev0 + ev1 + ev2 + ev3 + ev4;

            __builtin_memcpy(&p[0], PRH + (size_t)(sy+0)*PW + ix + 1, 16);
            __builtin_memcpy(&p[1], PRH + (size_t)(sy+1)*PW + ix + 0, 16);
            __builtin_memcpy(&p[2], PRH + (size_t)(sy+1)*PW + ix + 1, 16);
            __builtin_memcpy(&p[3], PRH + (size_t)(sy+1)*PW + ix + 2, 16);
            __builtin_memcpy(&p[4], PRH + (size_t)(sy+2)*PW + ix + 1, 16);
            { v4i m = vmed5(p); __builtin_memcpy(drh, &m, 16); }

            __builtin_memcpy(&p[0], PBH + (size_t)(sy+0)*PW + ix + 1, 16);
            __builtin_memcpy(&p[1], PBH + (size_t)(sy+1)*PW + ix + 0, 16);
            __builtin_memcpy(&p[2], PBH + (size_t)(sy+1)*PW + ix + 1, 16);
            __builtin_memcpy(&p[3], PBH + (size_t)(sy+1)*PW + ix + 2, 16);
            __builtin_memcpy(&p[4], PBH + (size_t)(sy+2)*PW + ix + 1, 16);
            { v4i m = vmed5(p); __builtin_memcpy(dbh, &m, 16); }

            __builtin_memcpy(&p[0], PRV + (size_t)(sy+0)*PW + ix + 1, 16);
            __builtin_memcpy(&p[1], PRV + (size_t)(sy+1)*PW + ix + 0, 16);
            __builtin_memcpy(&p[2], PRV + (size_t)(sy+1)*PW + ix + 1, 16);
            __builtin_memcpy(&p[3], PRV + (size_t)(sy+1)*PW + ix + 2, 16);
            __builtin_memcpy(&p[4], PRV + (size_t)(sy+2)*PW + ix + 1, 16);
            { v4i m = vmed5(p); __builtin_memcpy(drv, &m, 16); }

            __builtin_memcpy(&p[0], PBV + (size_t)(sy+0)*PW + ix + 1, 16);
            __builtin_memcpy(&p[1], PBV + (size_t)(sy+1)*PW + ix + 0, 16);
            __builtin_memcpy(&p[2], PBV + (size_t)(sy+1)*PW + ix + 1, 16);
            __builtin_memcpy(&p[3], PBV + (size_t)(sy+1)*PW + ix + 2, 16);
            __builtin_memcpy(&p[4], PBV + (size_t)(sy+2)*PW + ix + 1, 16);
            { v4i m = vmed5(p); __builtin_memcpy(dbv, &m, 16); }

            __builtin_memcpy(eh, &sum_eh, 16);
            __builtin_memcpy(ev, &sum_ev, 16);

            for (int l = 0; l < 4; l++)
                cs_finish_cell(w, inp, out, raw2ev, ev2raw, black, white, 4 + 2*(ix+l), y, drh[l], dbh[l], drv[l], dbv[l], eh[l], ev[l]);
        }
        for (; ix < nx; ix++)
        {
            int a[5], b2[5], c[5], d[5];
            size_t o[5] = {
                (size_t)(sy+0)*PW + ix + 1, (size_t)(sy+1)*PW + ix + 0, (size_t)(sy+1)*PW + ix + 1,
                (size_t)(sy+1)*PW + ix + 2, (size_t)(sy+2)*PW + ix + 1
            };
            int eh = 0, ev = 0;
            for (int k = 0; k < 5; k++)
            {
                a[k]  = PRH[o[k]]; b2[k] = PBH[o[k]]; c[k]  = PRV[o[k]]; d[k]  = PBV[o[k]];
                eh   += EHP[o[k]]; ev   += EVP[o[k]];
            }
            cs_finish_cell(w, inp, out, raw2ev, ev2raw, black, white, 4 + 2*ix, y, opt_med5(a), opt_med5(b2), opt_med5(c), opt_med5(d), eh, ev);
        }
    }
}

static void cs_process_strip_3x3(int iy0, int iy_end, int nx, int w, CHROMA_SMOOTH_TYPE * inp, CHROMA_SMOOTH_TYPE * out,
                                 int * raw2ev, int * ev2raw, int black, int white, int * planes)
{
    int sh = iy_end - iy0;
    int PW = nx + 2, PH = sh + 2;
    size_t pn = (size_t)PW * PH;
    int * PRH = planes, * PBH = planes + pn, * PRV = planes + 2*pn;
    int * PBV = planes + 3*pn, * EHP = planes + 4*pn, * EVP = planes + 5*pn;

    for (int pcy = 0; pcy < PH; pcy++)
    {
        int Y = 2 + 2 * (iy0 + pcy);
        for (int pcx = 0; pcx < PW; pcx++)
        {
            int X = 2 * (pcx + 1);
            int r  = inp[X   +     Y * w];
            int b  = inp[X+1 + (Y+1) * w];
            int g1 = raw2ev[inp[X+1 +     Y * w]];
            int g2 = raw2ev[inp[X   + (Y+1) * w]];
            int g3 = raw2ev[inp[X-1 +     Y * w]];
            int g4 = raw2ev[inp[X   + (Y-1) * w]];
            int g5 = raw2ev[inp[X+2 + (Y+1) * w]];
            int g6 = raw2ev[inp[X+1 + (Y+2) * w]];
            int er = raw2ev[r], eb = raw2ev[b];
            size_t o = (size_t)pcy * PW + pcx;
            PRH[o] = er - (g1+g3)/2;   PBH[o] = eb - (g2+g5)/2;   EHP[o] = ABS(g1-g3) + ABS(g2-g5);
            PRV[o] = er - (g2+g4)/2;   PBV[o] = eb - (g1+g6)/2;   EVP[o] = ABS(g2-g4) + ABS(g1-g6);
        }
    }

    for (int sy = 0; sy < sh; sy++)
    {
        int y = 4 + 2 * (iy0 + sy);
        int ix = 0;
        for (; ix + 4 <= nx; ix += 4)
        {
            v4i p[9];
            int drh[4], dbh[4], drv[4], dbv[4], eh[4], ev[4];
            v4i sum_eh = {0,0,0,0}, sum_ev = {0,0,0,0};
            for (int dj = 0; dj < 3; dj++) for (int di = 0; di < 3; di++) {
                v4i t; size_t o = (size_t)(sy + dj) * PW + ix + di;
                __builtin_memcpy(&t, EHP + o, 16); sum_eh += t;
                __builtin_memcpy(&t, EVP + o, 16); sum_ev += t;
            }
            for (int dj = 0; dj < 3; dj++) for (int di = 0; di < 3; di++)
                __builtin_memcpy(&p[dj*3+di], PRH + (size_t)(sy+dj)*PW + ix + di, 16);
            { v4i m = vmed9(p); __builtin_memcpy(drh, &m, 16); }
            for (int dj = 0; dj < 3; dj++) for (int di = 0; di < 3; di++)
                __builtin_memcpy(&p[dj*3+di], PBH + (size_t)(sy+dj)*PW + ix + di, 16);
            { v4i m = vmed9(p); __builtin_memcpy(dbh, &m, 16); }
            for (int dj = 0; dj < 3; dj++) for (int di = 0; di < 3; di++)
                __builtin_memcpy(&p[dj*3+di], PRV + (size_t)(sy+dj)*PW + ix + di, 16);
            { v4i m = vmed9(p); __builtin_memcpy(drv, &m, 16); }
            for (int dj = 0; dj < 3; dj++) for (int di = 0; di < 3; di++)
                __builtin_memcpy(&p[dj*3+di], PBV + (size_t)(sy+dj)*PW + ix + di, 16);
            { v4i m = vmed9(p); __builtin_memcpy(dbv, &m, 16); }
            __builtin_memcpy(eh, &sum_eh, 16); __builtin_memcpy(ev, &sum_ev, 16);
            for (int l = 0; l < 4; l++)
                cs_finish_cell(w, inp, out, raw2ev, ev2raw, black, white, 4 + 2*(ix+l), y, drh[l], dbh[l], drv[l], dbv[l], eh[l], ev[l]);
        }
        for (; ix < nx; ix++)
        {
            int a[9], b2[9], c[9], d[9]; int eh = 0, ev = 0, k = 0;
            for (int dj = 0; dj < 3; dj++) for (int di = 0; di < 3; di++) {
                size_t o = (size_t)(sy + dj) * PW + ix + di;
                a[k] = PRH[o]; b2[k] = PBH[o]; c[k] = PRV[o]; d[k] = PBV[o]; eh += EHP[o]; ev += EVP[o]; k++;
            }
            cs_finish_cell(w, inp, out, raw2ev, ev2raw, black, white, 4 + 2*ix, y, opt_med9(a), opt_med9(b2), opt_med9(c), opt_med9(d), eh, ev);
        }
    }
}

static void cs_process_strip_5x5(int iy0, int iy_end, int nx, int w, CHROMA_SMOOTH_TYPE * inp, CHROMA_SMOOTH_TYPE * out,
                                 int * raw2ev, int * ev2raw, int black, int white, int * planes)
{
    int sh = iy_end - iy0;
    int PW = nx + 4, PH = sh + 4;
    size_t pn = (size_t)PW * PH;
    int * PRH = planes, * PBH = planes + pn, * PRV = planes + 2*pn;
    int * PBV = planes + 3*pn, * EHP = planes + 4*pn, * EVP = planes + 5*pn;

    for (int pcy = 0; pcy < PH; pcy++)
    {
        int Y = 2 + 2 * (iy0 + pcy);
        for (int pcx = 0; pcx < PW; pcx++)
        {
            int X = 2 * (pcx + 1);
            int r  = inp[X   +     Y * w];
            int b  = inp[X+1 + (Y+1) * w];
            int g1 = raw2ev[inp[X+1 +     Y * w]];
            int g2 = raw2ev[inp[X   + (Y+1) * w]];
            int g3 = raw2ev[inp[X-1 +     Y * w]];
            int g4 = raw2ev[inp[X   + (Y-1) * w]];
            int g5 = raw2ev[inp[X+2 + (Y+1) * w]];
            int g6 = raw2ev[inp[X+1 + (Y+2) * w]];
            int er = raw2ev[r], eb = raw2ev[b];
            size_t o = (size_t)pcy * PW + pcx;
            PRH[o] = er - (g1+g3)/2;   PBH[o] = eb - (g2+g5)/2;   EHP[o] = ABS(g1-g3) + ABS(g2-g5);
            PRV[o] = er - (g2+g4)/2;   PBV[o] = eb - (g1+g6)/2;   EVP[o] = ABS(g2-g4) + ABS(g1-g6);
        }
    }

    for (int sy = 0; sy < sh; sy++)
    {
        int y = 6 + 2 * (iy0 + sy);
        int ix = 0;
        for (; ix + 4 <= nx; ix += 4)
        {
            v4i p[25];
            int drh[4], dbh[4], drv[4], dbv[4], eh[4], ev[4];
            v4i sum_eh = {0,0,0,0}, sum_ev = {0,0,0,0};
            for (int dj = 0; dj < 5; dj++) for (int di = 0; di < 5; di++) {
                v4i t; size_t o = (size_t)(sy + dj) * PW + ix + di;
                __builtin_memcpy(&t, EHP + o, 16); sum_eh += t;
                __builtin_memcpy(&t, EVP + o, 16); sum_ev += t;
            }
            for (int dj = 0; dj < 5; dj++) for (int di = 0; di < 5; di++)
                __builtin_memcpy(&p[dj*5+di], PRH + (size_t)(sy+dj)*PW + ix + di, 16);
            { v4i m = vmed25(p); __builtin_memcpy(drh, &m, 16); }
            for (int dj = 0; dj < 5; dj++) for (int di = 0; di < 5; di++)
                __builtin_memcpy(&p[dj*5+di], PBH + (size_t)(sy+dj)*PW + ix + di, 16);
            { v4i m = vmed25(p); __builtin_memcpy(dbh, &m, 16); }
            for (int dj = 0; dj < 5; dj++) for (int di = 0; di < 5; di++)
                __builtin_memcpy(&p[dj*5+di], PRV + (size_t)(sy+dj)*PW + ix + di, 16);
            { v4i m = vmed25(p); __builtin_memcpy(drv, &m, 16); }
            for (int dj = 0; dj < 5; dj++) for (int di = 0; di < 5; di++)
                __builtin_memcpy(&p[dj*5+di], PBV + (size_t)(sy+dj)*PW + ix + di, 16);
            { v4i m = vmed25(p); __builtin_memcpy(dbv, &m, 16); }
            __builtin_memcpy(eh, &sum_eh, 16); __builtin_memcpy(ev, &sum_ev, 16);
            for (int l = 0; l < 4; l++)
                cs_finish_cell(w, inp, out, raw2ev, ev2raw, black, white, 6 + 2*(ix+l), y, drh[l], dbh[l], drv[l], dbv[l], eh[l], ev[l]);
        }
        for (; ix < nx; ix++)
        {
            int a[25], b2[25], c[25], d[25]; int eh = 0, ev = 0, k = 0;
            for (int dj = 0; dj < 5; dj++) for (int di = 0; di < 5; di++) {
                size_t o = (size_t)(sy + dj) * PW + ix + di;
                a[k] = PRH[o]; b2[k] = PBH[o]; c[k] = PRV[o]; d[k] = PBV[o]; eh += EHP[o]; ev += EVP[o]; k++;
            }
            cs_finish_cell(w, inp, out, raw2ev, ev2raw, black, white, 6 + 2*ix, y, opt_med25(a), opt_med25(b2), opt_med25(c), opt_med25(d), eh, ev);
        }
    }
}

static void cs_dispatch(int mode, int w, int h, CHROMA_SMOOTH_TYPE * inp, CHROMA_SMOOTH_TYPE * out,
                        int * raw2ev, int * ev2raw, int black, int white)
{
    if (inp == out)
    {
        if (mode == 2) chroma_smooth_2x2_ref(w, h, inp, out, raw2ev, ev2raw, black, white);
        else if (mode == 3) chroma_smooth_3x3_ref(w, h, inp, out, raw2ev, ev2raw, black, white);
        else chroma_smooth_5x5_ref(w, h, inp, out, raw2ev, ev2raw, black, white);
        return;
    }

    int nx, ny, pad = (mode == 5) ? 4 : 2;
    if (mode == 5)
    {
        nx = (w - 6 > 6) ? (w - 6 - 6 + 1) / 2 : 0;
        ny = (h - 7 > 6) ? (h - 7 - 6 + 1) / 2 : 0;
    }
    else
    {
        nx = (w - 4 > 4) ? (w - 4 - 4 + 1) / 2 : 0;
        ny = (h - 5 > 4) ? (h - 5 - 4 + 1) / 2 : 0;
    }
    if (nx <= 0 || ny <= 0) return;

    int nstrips = (ny + CS_STRIP_H - 1) / CS_STRIP_H;
    size_t strip_bytes = 6 * (size_t)(nx + pad) * (CS_STRIP_H + pad) * sizeof(int);

#if defined(__APPLE__) && !defined(DISO_SINGLE_THREAD)
    __block int alloc_failed = 0;
    int nworkers = nstrips < 8 ? nstrips : 8;
    dispatch_apply((size_t)nworkers, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^(size_t worker_id) {
        int * planes = (int *)malloc(strip_bytes);
        if (!planes)
        {
            alloc_failed = 1;
            return;
        }
        for (int s = (int)worker_id; s < nstrips; s += nworkers)
        {
            int iy0 = s * CS_STRIP_H;
            int iy_end = iy0 + CS_STRIP_H;
            if (iy_end > ny) iy_end = ny;
            if (mode == 2) cs_process_strip_2x2(iy0, iy_end, nx, w, inp, out, raw2ev, ev2raw, black, white, planes);
            else if (mode == 3) cs_process_strip_3x3(iy0, iy_end, nx, w, inp, out, raw2ev, ev2raw, black, white, planes);
            else cs_process_strip_5x5(iy0, iy_end, nx, w, inp, out, raw2ev, ev2raw, black, white, planes);
        }
        free(planes);
    });
#elif defined(_OPENMP) && !defined(DISO_SINGLE_THREAD)
    int alloc_failed = 0;
    #pragma omp parallel
    {
        int * planes = (int *)malloc(strip_bytes);
        if (!planes)
        {
            alloc_failed = 1;
        }
        #pragma omp for schedule(dynamic, 1)
        for (int s = 0; s < nstrips; s++)
        {
            if (!planes) continue;
            int iy0 = s * CS_STRIP_H;
            int iy_end = iy0 + CS_STRIP_H;
            if (iy_end > ny) iy_end = ny;
            if (mode == 2) cs_process_strip_2x2(iy0, iy_end, nx, w, inp, out, raw2ev, ev2raw, black, white, planes);
            else if (mode == 3) cs_process_strip_3x3(iy0, iy_end, nx, w, inp, out, raw2ev, ev2raw, black, white, planes);
            else cs_process_strip_5x5(iy0, iy_end, nx, w, inp, out, raw2ev, ev2raw, black, white, planes);
        }
        free(planes);
    }
#else
    int alloc_failed = 0;
    int * planes = (int *)malloc(strip_bytes);
    if (!planes)
    {
        alloc_failed = 1;
    }
    else
    {
        for (int s = 0; s < nstrips; s++)
        {
            int iy0 = s * CS_STRIP_H;
            int iy_end = iy0 + CS_STRIP_H;
            if (iy_end > ny) iy_end = ny;
            if (mode == 2) cs_process_strip_2x2(iy0, iy_end, nx, w, inp, out, raw2ev, ev2raw, black, white, planes);
            else if (mode == 3) cs_process_strip_3x3(iy0, iy_end, nx, w, inp, out, raw2ev, ev2raw, black, white, planes);
            else cs_process_strip_5x5(iy0, iy_end, nx, w, inp, out, raw2ev, ev2raw, black, white, planes);
        }
        free(planes);
    }
#endif

    if (alloc_failed)
    {
        if (mode == 2) chroma_smooth_2x2_ref(w, h, inp, out, raw2ev, ev2raw, black, white);
        else if (mode == 3) chroma_smooth_3x3_ref(w, h, inp, out, raw2ev, ev2raw, black, white);
        else chroma_smooth_5x5_ref(w, h, inp, out, raw2ev, ev2raw, black, white);
    }
}

static void chroma_smooth_2x2(int w, int h, CHROMA_SMOOTH_TYPE * inp, CHROMA_SMOOTH_TYPE * out,
                              int * raw2ev, int * ev2raw, int black, int white)
{
    cs_dispatch(2, w, h, inp, out, raw2ev, ev2raw, black, white);
}

static void chroma_smooth_3x3(int w, int h, CHROMA_SMOOTH_TYPE * inp, CHROMA_SMOOTH_TYPE * out,
                              int * raw2ev, int * ev2raw, int black, int white)
{
    cs_dispatch(3, w, h, inp, out, raw2ev, ev2raw, black, white);
}

static void chroma_smooth_5x5(int w, int h, CHROMA_SMOOTH_TYPE * inp, CHROMA_SMOOTH_TYPE * out,
                              int * raw2ev, int * ev2raw, int black, int white)
{
    cs_dispatch(5, w, h, inp, out, raw2ev, ev2raw, black, white);
}
