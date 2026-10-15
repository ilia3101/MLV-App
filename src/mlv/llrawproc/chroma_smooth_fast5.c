#include <string.h>
#include <stdlib.h>


typedef int v4i __attribute__((vector_size(16)));

#if defined(__clang__) && __has_builtin(__builtin_elementwise_min) && __has_builtin(__builtin_elementwise_max)
#define VSORT(a,b) { v4i _t = __builtin_elementwise_min((a),(b)); (b) = __builtin_elementwise_max((a),(b)); (a) = _t; }
#elif defined(__GNUC__)
#define VSORT(a,b) { v4i _min = ((a) < (b)) ? (a) : (b); v4i _max = ((a) > (b)) ? (a) : (b); (a) = _min; (b) = _max; }
#endif

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

static inline void cs5_finish_cell(int w, uint32_t * inp, uint32_t * out, int * raw2ev, int * ev2raw, int black, int white,
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

enum { CS5_STRIP_H = 32 };

void cs5_process_strip(int iy0, int iy_end, int nx, int w, uint32_t * inp, uint32_t * out,
                              int * raw2ev, int * ev2raw, int black, int white, int * planes)
{
    int sh = iy_end - iy0;
    int PW = nx + 4;
    int PH = sh + 4;
    size_t pn = (size_t)PW * PH;
    int * PRH = planes, * PBH = planes + pn, * PRV = planes + 2*pn;
    int * PBV = planes + 3*pn, * EHP = planes + 4*pn, * EVP = planes + 5*pn;

    for (int pcy = 0; pcy < PH; pcy++)
    {
        int Y = 2 * (iy0 + pcy + 1);
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
                cs5_finish_cell(w, inp, out, raw2ev, ev2raw, black, white, 6 + 2*(ix+l), y, drh[l], dbh[l], drv[l], dbv[l], eh[l], ev[l]);
        }
        for (; ix < nx; ix++)
        {
            int a[25], b2[25], c[25], d[25]; int eh = 0, ev = 0, k = 0;
            for (int dj = 0; dj < 5; dj++) for (int di = 0; di < 5; di++) {
                size_t o = (size_t)(sy + dj) * PW + ix + di;
                a[k] = PRH[o]; b2[k] = PBH[o]; c[k] = PRV[o]; d[k] = PBV[o]; eh += EHP[o]; ev += EVP[o]; k++;
            }
            cs5_finish_cell(w, inp, out, raw2ev, ev2raw, black, white, 6 + 2*ix, y, opt_med25(a), opt_med25(b2), opt_med25(c), opt_med25(d), eh, ev);
        }
    }
}

static void chroma_smooth_5x5(int w, int h, uint32_t * inp, uint32_t * out, int* raw2ev, int* ev2raw, int black, int white)
{
    if (inp == out) { chroma_smooth_5x5_ref(w, h, inp, out, raw2ev, ev2raw, black, white); return; }

    int nx = (w - 6 > 6) ? (w - 6 - 6 + 1) / 2 : 0;
    int ny = (h - 7 > 6) ? (h - 7 - 6 + 1) / 2 : 0;
    if (nx <= 0 || ny <= 0) return;

    int nstrips = (ny + CS5_STRIP_H - 1) / CS5_STRIP_H;
    size_t strip_bytes = 6 * (size_t)(nx + 4) * (CS5_STRIP_H + 4) * sizeof(int);

    int alloc_failed = 0;
    #pragma omp parallel for schedule(dynamic, 1)
    for (int s = 0; s < nstrips; s++)
    {
        int * planes = (int *)malloc(strip_bytes);
        if (!planes) { alloc_failed = 1; continue; }
        int iy0 = s * CS5_STRIP_H;
        int iy_end = iy0 + CS5_STRIP_H;
        if (iy_end > ny) iy_end = ny;
        cs5_process_strip(iy0, iy_end, nx, w, inp, out, raw2ev, ev2raw, black, white, planes);
        free(planes);
    }
    if (alloc_failed) chroma_smooth_5x5_ref(w, h, inp, out, raw2ev, ev2raw, black, white);
}
