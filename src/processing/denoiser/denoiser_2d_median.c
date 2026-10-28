/*!
 * \file denoise_2D_median.c
 * \author masc4ii
 * \copyright 2018
 * \brief an optimized 2D median denoiser
 */

#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif
#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#if defined(__SSE4_1__)
#include <smmintrin.h>
#endif
#endif
#include "denoiser_2d_median.h"

#define PIX_SORT(a, b) { if ((a) > (b)) { int temp = (a); (a) = (b); (b) = temp; } }

static inline int med4(int *p) {
    PIX_SORT(p[0], p[1]); PIX_SORT(p[2], p[3]);
    PIX_SORT(p[0], p[2]); PIX_SORT(p[1], p[3]);
    PIX_SORT(p[1], p[2]);
    return p[2];
}

static inline int opt_med9(int *p) {
    PIX_SORT(p[1], p[2]) ; PIX_SORT(p[4], p[5]) ; PIX_SORT(p[7], p[8]) ;
    PIX_SORT(p[0], p[1]) ; PIX_SORT(p[3], p[4]) ; PIX_SORT(p[6], p[7]) ;
    PIX_SORT(p[1], p[2]) ; PIX_SORT(p[4], p[5]) ; PIX_SORT(p[7], p[8]) ;
    PIX_SORT(p[0], p[3]) ; PIX_SORT(p[5], p[8]) ; PIX_SORT(p[4], p[7]) ;
    PIX_SORT(p[3], p[6]) ; PIX_SORT(p[1], p[4]) ; PIX_SORT(p[2], p[5]) ;
    PIX_SORT(p[4], p[7]) ; PIX_SORT(p[4], p[2]) ; PIX_SORT(p[6], p[4]) ;
    PIX_SORT(p[4], p[2]) ; return(p[4]) ;
}

static inline int opt_med16(int *p) {
    PIX_SORT(p[0], p[1]);   PIX_SORT(p[2], p[3]);   PIX_SORT(p[4], p[5]);   PIX_SORT(p[6], p[7]);
    PIX_SORT(p[8], p[9]);   PIX_SORT(p[10], p[11]); PIX_SORT(p[12], p[13]); PIX_SORT(p[14], p[15]);
    PIX_SORT(p[0], p[2]);   PIX_SORT(p[1], p[3]);   PIX_SORT(p[4], p[6]);   PIX_SORT(p[5], p[7]);
    PIX_SORT(p[8], p[10]);  PIX_SORT(p[9], p[11]);  PIX_SORT(p[12], p[14]); PIX_SORT(p[13], p[15]);
    PIX_SORT(p[0], p[4]);   PIX_SORT(p[1], p[5]);   PIX_SORT(p[2], p[6]);   PIX_SORT(p[3], p[7]);
    PIX_SORT(p[8], p[12]);  PIX_SORT(p[9], p[13]);  PIX_SORT(p[10], p[14]); PIX_SORT(p[11], p[15]);
    PIX_SORT(p[0], p[8]);   PIX_SORT(p[1], p[9]);   PIX_SORT(p[2], p[10]);  PIX_SORT(p[3], p[11]);
    PIX_SORT(p[4], p[12]);  PIX_SORT(p[5], p[13]);  PIX_SORT(p[6], p[14]);  PIX_SORT(p[7], p[15]);
    PIX_SORT(p[5], p[10]);  PIX_SORT(p[6], p[9]);   PIX_SORT(p[3], p[12]);  PIX_SORT(p[13], p[14]);
    PIX_SORT(p[7], p[11]);  PIX_SORT(p[1], p[2]);   PIX_SORT(p[4], p[8]);   PIX_SORT(p[1], p[4]);
    PIX_SORT(p[7], p[13]);  PIX_SORT(p[2], p[8]);   PIX_SORT(p[11], p[14]); PIX_SORT(p[2], p[4]);
    PIX_SORT(p[5], p[6]);   PIX_SORT(p[9], p[10]);  PIX_SORT(p[11], p[13]); PIX_SORT(p[3], p[8]);
    PIX_SORT(p[7], p[12]);  PIX_SORT(p[6], p[8]);   PIX_SORT(p[10], p[12]); PIX_SORT(p[3], p[5]);
    PIX_SORT(p[7], p[9]);   PIX_SORT(p[3], p[4]);   PIX_SORT(p[5], p[6]);   PIX_SORT(p[7], p[8]);
    PIX_SORT(p[9], p[10]);  PIX_SORT(p[11], p[12]); PIX_SORT(p[6], p[7]);   PIX_SORT(p[8], p[9]);
    return p[8];
}

static inline int opt_med25(int *p) {
    PIX_SORT(p[0], p[1]) ;   PIX_SORT(p[3], p[4]) ;   PIX_SORT(p[2], p[4]) ;
    PIX_SORT(p[2], p[3]) ;   PIX_SORT(p[6], p[7]) ;   PIX_SORT(p[5], p[7]) ;
    PIX_SORT(p[5], p[6]) ;   PIX_SORT(p[9], p[10]) ;  PIX_SORT(p[8], p[10]) ;
    PIX_SORT(p[8], p[9]) ;   PIX_SORT(p[12], p[13]) ; PIX_SORT(p[11], p[13]) ;
    PIX_SORT(p[11], p[12]) ; PIX_SORT(p[15], p[16]) ; PIX_SORT(p[14], p[16]) ;
    PIX_SORT(p[14], p[15]) ; PIX_SORT(p[18], p[19]) ; PIX_SORT(p[17], p[19]) ;
    PIX_SORT(p[17], p[18]) ; PIX_SORT(p[21], p[22]) ; PIX_SORT(p[20], p[22]) ;
    PIX_SORT(p[20], p[21]) ; PIX_SORT(p[23], p[24]) ; PIX_SORT(p[2], p[5]) ;
    PIX_SORT(p[3], p[6]) ;   PIX_SORT(p[0], p[6]) ;   PIX_SORT(p[0], p[3]) ;
    PIX_SORT(p[4], p[7]) ;   PIX_SORT(p[1], p[7]) ;   PIX_SORT(p[1], p[4]) ;
    PIX_SORT(p[11], p[14]) ; PIX_SORT(p[8], p[14]) ;  PIX_SORT(p[8], p[11]) ;
    PIX_SORT(p[12], p[15]) ; PIX_SORT(p[9], p[15]) ;  PIX_SORT(p[9], p[12]) ;
    PIX_SORT(p[13], p[16]) ; PIX_SORT(p[10], p[16]) ; PIX_SORT(p[10], p[13]) ;
    PIX_SORT(p[20], p[23]) ; PIX_SORT(p[17], p[23]) ; PIX_SORT(p[17], p[20]) ;
    PIX_SORT(p[21], p[24]) ; PIX_SORT(p[18], p[24]) ; PIX_SORT(p[18], p[21]) ;
    PIX_SORT(p[19], p[22]) ; PIX_SORT(p[8], p[17]) ;  PIX_SORT(p[9], p[18]) ;
    PIX_SORT(p[0], p[18]) ;  PIX_SORT(p[0], p[9]) ;   PIX_SORT(p[10], p[19]) ;
    PIX_SORT(p[1], p[19]) ;  PIX_SORT(p[1], p[10]) ;  PIX_SORT(p[11], p[20]) ;
    PIX_SORT(p[2], p[20]) ;  PIX_SORT(p[2], p[11]) ;  PIX_SORT(p[12], p[21]) ;
    PIX_SORT(p[3], p[21]) ;  PIX_SORT(p[3], p[12]) ;  PIX_SORT(p[13], p[22]) ;
    PIX_SORT(p[4], p[22]) ;  PIX_SORT(p[4], p[13]) ;  PIX_SORT(p[14], p[23]) ;
    PIX_SORT(p[5], p[23]) ;  PIX_SORT(p[5], p[14]) ;  PIX_SORT(p[15], p[24]) ;
    PIX_SORT(p[6], p[24]) ;  PIX_SORT(p[6], p[15]) ;  PIX_SORT(p[7], p[16]) ;
    PIX_SORT(p[7], p[19]) ;  PIX_SORT(p[13], p[21]) ; PIX_SORT(p[15], p[23]) ;
    PIX_SORT(p[7], p[13]) ;  PIX_SORT(p[7], p[15]) ;  PIX_SORT(p[1], p[9]) ;
    PIX_SORT(p[3], p[11]) ;  PIX_SORT(p[5], p[17]) ;  PIX_SORT(p[11], p[17]) ;
    PIX_SORT(p[9], p[17]) ;  PIX_SORT(p[4], p[10]) ;  PIX_SORT(p[6], p[12]) ;
    PIX_SORT(p[7], p[14]) ;  PIX_SORT(p[4], p[6]) ;   PIX_SORT(p[4], p[7]) ;
    PIX_SORT(p[12], p[14]) ; PIX_SORT(p[10], p[14]) ; PIX_SORT(p[6], p[7]) ;
    PIX_SORT(p[10], p[12]) ; PIX_SORT(p[6], p[10]) ;  PIX_SORT(p[6], p[17]) ;
    PIX_SORT(p[12], p[17]) ; PIX_SORT(p[7], p[17]) ;  PIX_SORT(p[7], p[10]) ;
    PIX_SORT(p[12], p[18]) ; PIX_SORT(p[7], p[12]) ;  PIX_SORT(p[10], p[18]) ;
    PIX_SORT(p[12], p[20]) ; PIX_SORT(p[10], p[20]) ; PIX_SORT(p[10], p[12]) ;
    return(p[12]) ;
}

static inline void swap_int(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

// Non-recursive median selection for arbitrary window sizes
static inline int fast_select(int a[], int n, int k) {
    int left = 0, right = n - 1;
    while (left < right) {
        if (right - left < 10) {
            for (int i = left + 1; i <= right; i++) {
                int key = a[i];
                int j = i - 1;
                while (j >= left && a[j] > key) {
                    a[j + 1] = a[j];
                    j--;
                }
                a[j + 1] = key;
            }
            return a[k];
        }
        int mid = left + (right - left) / 2;
        if (a[mid] < a[left]) swap_int(&a[mid], &a[left]);
        if (a[right] < a[left]) swap_int(&a[right], &a[left]);
        if (a[right] < a[mid]) swap_int(&a[right], &a[mid]);
        int pivot = a[mid];
        swap_int(&a[mid], &a[right]);

        int i = left;
        for (int j = left; j < right; j++) {
            if (a[j] < pivot) {
                swap_int(&a[i], &a[j]);
                i++;
            }
        }
        swap_int(&a[i], &a[right]);

        if (i == k) return a[k];
        else if (i < k) left = i + 1;
        else right = i - 1;
    }
    return a[k];
}

#if defined(__ARM_NEON) || defined(__aarch64__)
typedef uint16x8_t vec_u16_t;
#define VEC_SORT(a, b) { \
    uint16x8_t _mn = vminq_u16(a, b); \
    uint16x8_t _mx = vmaxq_u16(a, b); \
    a = _mn; b = _mx; \
}
#define HAS_SIMD 1
#elif defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
typedef uint16_t vec_u16_t __attribute__((vector_size(16)));
#if defined(__clang__) && __has_builtin(__builtin_elementwise_min) && __has_builtin(__builtin_elementwise_max)
#define VEC_SORT(a, b) { \
    vec_u16_t _mn = __builtin_elementwise_min(a, b); \
    vec_u16_t _mx = __builtin_elementwise_max(a, b); \
    a = _mn; b = _mx; \
}
#else
#define VEC_SORT(a, b) { \
    vec_u16_t _m = (a < b); \
    vec_u16_t _mn = (a & _m) | (b & ~_m); \
    vec_u16_t _mx = (b & _m) | (a & ~_m); \
    a = _mn; b = _mx; \
}
#endif
#define HAS_SIMD 1
#else
#define HAS_SIMD 0
#endif

#if HAS_SIMD
static inline vec_u16_t vec_med4(vec_u16_t p[4]) {
    VEC_SORT(p[0], p[1]); VEC_SORT(p[2], p[3]);
    VEC_SORT(p[0], p[2]); VEC_SORT(p[1], p[3]);
    VEC_SORT(p[1], p[2]);
    return p[2];
}

static inline vec_u16_t vec_med9(vec_u16_t p[9]) {
    VEC_SORT(p[1], p[2]) ; VEC_SORT(p[4], p[5]) ; VEC_SORT(p[7], p[8]) ;
    VEC_SORT(p[0], p[1]) ; VEC_SORT(p[3], p[4]) ; VEC_SORT(p[6], p[7]) ;
    VEC_SORT(p[1], p[2]) ; VEC_SORT(p[4], p[5]) ; VEC_SORT(p[7], p[8]) ;
    VEC_SORT(p[0], p[3]) ; VEC_SORT(p[5], p[8]) ; VEC_SORT(p[4], p[7]) ;
    VEC_SORT(p[3], p[6]) ; VEC_SORT(p[1], p[4]) ; VEC_SORT(p[2], p[5]) ;
    VEC_SORT(p[4], p[7]) ; VEC_SORT(p[4], p[2]) ; VEC_SORT(p[6], p[4]) ;
    VEC_SORT(p[4], p[2]) ; return p[4] ;
}

static inline vec_u16_t vec_med16(vec_u16_t p[16]) {
    VEC_SORT(p[0], p[1]);   VEC_SORT(p[2], p[3]);   VEC_SORT(p[4], p[5]);   VEC_SORT(p[6], p[7]);
    VEC_SORT(p[8], p[9]);   VEC_SORT(p[10], p[11]); VEC_SORT(p[12], p[13]); VEC_SORT(p[14], p[15]);
    VEC_SORT(p[0], p[2]);   VEC_SORT(p[1], p[3]);   VEC_SORT(p[4], p[6]);   VEC_SORT(p[5], p[7]);
    VEC_SORT(p[8], p[10]);  VEC_SORT(p[9], p[11]);  VEC_SORT(p[12], p[14]); VEC_SORT(p[13], p[15]);
    VEC_SORT(p[0], p[4]);   VEC_SORT(p[1], p[5]);   VEC_SORT(p[2], p[6]);   VEC_SORT(p[3], p[7]);
    VEC_SORT(p[8], p[12]);  VEC_SORT(p[9], p[13]);  VEC_SORT(p[10], p[14]); VEC_SORT(p[11], p[15]);
    VEC_SORT(p[0], p[8]);   VEC_SORT(p[1], p[9]);   VEC_SORT(p[2], p[10]);  VEC_SORT(p[3], p[11]);
    VEC_SORT(p[4], p[12]);  VEC_SORT(p[5], p[13]);  VEC_SORT(p[6], p[14]);  VEC_SORT(p[7], p[15]);
    VEC_SORT(p[5], p[10]);  VEC_SORT(p[6], p[9]);   VEC_SORT(p[3], p[12]);  VEC_SORT(p[13], p[14]);
    VEC_SORT(p[7], p[11]);  VEC_SORT(p[1], p[2]);   VEC_SORT(p[4], p[8]);   VEC_SORT(p[1], p[4]);
    VEC_SORT(p[7], p[13]);  VEC_SORT(p[2], p[8]);   VEC_SORT(p[11], p[14]); VEC_SORT(p[2], p[4]);
    VEC_SORT(p[5], p[6]);   VEC_SORT(p[9], p[10]);  VEC_SORT(p[11], p[13]); VEC_SORT(p[3], p[8]);
    VEC_SORT(p[7], p[12]);  VEC_SORT(p[6], p[8]);   VEC_SORT(p[10], p[12]); VEC_SORT(p[3], p[5]);
    VEC_SORT(p[7], p[9]);   VEC_SORT(p[3], p[4]);   VEC_SORT(p[5], p[6]);   VEC_SORT(p[7], p[8]);
    VEC_SORT(p[9], p[10]);  VEC_SORT(p[11], p[12]); VEC_SORT(p[6], p[7]);   VEC_SORT(p[8], p[9]);
    return p[8];
}

static inline vec_u16_t vec_med25(vec_u16_t p[25]) {
    VEC_SORT(p[0], p[1]) ;   VEC_SORT(p[3], p[4]) ;   VEC_SORT(p[2], p[4]) ;
    VEC_SORT(p[2], p[3]) ;   VEC_SORT(p[6], p[7]) ;   VEC_SORT(p[5], p[7]) ;
    VEC_SORT(p[5], p[6]) ;   VEC_SORT(p[9], p[10]) ;  VEC_SORT(p[8], p[10]) ;
    VEC_SORT(p[8], p[9]) ;   VEC_SORT(p[12], p[13]) ; VEC_SORT(p[11], p[13]) ;
    VEC_SORT(p[11], p[12]) ; VEC_SORT(p[15], p[16]) ; VEC_SORT(p[14], p[16]) ;
    VEC_SORT(p[14], p[15]) ; VEC_SORT(p[18], p[19]) ; VEC_SORT(p[17], p[19]) ;
    VEC_SORT(p[17], p[18]) ; VEC_SORT(p[21], p[22]) ; VEC_SORT(p[20], p[22]) ;
    VEC_SORT(p[20], p[21]) ; VEC_SORT(p[23], p[24]) ; VEC_SORT(p[2], p[5]) ;
    VEC_SORT(p[3], p[6]) ;   VEC_SORT(p[0], p[6]) ;   VEC_SORT(p[0], p[3]) ;
    VEC_SORT(p[4], p[7]) ;   VEC_SORT(p[1], p[7]) ;   VEC_SORT(p[1], p[4]) ;
    VEC_SORT(p[11], p[14]) ; VEC_SORT(p[8], p[14]) ;  VEC_SORT(p[8], p[11]) ;
    VEC_SORT(p[12], p[15]) ; VEC_SORT(p[9], p[15]) ;  VEC_SORT(p[9], p[12]) ;
    VEC_SORT(p[13], p[16]) ; VEC_SORT(p[10], p[16]) ; VEC_SORT(p[10], p[13]) ;
    VEC_SORT(p[20], p[23]) ; VEC_SORT(p[17], p[23]) ; VEC_SORT(p[17], p[20]) ;
    VEC_SORT(p[21], p[24]) ; VEC_SORT(p[18], p[24]) ; VEC_SORT(p[18], p[21]) ;
    VEC_SORT(p[19], p[22]) ; VEC_SORT(p[8], p[17]) ;  VEC_SORT(p[9], p[18]) ;
    VEC_SORT(p[0], p[18]) ;  VEC_SORT(p[0], p[9]) ;   VEC_SORT(p[10], p[19]) ;
    VEC_SORT(p[1], p[19]) ;  VEC_SORT(p[1], p[10]) ;  VEC_SORT(p[11], p[20]) ;
    VEC_SORT(p[2], p[20]) ;  VEC_SORT(p[2], p[11]) ;  VEC_SORT(p[12], p[21]) ;
    VEC_SORT(p[3], p[21]) ;  VEC_SORT(p[3], p[12]) ;  VEC_SORT(p[13], p[22]) ;
    VEC_SORT(p[4], p[22]) ;  VEC_SORT(p[4], p[13]) ;  VEC_SORT(p[14], p[23]) ;
    VEC_SORT(p[5], p[23]) ;  VEC_SORT(p[5], p[14]) ;  VEC_SORT(p[15], p[24]) ;
    VEC_SORT(p[6], p[24]) ;  VEC_SORT(p[6], p[15]) ;  VEC_SORT(p[7], p[16]) ;
    VEC_SORT(p[7], p[19]) ;  VEC_SORT(p[13], p[21]) ; VEC_SORT(p[15], p[23]) ;
    VEC_SORT(p[7], p[13]) ;  VEC_SORT(p[7], p[15]) ;  VEC_SORT(p[1], p[9]) ;
    VEC_SORT(p[3], p[11]) ;  VEC_SORT(p[5], p[17]) ;  VEC_SORT(p[11], p[17]) ;
    VEC_SORT(p[9], p[17]) ;  VEC_SORT(p[4], p[10]) ;  VEC_SORT(p[6], p[12]) ;
    VEC_SORT(p[7], p[14]) ;  VEC_SORT(p[4], p[6]) ;   VEC_SORT(p[4], p[7]) ;
    VEC_SORT(p[12], p[14]) ; VEC_SORT(p[10], p[14]) ; VEC_SORT(p[6], p[7]) ;
    VEC_SORT(p[10], p[12]) ; VEC_SORT(p[6], p[10]) ;  VEC_SORT(p[6], p[17]) ;
    VEC_SORT(p[12], p[17]) ; VEC_SORT(p[7], p[17]) ;  VEC_SORT(p[7], p[10]) ;
    VEC_SORT(p[12], p[18]) ; VEC_SORT(p[7], p[12]) ;  VEC_SORT(p[10], p[18]) ;
    VEC_SORT(p[12], p[20]) ; VEC_SORT(p[10], p[20]) ; VEC_SORT(p[10], p[12]) ;
    return p[12];
}

static inline void vec_bitonic_sort64(vec_u16_t a[64]) {
    for (int k = 2; k <= 64; k *= 2) {
        for (int j = k / 2; j > 0; j /= 2) {
            for (int i = 0; i < 64; i++) {
                int l = i ^ j;
                if (l > i) {
                    if ((i & k) == 0) {
                        VEC_SORT(a[i], a[l]);
                    } else {
                        VEC_SORT(a[l], a[i]);
                    }
                }
            }
        }
    }
}

static inline void load_rgb8(const uint16_t *src, vec_u16_t *r, vec_u16_t *g, vec_u16_t *b) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    uint16x8x3_t c = vld3q_u16(src);
    *r = c.val[0]; *g = c.val[1]; *b = c.val[2];
#else
    uint16_t r_buf[8], g_buf[8], b_buf[8];
    for (int i = 0; i < 8; i++) {
        r_buf[i] = src[i * 3 + 0];
        g_buf[i] = src[i * 3 + 1];
        b_buf[i] = src[i * 3 + 2];
    }
    memcpy(r, r_buf, 16);
    memcpy(g, g_buf, 16);
    memcpy(b, b_buf, 16);
#endif
}

static inline void store_rgb8(uint16_t *dst, vec_u16_t r, vec_u16_t g, vec_u16_t b) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    uint16x8x3_t c;
    c.val[0] = r; c.val[1] = g; c.val[2] = b;
    vst3q_u16(dst, c);
#else
    uint16_t r_buf[8], g_buf[8], b_buf[8];
    memcpy(r_buf, &r, 16);
    memcpy(g_buf, &g, 16);
    memcpy(b_buf, &b, 16);
    for (int i = 0; i < 8; i++) {
        dst[i * 3 + 0] = r_buf[i];
        dst[i * 3 + 1] = g_buf[i];
        dst[i * 3 + 2] = b_buf[i];
    }
#endif
}

static inline vec_u16_t vec_blend(vec_u16_t med, vec_u16_t orig, float strengthF, float antiStrengthF) {
    uint16_t m[8], o[8], res[8];
    memcpy(m, &med, 16);
    memcpy(o, &orig, 16);
    for (int i = 0; i < 8; i++) {
        res[i] = (uint16_t)(strengthF * (float)m[i] + antiStrengthF * (float)o[i]);
    }
    vec_u16_t ret;
    memcpy(&ret, res, 16);
    return ret;
}

static inline vec_u16_t vec_dup(uint16_t val) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vdupq_n_u16(val);
#else
    vec_u16_t ret;
    uint16_t buf[8];
    for (int i = 0; i < 8; i++) buf[i] = val;
    memcpy(&ret, buf, 16);
    return ret;
#endif
}
#endif

// 2D median filter
void denoise_2D_median(uint16_t *data, int width, int height, uint8_t window, uint8_t strength)
{
    if( strength > 100 ) strength = 100;
    float strengthF = strength / 100.0;
    float antiStrengthF = 1 - strengthF;

    uint32_t imageSize = (uint32_t)width * height * 3;

    uint16_t * noisy = (uint16_t *)malloc( imageSize * sizeof( uint16_t ) );
    if (!noisy) return;
    memcpy( noisy, data, imageSize * sizeof( uint16_t ) );

    uint16_t winSize = window * window;
    uint16_t edgeX = window / 2;
    uint16_t edgeY = window / 2;
    uint8_t middle = (uint8_t)(winSize / 2);

    int start_y = edgeY;
    int end_y = height - edgeY;
    int active_rows = end_y - start_y;
    if (active_rows <= 0 || width <= (edgeX * 2)) {
        free(noisy);
        return;
    }

#ifdef __APPLE__
    int workers = (active_rows < 8) ? active_rows : 8;
    dispatch_apply(workers, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t chunk) {
        int y0 = start_y + (int)((chunk * active_rows) / workers);
        int y1 = start_y + (int)(((chunk + 1) * active_rows) / workers);
        for( int y = y0; y < y1; y++ )
#else
#pragma omp parallel for schedule(static)
    for( int y = start_y; y < end_y; y++ )
#endif
    {
        const uint16_t *row_ptrs[64];
        for (int r = 0; r < window; r++) {
            row_ptrs[r] = noisy + ((size_t)(y + r - edgeY) * width) * 3;
        }
        uint16_t *out_row = data + ((size_t)y * width) * 3;

        int x = edgeX;

#if HAS_SIMD
        if (window <= 7) {
            for (; x <= width - edgeX - 8; x += 8) {
                if (window == 2) {
                    vec_u16_t wR[4], wG[4], wB[4];
                    int idx = 0;
                    for (int fy = 0; fy < 2; fy++) {
                        for (int fx = 0; fx < 2; fx++) {
                            load_rgb8(row_ptrs[fy] + (x - edgeX + fx) * 3, &wR[idx], &wG[idx], &wB[idx]);
                            idx++;
                        }
                    }
                    vec_u16_t mR = vec_med4(wR), mG = vec_med4(wG), mB = vec_med4(wB);
                    vec_u16_t origR, origG, origB;
                    load_rgb8(out_row + x * 3, &origR, &origG, &origB);
                    vec_u16_t outR = vec_blend(mR, origR, strengthF, antiStrengthF);
                    vec_u16_t outG = vec_blend(mG, origG, strengthF, antiStrengthF);
                    vec_u16_t outB = vec_blend(mB, origB, strengthF, antiStrengthF);
                    store_rgb8(out_row + x * 3, outR, outG, outB);
                } else if (window == 3) {
                    vec_u16_t wR[9], wG[9], wB[9];
                    int idx = 0;
                    for (int fy = 0; fy < 3; fy++) {
                        for (int fx = 0; fx < 3; fx++) {
                            load_rgb8(row_ptrs[fy] + (x - edgeX + fx) * 3, &wR[idx], &wG[idx], &wB[idx]);
                            idx++;
                        }
                    }
                    vec_u16_t mR = vec_med9(wR), mG = vec_med9(wG), mB = vec_med9(wB);
                    vec_u16_t origR, origG, origB;
                    load_rgb8(out_row + x * 3, &origR, &origG, &origB);
                    vec_u16_t outR = vec_blend(mR, origR, strengthF, antiStrengthF);
                    vec_u16_t outG = vec_blend(mG, origG, strengthF, antiStrengthF);
                    vec_u16_t outB = vec_blend(mB, origB, strengthF, antiStrengthF);
                    store_rgb8(out_row + x * 3, outR, outG, outB);
                } else if (window == 4) {
                    vec_u16_t wR[16], wG[16], wB[16];
                    int idx = 0;
                    for (int fy = 0; fy < 4; fy++) {
                        for (int fx = 0; fx < 4; fx++) {
                            load_rgb8(row_ptrs[fy] + (x - edgeX + fx) * 3, &wR[idx], &wG[idx], &wB[idx]);
                            idx++;
                        }
                    }
                    vec_u16_t mR = vec_med16(wR), mG = vec_med16(wG), mB = vec_med16(wB);
                    vec_u16_t origR, origG, origB;
                    load_rgb8(out_row + x * 3, &origR, &origG, &origB);
                    vec_u16_t outR = vec_blend(mR, origR, strengthF, antiStrengthF);
                    vec_u16_t outG = vec_blend(mG, origG, strengthF, antiStrengthF);
                    vec_u16_t outB = vec_blend(mB, origB, strengthF, antiStrengthF);
                    store_rgb8(out_row + x * 3, outR, outG, outB);
                } else if (window == 5) {
                    vec_u16_t wR[25], wG[25], wB[25];
                    int idx = 0;
                    for (int fy = 0; fy < 5; fy++) {
                        for (int fx = 0; fx < 5; fx++) {
                            load_rgb8(row_ptrs[fy] + (x - edgeX + fx) * 3, &wR[idx], &wG[idx], &wB[idx]);
                            idx++;
                        }
                    }
                    vec_u16_t mR = vec_med25(wR), mG = vec_med25(wG), mB = vec_med25(wB);
                    vec_u16_t origR, origG, origB;
                    load_rgb8(out_row + x * 3, &origR, &origG, &origB);
                    vec_u16_t outR = vec_blend(mR, origR, strengthF, antiStrengthF);
                    vec_u16_t outG = vec_blend(mG, origG, strengthF, antiStrengthF);
                    vec_u16_t outB = vec_blend(mB, origB, strengthF, antiStrengthF);
                    store_rgb8(out_row + x * 3, outR, outG, outB);
                } else if (window == 6) {
                    vec_u16_t wR[64], wG[64], wB[64];
                    int idx = 0;
                    for (int fy = 0; fy < 6; fy++) {
                        for (int fx = 0; fx < 6; fx++) {
                            load_rgb8(row_ptrs[fy] + (x - edgeX + fx) * 3, &wR[idx], &wG[idx], &wB[idx]);
                            idx++;
                        }
                    }
                    vec_u16_t pad = vec_dup(0xFFFF);
                    for (int i = 36; i < 64; i++) {
                        wR[i] = pad; wG[i] = pad; wB[i] = pad;
                    }
                    vec_bitonic_sort64(wR); vec_bitonic_sort64(wG); vec_bitonic_sort64(wB);
                    vec_u16_t origR, origG, origB;
                    load_rgb8(out_row + x * 3, &origR, &origG, &origB);
                    vec_u16_t outR = vec_blend(wR[18], origR, strengthF, antiStrengthF);
                    vec_u16_t outG = vec_blend(wG[18], origG, strengthF, antiStrengthF);
                    vec_u16_t outB = vec_blend(wB[18], origB, strengthF, antiStrengthF);
                    store_rgb8(out_row + x * 3, outR, outG, outB);
                } else if (window == 7) {
                    vec_u16_t wR[64], wG[64], wB[64];
                    int idx = 0;
                    for (int fy = 0; fy < 7; fy++) {
                        for (int fx = 0; fx < 7; fx++) {
                            load_rgb8(row_ptrs[fy] + (x - edgeX + fx) * 3, &wR[idx], &wG[idx], &wB[idx]);
                            idx++;
                        }
                    }
                    vec_u16_t pad = vec_dup(0xFFFF);
                    for (int i = 49; i < 64; i++) {
                        wR[i] = pad; wG[i] = pad; wB[i] = pad;
                    }
                    vec_bitonic_sort64(wR); vec_bitonic_sort64(wG); vec_bitonic_sort64(wB);
                    vec_u16_t origR, origG, origB;
                    load_rgb8(out_row + x * 3, &origR, &origG, &origB);
                    vec_u16_t outR = vec_blend(wR[24], origR, strengthF, antiStrengthF);
                    vec_u16_t outG = vec_blend(wG[24], origG, strengthF, antiStrengthF);
                    vec_u16_t outB = vec_blend(wB[24], origB, strengthF, antiStrengthF);
                    store_rgb8(out_row + x * 3, outR, outG, outB);
                }
            }
        }
#endif

        // Scalar processing for trailing pixels and non-NEON targets
        for( ; x < width - edgeX; x++ )
        {
            int medR, medG, medB;
            if (window == 2) {
                int wR[4], wG[4], wB[4];
                int idx = 0;
                for (int fy = 0; fy < 2; fy++) {
                    const uint16_t *p = row_ptrs[fy] + (x - edgeX) * 3;
                    for (int fx = 0; fx < 2; fx++, p += 3) {
                        wR[idx] = p[0]; wG[idx] = p[1]; wB[idx] = p[2]; idx++;
                    }
                }
                medR = med4(wR); medG = med4(wG); medB = med4(wB);
            } else if (window == 3) {
                int wR[9], wG[9], wB[9];
                int idx = 0;
                for (int fy = 0; fy < 3; fy++) {
                    const uint16_t *p = row_ptrs[fy] + (x - edgeX) * 3;
                    for (int fx = 0; fx < 3; fx++, p += 3) {
                        wR[idx] = p[0]; wG[idx] = p[1]; wB[idx] = p[2]; idx++;
                    }
                }
                medR = opt_med9(wR); medG = opt_med9(wG); medB = opt_med9(wB);
            } else if (window == 4) {
                int wR[16], wG[16], wB[16];
                int idx = 0;
                for (int fy = 0; fy < 4; fy++) {
                    const uint16_t *p = row_ptrs[fy] + (x - edgeX) * 3;
                    for (int fx = 0; fx < 4; fx++, p += 3) {
                        wR[idx] = p[0]; wG[idx] = p[1]; wB[idx] = p[2]; idx++;
                    }
                }
                medR = opt_med16(wR); medG = opt_med16(wG); medB = opt_med16(wB);
            } else if (window == 5) {
                int wR[25], wG[25], wB[25];
                int idx = 0;
                for (int fy = 0; fy < 5; fy++) {
                    const uint16_t *p = row_ptrs[fy] + (x - edgeX) * 3;
                    for (int fx = 0; fx < 5; fx++, p += 3) {
                        wR[idx] = p[0]; wG[idx] = p[1]; wB[idx] = p[2]; idx++;
                    }
                }
                medR = opt_med25(wR); medG = opt_med25(wG); medB = opt_med25(wB);
            } else {
                int stackR[64], stackG[64], stackB[64];
                int *wR = (winSize <= 64) ? stackR : (int*)malloc(winSize * sizeof(int));
                int *wG = (winSize <= 64) ? stackG : (int*)malloc(winSize * sizeof(int));
                int *wB = (winSize <= 64) ? stackB : (int*)malloc(winSize * sizeof(int));
                int idx = 0;
                for (int fy = 0; fy < window; fy++) {
                    const uint16_t *p = row_ptrs[fy] + (x - edgeX) * 3;
                    for (int fx = 0; fx < window; fx++, p += 3) {
                        wR[idx] = p[0]; wG[idx] = p[1]; wB[idx] = p[2]; idx++;
                    }
                }
                medR = fast_select(wR, winSize, middle);
                medG = fast_select(wG, winSize, middle);
                medB = fast_select(wB, winSize, middle);
                if (winSize > 64) {
                    free(wR); free(wG); free(wB);
                }
            }

            size_t out_pos = (size_t)x * 3;
            out_row[out_pos + 0] = (uint16_t)(strengthF * (float)medR + antiStrengthF * (float)out_row[out_pos + 0]);
            out_row[out_pos + 1] = (uint16_t)(strengthF * (float)medG + antiStrengthF * (float)out_row[out_pos + 1]);
            out_row[out_pos + 2] = (uint16_t)(strengthF * (float)medB + antiStrengthF * (float)out_row[out_pos + 2]);
        }
    }
#ifdef __APPLE__
    });
#endif

    free( noisy );
}
