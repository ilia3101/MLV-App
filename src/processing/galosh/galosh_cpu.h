/* galosh_cpu.h  --  GALOSH CPU-side complete library (algorithm core +
 * runtime infrastructure).  Single header, `static` definitions so each
 * translation unit gets its own copy (matches darktable plugin convention
 * where a single .c file is dropped into an IOP and can't share symbols).
 *
 * ##############################################################
 * # [LATEST] used by GALOSH_RAW_H / GALOSH_YUV_G canonical pipelines
 * ##############################################################
 *   Phase 0 (noise est)  : galosh_estimate_noise + estimate_gat_sigma_mosaic
 *                          (RAW), estimate_sigma_plane (YUV per-plane MAD)
 *   Phase 1 (GAT)        : gat_forward, gat_build_inverse_table,
 *                          gat_inverse_exact, gat_inv_table_t,
 *                          estimate_gat_sigma_halfres
 *   Phase 3 (forward WHT, RAW_H) : gat_h_forward_wht_stride1 (cycle-spinning
 *                          stride=1 forward 2x2 WHT, mirror-padded)
 *   Phase 4 (CFA demod, RAW_H)   : gat_h_demodulate_chroma (self-inverse
 *                          (-1)^r / (-1)^c / (-1)^(r+c) sign flip on
 *                          C1/C2/C3; also used as Phase 6 remod)
 *   Phase 5 luma (RAW_H/YUV_G)   : galosh_pass12_multiorient_blocked
 *                          (= pass1_blocked + pass2_blocked, with
 *                           use_robust_shrink=1 = MAD-based σ_Y)
 *   Phase 5 chroma (RAW_H/YUV_G) : galosh_loess_chroma (Y-guided
 *                          bilateral LOESS, R=GALOSH_LOESS_RADIUS,
 *                          BW=GALOSH_LOESS_BW)
 *   Phase 7 (inverse WHT, RAW_H) : gat_h_inverse_overlap_add (4-block
 *                          per-pixel overlap-add inverse 2x2 WHT)
 *   YUV Y plane LOSH     : galosh_pass12_multiorient_blocked
 *   YUV chroma           : galosh_loess_chroma on Cb/Cr
 *   Kaiser windows       : init_galosh_kaiser, galosh_kaiser_1d/2d/half_2d
 *   WHT primitives       : wht2d_8x8 (sequency 8x8), wht1d_8 helpers
 *   Helper sort          : compare_floats_galosh, partial_select_sort
 *
 * ##############################################################
 * # [PREVIOUS: GALOSH_RAW_G] kept for bench reproduction (--variant=g)
 * ##############################################################
 *   Phase 4(b) K14 box   : compute_L_fullres (lives in galosh_raw_cpu.c)
 *                          half-res L+C → full-res L via 4-tap box
 *   Phase 4(c) K15 P2    : galosh_pass2_multiorient w/ n_orient=1 (= plain
 *                          galosh_pass2 on K14-reconstructed L_fullres)
 *   Phase 4(d) K16 chrup : galosh_upsample_2x_ewajl3 + galosh_jinc
 *                          (half-res C → full-res C via EWA Jinc-Lanczos-3)
 *   These three (K14/K15/K16) are no longer reached when --variant=h.
 *
 * ##############################################################
 * # [ARCHIVED] kept for bench reproduction; never called by default
 * ##############################################################
 *   galosh_pass1, galosh_pass2     : single-orient flat-buffer pass1/pass2
 *                                    (called only by [ARCHIVED] legacy
 *                                    stride=4 raw GALOSH path in
 *                                    galosh_raw_cpu.c #ifdef GALOSH_LEGACY)
 *   galosh_rotate_bilinear         : variant B (n_orient=4) helper.
 *                                    Called only when n_orient>1; default
 *                                    n_orient=1 skips it (identity rotation).
 *   galosh_pass2_ex                : pass2 variant w/ Wiener-floor override
 *                                    (was needed for chroma archived paths)
 *   galosh_pass12_multiorient (NON-blocked) : superseded by *_blocked
 *   WHT 4x4 primitives             : used only by archived k13_block=4
 *                                    grain-scale-matched experiment
 *
 * Sections (search "Section:" to navigate):
 *   1. Runtime infrastructure (alloc shim, OMP macros, prof helpers,
 *      median selection helper).
 *   2. Noise model: Foi-Mäkitalo Poisson-Gaussian + GAT (forward / exact
 *      unbiased inverse / blind sigma estimation).
 *   3. WHT primitives (8x8 + 4x4 in sequency order).  4x4 = [ARCHIVED].
 *   4. Kaiser windows (8x8 + 4x4 overlap-add weights).
 *   5. GALOSH passes (block-size-parameterized BayesShrink Pass1 +
 *      empirical Wiener Pass2 + multi-orientation wrapper).  Pass1
 *      supports MAD-based robust sigma_Y for noise-cluster killing
 *      (= [LATEST] GALOSH_RAW_G / YUV_G core).
 *   6. LOESS chroma denoise (luma-guided locally-weighted regression).
 *   7. EWA Jinc-Lanczos-3 generic 2x upsample (used by Phase 4(d) K16).
 *
 * RAW-specific orchestration (Phase 0..4 main pipeline) lives in
 * galosh_raw_cpu.c.  YUV-specific orchestration (sRGB <-> linear,
 * BT.709 YCbCr, Y plane LOSH + chroma LOESS) lives in galosh_yuv_cpu.c.
 */
#ifndef GALOSH_CPU_H
#define GALOSH_CPU_H

/* Introspection TUs see every static function here but only use
 * the params struct from rawdenoise.c.  Silence -Wunused-function so
 * -Werror doesn't kill the auto-generated introspection compile. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

/*
 * Standalone raw denoiser -- GALOSH (GAT L/C-decomposed Overlap Shrinkage)
 *
 * Replaces BM3D with a purely local WHT-based shrinkage denoiser.
 * No block matching, no non-local processing.
 *
 * Usage: rawdenoise_v6 input.bin output.bin width height
 *        [method] [strength] [luma_str] [chroma_str] [alpha] [sigma_sq]
 *
 * Input/output: 32-bit float raw Bayer mosaic (RGGB), row-major
 *
 * Build:
 *   gcc -O3 -march=native -msse4.1 -fopenmp -o rawdenoise_v6 rawdenoise_v6.c -lm
 *
 * Theory:
 *   GALOSH is a two-pass local denoiser that relies on the Generalized
 *   Anscombe Transform (GAT) to normalize Poisson-Gaussian noise to
 *   i.i.d. Gaussian (sigma=1). In this stabilized domain, local adaptive
 *   shrinkage in the Walsh-Hadamard Transform (WHT) domain is sufficient
 *   without non-local block matching.
 *
 *   Pass 1: 8x8 WHT + BayesShrink adaptive hard threshold (pilot)
 *     Ref: Chang, Yu & Vetterli, "Adaptive wavelet thresholding for image
 *          denoising and compression", IEEE TIP, 2000
 *   Pass 2: 8x8 WHT + empirical Wiener shrinkage using pilot
 *     Ref: Textbook James-Stein / empirical Wiener estimator
 *
 *   The L/C decomposition via 2x2 WHT separates luma and chroma at half
 *   resolution, allowing independent strength control.
 *     Ref: Danielyan et al. "Cross-color BM3D" (LNLA 2009) -- for the
 *          L/C decomposition idea (we replace BM3D with local shrinkage)
 *
 *   GAT and exact unbiased inverse:
 *     Ref: Anscombe (1948), Foi et al. (Sig.Proc. 2008),
 *          Makitalo & Foi (TIP 2013)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdint.h>
#include <omp.h>
#include <time.h>
#if defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>  /* SSE  */
#include <emmintrin.h>  /* SSE2 */
#include <pmmintrin.h>  /* SSE3: _mm_hadd_ps */
#endif

/* =====================================================================
 *  GALOSH_CHROMA_CLAMP — chroma-overshoot fix (2026-06-27), now CANONICAL.
 *
 *  The L-guided chroma path (P5/P7 degree-1 LOESS regression + P9 K16 jinc
 *  joint-bilateral upsample) had NO bound on the reconstructed chroma: the
 *  jinc kernel's negative sidelobes (non-convex weights) let the upsample ring
 *  PAST the local input-chroma range at high-contrast luma edges, and the
 *  sum_w floor dropped its sign when the sidelobes cancelled (sum_w≈0).  The
 *  resulting chroma overshoot, magnified by the blue WB gain (~×2.7), showed as
 *  isolated magenta specks (worst RawNIND scenes lost up to ~0.13 LPIPS).
 *
 *  Fix (mirrors the INT pipeline, which never showed it): (1) clamp the LOESS
 *  output and the K16-upsampled chroma to the FULL-window local INPUT chroma
 *  range — "the denoiser may not invent colour the input lacks"; (2) sign-
 *  preserving sum_w floor.  Validated baseline→fix: magenta→~0 (below INT),
 *  LPIPS net −0.028 over 17 scenes, ~1.4% pixels touched.  Define it by default
 *  so every build (standalone + darktable) gets the fix; `-UGALOSH_CHROMA_CLAMP`
 *  recovers the pre-fix path for A/B reproduction.
 * ===================================================================== */
#ifndef GALOSH_CHROMA_CLAMP
#define GALOSH_CHROMA_CLAMP 1
#endif

/* =====================================================================
 *  Environment compat layer
 *
 *  This header is used in three contexts:
 *    (a) standalone CPU drivers (galosh_raw_cpu.c, galosh_yuv_cpu.c),
 *        built without darktable — we provide every symbol locally.
 *    (b) darktable IOP (rawdenoise.c) — the real dt_* symbols come from
 *        darktable headers that the IOP already included BEFORE this
 *        header is pulled in.  Define GALOSH_DT_ENV before including
 *        galosh_core.h in that case to skip the standalone stubs.
 *    (c) external plugins — decide per host.
 *
 *  The standalone stubs block below only compiles when !GALOSH_DT_ENV.
 * ===================================================================== */
#ifdef GALOSH_DT_ENV
/* darktable exposes `dt_alloc_aligned(size)` (1-arg, implicit cacheline
 * alignment via DT_CACHELINE_BYTES).  Our core calls `dt_alloc_align(
 * alignment, sz)` (2-arg) in a few places where the standalone build
 * picks a tighter alignment.  Wrap to the 1-arg form and discard the
 * explicit alignment — DT_CACHELINE_BYTES satisfies every SSE/AVX load
 * we do. */
static inline void *dt_alloc_align(size_t alignment, size_t sz)
{
  (void)alignment;
  return dt_alloc_aligned(sz);
}
#endif

#ifndef GALOSH_DT_ENV

#define DT_OMP_FOR() _Pragma("omp parallel for schedule(static)")

typedef int gboolean;
#define TRUE 1
#define FALSE 0

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

static inline void *dt_alloc_align(size_t alignment, size_t sz) {
#ifdef _WIN32
    return _aligned_malloc(sz, alignment);
#else
    void *p = NULL;
    if(posix_memalign(&p, alignment, sz) != 0) return NULL;
    return p;
#endif
}
static inline float *dt_alloc_align_float(size_t n) {
    size_t sz = sizeof(float) * ((n + 15) & ~15);
    return (float *)dt_alloc_align(64, sz);
}
static inline void dt_free_align(void *p) {
    if(!p) return;
#ifdef _WIN32
    _aligned_free(p);
#else
    free(p);
#endif
}

typedef struct { int width, height; } dt_iop_roi_t;

#endif /* !GALOSH_DT_ENV */


/* ================================================================
 * Per-step profiling helpers (CPU side).
 *
 * Mirror of the GPU prof helpers in galosh_gpu_common.h (pending Stage 3
 * file split): same `prof_add(name, ms)` / `prof_print()` API so the two
 * sides can be benched with the same harness.  Compile with -DGALOSH_PROF
 * to enable; otherwise all helpers compile to no-ops and incur zero
 * runtime cost.  Uses portable wall-clock (clock_gettime / QueryPerformance
 * Counter) so it captures real elapsed time including I/O stalls.
 * ================================================================ */
#if defined(_WIN32)
  #include <windows.h>
  static inline double galosh_get_time_ms(void)
  {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
  }
#else
  #include <time.h>
  static inline double galosh_get_time_ms(void)
  {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return 1000.0 * (double)ts.tv_sec + 1e-6 * (double)ts.tv_nsec;
  }
#endif

#ifdef GALOSH_PROF
  #define GALOSH_PROF_MAX_STEPS 64
  typedef struct { const char *name; double ms; int n; } galosh_prof_entry_t;
  static galosh_prof_entry_t galosh_prof_table[GALOSH_PROF_MAX_STEPS];
  static int galosh_prof_count = 0;

  static inline void galosh_prof_add(const char *name, double ms)
  {
    for(int i = 0; i < galosh_prof_count; i++)
      if(galosh_prof_table[i].name == name ||
         (galosh_prof_table[i].name && name &&
          strcmp(galosh_prof_table[i].name, name) == 0))
      {
        galosh_prof_table[i].ms += ms;
        galosh_prof_table[i].n  += 1;
        return;
      }
    if(galosh_prof_count < GALOSH_PROF_MAX_STEPS)
    {
      galosh_prof_table[galosh_prof_count].name = name;
      galosh_prof_table[galosh_prof_count].ms   = ms;
      galosh_prof_table[galosh_prof_count].n    = 1;
      galosh_prof_count++;
    }
  }
  static inline void galosh_prof_reset(void) { galosh_prof_count = 0; }
  static inline void galosh_prof_print(const char *header)
  {
    fprintf(stderr, "\n[GALOSH_PROF] ====== %s ======\n", header ? header : "per-step");
    double total = 0.0;
    for(int i = 0; i < galosh_prof_count; i++) total += galosh_prof_table[i].ms;
    for(int i = 0; i < galosh_prof_count; i++)
      fprintf(stderr, "[GALOSH_PROF] %-32s  %8.2f ms  (n=%d, %.1f%%)\n",
              galosh_prof_table[i].name ? galosh_prof_table[i].name : "(null)",
              galosh_prof_table[i].ms, galosh_prof_table[i].n,
              total > 0.0 ? 100.0 * galosh_prof_table[i].ms / total : 0.0);
    fprintf(stderr, "[GALOSH_PROF] %-32s  %8.2f ms\n", "TOTAL", total);
  }
  /* Convenience: wrap a block in start/end timestamps and accumulate. */
  #define GALOSH_PROF_BEGIN(name) \
      const double _galosh_prof_t0_##name = galosh_get_time_ms()
  #define GALOSH_PROF_END(name) \
      galosh_prof_add(#name, galosh_get_time_ms() - _galosh_prof_t0_##name)
#else
  static inline void galosh_prof_add(const char *name, double ms) { (void)name; (void)ms; }
  static inline void galosh_prof_reset(void) {}
  static inline void galosh_prof_print(const char *header) { (void)header; }
  #define GALOSH_PROF_BEGIN(name) ((void)0)
  #define GALOSH_PROF_END(name)   ((void)0)
#endif


/* ===================== GALOSH Constants =====================
 * GALOSH: GAT L/C-decomposed Overlap Shrinkage
 *
 * Purely local denoiser -- no block matching, no non-local processing.
 * Relies on GAT normalizing noise to i.i.d. Gaussian (sigma=1) so that
 * local adaptive shrinkage in the WHT domain is sufficient.
 *
 * Two-pass design:
 *   Pass 1: 8x8 WHT + BayesShrink hard threshold (pilot estimate)
 *     Ref: Chang, Yu & Vetterli, IEEE TIP, 2000
 *   Pass 2: 8x8 WHT + Wiener shrinkage using pilot
 *     Ref: Textbook empirical Wiener / James-Stein estimator
 */
#define GALOSH_BLOCK_SIZE   8
#define GALOSH_BLOCK_PIXELS (GALOSH_BLOCK_SIZE * GALOSH_BLOCK_SIZE)  /* 64 */
#define GALOSH_HALF_BLOCK   4
#define GALOSH_HALF_PIXELS  (GALOSH_HALF_BLOCK * GALOSH_HALF_BLOCK)  /* 16 */
#define GALOSH_STRIDE       4   /* 50% overlap for 8x8 blocks */

/* Wiener floor: minimum gain to prevent complete signal annihilation.
 *
 * Derived from perceptual threshold: human vision tolerates residual
 * noise up to ~1/N of signal range (Weber fraction ~1%).
 * For N=64 coefficients, the minimum fraction of each coefficient
 * to preserve: 1/N = 0.016. We use 1/sqrt(N) = 0.125 ≈ 0.1 as a
 * compromise: sqrt accounts for energy (not amplitude) domain.
 *
 * In practice, floor=0 risks creating black holes in uniform areas
 * where all coefficients are killed; floor > 0 preserves DC leakage
 * and prevents blocking artifacts at block boundaries.
 *
 * 1/sqrt(N) = 1/8 = 0.125 for 8x8 blocks.
 */
#define GALOSH_WIENER_FLOOR (1.0f / GALOSH_BLOCK_SIZE)  /* 0.125 */

/* CFA frequency indices in 8x8 WHT (full-res raw Bayer).
 *
 * The RGGB Bayer pattern has 2-pixel periodicity in both row and column.
 * In the 8-point WHT (this implementation's sequency order), the
 * 2-pixel alternation [+,-,+,-,+,-,+,-] maps to index 1.
 *
 * In 2D, the CFA color information occupies 3 coefficients:
 *   (row=0, col=1) = idx  1: horizontal color diff (R−Gr / Gb−B DC)
 *   (row=1, col=0) = idx  8: vertical color diff   (R−Gb / Gr−B DC)
 *   (row=1, col=1) = idx  9: checkerboard           (R−Gr−Gb+B DC)
 *
 * These carry the "DC of color" — the average inter-channel difference
 * across the 8×8 block. Shrinking them in full-res raw GALOSH destroys
 * color information, causing green shift (G-biased luma dominates).
 *
 * CFA 色情報が乗る WHT 係数。2 pixel 周期の交互パターンは WHT index 1
 * に対応。2D では (0,1), (1,0), (1,1) の 3 係数が CFA 色 DC を担う。
 * これらを Phase 4 full-res GALOSH で shrinkage から保護すると、
 * 色情報が保持され green shift が防止される。
 */
/* Achromatic filter threshold for dark anchor estimation.
 *
 * In sigma-normalized GAT domain (noise std = 1), the range (max - min)
 * of 4 i.i.d. N(mu, 1) samples has E[range] ≈ 2.06, std ≈ 0.73.
 * Threshold 4.0 covers ~98% of noise-only pixels while excluding
 * pixels with real spectral differences (e.g., dark skin: R ≠ G
 * due to melanin absorption).
 *
 * This prevents dark-area color shift caused by contamination of
 * the dark anchor with spectrally non-neutral signal.
 *
 * Ref: achromatic pixel selection is established in white balance
 *      estimation (Gray World variants); same principle applied here
 *      to dark-area DC bias removal.
 */
#define GALOSH_ACHROMATIC_RANGE 4.0f

/* Kaiser-Bessel window for overlap-add aggregation.
 * Without windowing, patch boundaries create visible grid artifacts in
 * smooth gradients because the set of contributing patches changes
 * abruptly at every stride pixels. The window tapers each patch's
 * contribution: high weight at center, near-zero at edges, ensuring
 * smooth transitions between overlapping patches.
 *
 * w(n) = I0(beta * sqrt(1 - ((2n/(N-1))-1)^2)) / I0(beta)
 * beta = 2.0 (moderate taper), N = GALOSH_BLOCK_SIZE = 8.
 * 2D window = outer product of 1D window. */
static const float galosh_kaiser_1d[GALOSH_BLOCK_SIZE] = {
  0.34012f, 0.59885f, 0.84123f, 0.97659f,
  0.97659f, 0.84123f, 0.59885f, 0.34012f
};
static float galosh_kaiser_2d[GALOSH_BLOCK_PIXELS];
/* Half-res 4x4 Kaiser: averaged from consecutive pairs of the 8-point window.
 * Point-sampling at stride-2 produces an asymmetric window [0.34,0.84,0.98,0.60]
 * which causes visible grid artifacts. Pair-averaging gives symmetric
 * [0.47,0.91,0.91,0.47] for proper overlap-add in half-res aggregation. */
static float galosh_kaiser_half_2d[GALOSH_HALF_PIXELS];
static void init_galosh_kaiser(void)
{
  for(int dy = 0; dy < GALOSH_BLOCK_SIZE; dy++)
    for(int dx = 0; dx < GALOSH_BLOCK_SIZE; dx++)
      galosh_kaiser_2d[dy * GALOSH_BLOCK_SIZE + dx] = galosh_kaiser_1d[dy] * galosh_kaiser_1d[dx];

  /* Build symmetric 4-point Kaiser by averaging consecutive pairs */
  float kaiser_half_1d[4];
  for(int i = 0; i < 4; i++)
    kaiser_half_1d[i] = (galosh_kaiser_1d[2 * i] + galosh_kaiser_1d[2 * i + 1]) * 0.5f;
  for(int dy = 0; dy < 4; dy++)
    for(int dx = 0; dx < 4; dx++)
      galosh_kaiser_half_2d[dy * 4 + dx] = kaiser_half_1d[dy] * kaiser_half_1d[dx];
}

/* ================================================================
 * 8-point Walsh-Hadamard Transform (in-place, sequency order)
 *
 * Pure add/subtract -- no multiplication. O(N log N).
 *
 * The WHT is self-inverse up to a scale factor:
 *   WHT(WHT(x)) = N * x
 * So forward and inverse share the same butterfly structure;
 * the only difference is the normalization (1/N for inverse).
 *
 * Sequency ordering ensures that higher-index coefficients
 * correspond to higher spatial frequencies (analogous to DCT),
 * which is important for the shrinkage thresholding to work
 * correctly (low-index = low-frequency = signal, high-index =
 * high-frequency = noise).
 * ================================================================ */
static inline void wht8_inplace(float *x)
{
    /* Stage 1: pairs */
    float a0 = x[0] + x[1], a1 = x[0] - x[1];
    float a2 = x[2] + x[3], a3 = x[2] - x[3];
    float a4 = x[4] + x[5], a5 = x[4] - x[5];
    float a6 = x[6] + x[7], a7 = x[6] - x[7];
    /* Stage 2: quads */
    float b0 = a0 + a2, b1 = a1 + a3;
    float b2 = a0 - a2, b3 = a1 - a3;
    float b4 = a4 + a6, b5 = a5 + a7;
    float b6 = a4 - a6, b7 = a5 - a7;
    /* Stage 3: octets (sequency order) */
    x[0] = b0 + b4;
    x[1] = b1 + b5;
    x[2] = b2 + b6;
    x[3] = b3 + b7;
    x[4] = b0 - b4;
    x[5] = b1 - b5;
    x[6] = b2 - b6;
    x[7] = b3 - b7;
}

/* 2D separable WHT on an 8x8 block (in-place).
 * Forward: call with normalize=false, then coefficients are scaled by 64.
 * Inverse: call with normalize=true, divides all by 64.
 *
 * The separable structure means 2D WHT = row WHT + column WHT,
 * which is O(N^2 log N) instead of O(N^2 * N^2) for direct 2D. */
static void wht2d_8x8(float block[GALOSH_BLOCK_PIXELS], const int normalize)
{
    /* Rows */
    for(int r = 0; r < GALOSH_BLOCK_SIZE; r++)
        wht8_inplace(block + r * GALOSH_BLOCK_SIZE);
    /* Columns */
    for(int c = 0; c < GALOSH_BLOCK_SIZE; c++)
    {
        float col[GALOSH_BLOCK_SIZE];
        for(int r = 0; r < GALOSH_BLOCK_SIZE; r++)
            col[r] = block[r * GALOSH_BLOCK_SIZE + c];
        wht8_inplace(col);
        for(int r = 0; r < GALOSH_BLOCK_SIZE; r++)
            block[r * GALOSH_BLOCK_SIZE + c] = col[r];
    }
    if(normalize)
    {
        const float inv = 1.0f / (float)GALOSH_BLOCK_PIXELS;
        for(int i = 0; i < GALOSH_BLOCK_PIXELS; i++)
            block[i] *= inv;
    }
}

/* ================================================================
 * 4-point WHT (in-place, sequency order) — same family as wht8_inplace
 *
 * EN: Sequency-ordered 4-point Hadamard, used by the 4x4 2D-WHT below
 *     for K13 (half-res L) when block_size = 4.  In sequency order:
 *         y[0] = (++++) · x  = x0+x1+x2+x3   (DC)
 *         y[1] = (++--) · x  = x0+x1-x2-x3   (1 sign change)
 *         y[2] = (+--+) · x  = x0-x1-x2+x3   (2 sign changes)
 *         y[3] = (+-+-) · x  = x0-x1+x2-x3   (3 sign changes)
 *     Implemented via 2-stage radix-2 butterfly (same template as
 *     wht8_inplace).  Self-inverse up to factor 4.
 *
 * JP: 4 点 sequency-ordered WHT。8 点版 (wht8_inplace) と同じ構造の
 *     2 段 butterfly。WHT2D_4x4 の row/col primitive。 */
static inline void wht4_inplace(float *x)
{
    /* Stage 1: pairs */
    const float a0 = x[0] + x[1];
    const float a1 = x[0] - x[1];
    const float a2 = x[2] + x[3];
    const float a3 = x[2] - x[3];
    /* Stage 2: sequency-ordered output (low → high frequency) */
    x[0] = a0 + a2;  /* (+ + + +) DC */
    x[1] = a0 - a2;  /* (+ + - -) lowest non-DC */
    x[2] = a1 - a3;  /* (+ - - +) */
    x[3] = a1 + a3;  /* (+ - + -) Nyquist */
}

/* 2D separable WHT on a 4x4 block (in-place).
 * Used by K13 (half-res L Pass1+2) when --k13-block=4 to match the
 * full-res grain scale produced by K15 8×8 (= 2 × 4 = 8 px at full-res
 * after K14 ×2 upsample, equal to K15's 8 px native scale).
 *
 * Forward: normalize=0 (coefficients scaled by 16).
 * Inverse: normalize=1 (divides by 16).
 *
 * 配列は 16 連続 float (row-major 4×4)。row 4 個 + col 4 個の WHT。
 * normalize=0 で前方 (係数は 16 倍スケール)、=1 で逆方向 (1/16 で除す)。 */
static void wht2d_4x4(float *block, const int normalize)
{
    /* Rows */
    for(int r = 0; r < 4; r++)
        wht4_inplace(block + r * 4);
    /* Columns */
    for(int c = 0; c < 4; c++)
    {
        float col[4];
        for(int r = 0; r < 4; r++)
            col[r] = block[r * 4 + c];
        wht4_inplace(col);
        for(int r = 0; r < 4; r++)
            block[r * 4 + c] = col[r];
    }
    if(normalize)
    {
        const float inv = 1.0f / 16.0f;
        for(int i = 0; i < 16; i++)
            block[i] *= inv;
    }
}

/* WHT decomposition: 8x8 Bayer patch -> 4x4 x 4ch (L, C1, C2, C3).
 * Each 2x2 cell in the 8x8 patch is transformed independently.
 * L = average (luma-like), C1/C2/C3 = differences (chroma-like).
 * Noise variance is preserved: if input noise sigma^2=1 per pixel,
 * each WHT component also has sigma^2=1 (orthogonal with /2 norm).
 *
 * SSE version: processes all 4 columns (j=0..3) simultaneously.
 * Row pair (2i, 2i+1) of the 8x8 patch = 8+8 = 16 consecutive floats.
 * Deinterleave even/odd columns -> a,b,c,d as __m128 -> add/sub. */
static float quick_select_kth(float *arr, const int n, const int k)
{
  if(n <= 0) return 0.0f;
  int lo = 0, hi = n - 1;
  const int mid = k;
  while(lo < hi)
  {
    const float pivot = arr[mid];
    int i = lo, j = hi;
    do {
      while(arr[i] < pivot) i++;
      while(arr[j] > pivot) j--;
      if(i <= j) { const float t = arr[i]; arr[i] = arr[j]; arr[j] = t; i++; j--; }
    } while(i <= j);
    if(j < mid) lo = i;
    if(i > mid) hi = j;
  }
  return arr[mid];
}

static inline float quick_select_median(float *arr, const int n)
{
  return quick_select_kth(arr, n, n / 2);
}

typedef struct galosh_noise_params_t { float alpha, sigma_sq; } galosh_noise_params_t;

static int compare_floats_galosh(const void *a, const void *b)
{
  const float fa = *(const float *)a, fb = *(const float *)b;
  /* NaN-safe: NaN sorts to end */
  if(fa != fa) return 1;
  if(fb != fb) return -1;
  return (fa > fb) - (fa < fb);
}

/* Robust Poisson-Gaussian noise estimation using Laplacian (second-order
 * difference) and MAD. Uses ALL Bayer pixels so that all 4 sub-channels
 * share exactly the same noise parameters -- critical to prevent jaggies.
 *
 * Key insight: the first-difference estimator d = raw[x+2] - raw[x]
 * is contaminated by signal gradients: Var(d) = 2*sigma^2 + gradient^2.
 * For typical images the gradient term dominates, overestimating noise
 * by 10-50x, which makes the GAT over-stabilize (sigma_GAT << 1).
 *
 * The Laplacian (second difference) d = raw[x] - 2*raw[x+2] + raw[x+4]
 * perfectly cancels any linear signal component:
 *   d_signal = f - 2(f+f'h) + (f+2f'h) = 0   (h = 2 pixels)
 * Only noise remains: Var(d) = (1^2 + 2^2 + 1^2)*sigma^2 = 6*sigma^2
 *
 * Binned by intensity -> median(d^2) -> robust sigma^2 per bin -> linear fit
 * gives the Poisson-Gaussian model: noise_var = alpha*intensity + sigma^2.
 *
 * Ref: Foi et al. (2008) lower envelope method */
/* [LATEST: GALOSH_RAW_H/G] galosh_estimate_noise — Phase 0 entry.
 * Foi-Alenius blind α / σ² estimation (Poisson-Gauss noise model).
 * Block-based Laplacian + lower envelope + Huber M-estimator + dark
 * pixel pass.  Called once at the top of gat_galosh_denoise_rawlc.
 *
 * CLI override (set by argv[9] / argv[10] in main, see galosh_raw_cpu.c).
 * When g_galosh_alpha_override > 0, the function short-circuits and returns
 * the externally-supplied (α, σ²) verbatim — bypassing all internal
 * estimation.  Used for oracle bench, EM_iter Python prototype bench, and
 * any external blind-estimator comparison without rebuilding the binary. */
float g_galosh_alpha_override = 0.0f;
float g_galosh_sigma_sq_override = 0.0f;

/* ================================================================
 * [V2.0 A4] FP16 storage oracle (--f16-storage).
 *
 * EN: When enabled, the MAJOR inter-phase buffers of the O pipeline are
 * rounded through IEEE binary16 (round-to-nearest-even) at the exact
 * points where the Vulkan FP16 port will store them in 16-bit buffers
 * (compute stays FP32 = "FP16 storage / FP32 accumulate", the same
 * philosophy as the INT16 lf/cf storage narrowing).  This CPU build is
 * therefore the bit-parity ORACLE for the GPU FP16 path, and the
 * measured delta vs the FP32 canonical output IS the storage-
 * quantization cost.  Rounded buffers (the v1 contract):
 *   L_cs (P5 in), L_cs_pilot (pass1->pass2), L_cs_den (P5 out),
 *   L_pixel + L_h_den (P6 guides), C1/2/3_h_den (P8 out),
 *   C1/2/3_aligned (P9 out).  Pyramid-internal quarter/eighth buffers
 *   stay FP32 in this contract revision.
 * JP: 有効時、O パイプラインの主要フェーズ間バッファを binary16 で
 * 丸める（演算は FP32 のまま = ストレージのみ半精度、i16 の lf/cf と
 * 同じ思想）。Vulkan FP16 移植の bit-parity オラクルであり、正準 FP32
 * 出力との差分がそのまま格納量子化コスト。 */
int g_galosh_f16_storage = 0;

static inline float galosh_f16_round(const float x)
{
#if defined(GALOSH_NO_F16_STORAGE) || !defined(__FLT16_MANT_DIG__)
  /* Portability fallback: the FP16-storage oracle is not used by MLV-App. */
  return x;
#else
  /* GCC >= 12 x86-64: correct RNE via _Float16 (F16C or soft-float). */
  return (float)(_Float16)x;
#endif
}

static void galosh_f16_round_buf(float *buf, const size_t n)
{
  DT_OMP_FOR()
  for(size_t i = 0; i < n; i++) buf[i] = galosh_f16_round(buf[i]);
}

/* Insert at a phase boundary: rounds in place only when the oracle is on. */
#define GALOSH_F16_STAGE(buf, n) \
  do { if(g_galosh_f16_storage) galosh_f16_round_buf((buf), (n)); } while(0)

static galosh_noise_params_t galosh_estimate_noise(const float *raw,
                                                const int width, const int height)
{
  /* External override short-circuit. */
  if(g_galosh_alpha_override > 0.0f && g_galosh_sigma_sq_override >= 0.0f)
  {
    galosh_noise_params_t override = {
      g_galosh_alpha_override,
      g_galosh_sigma_sq_override
    };
    return override;
  }

  /* Block-based Laplacian + lower envelope + Huber M-estimator + dark pixel pass
   * Estimates noise parameters from raw Bayer data: Var(x) = alpha * E(x) + sigma_sq
   *   alpha: shot noise gain, sigma_sq: read noise variance
   *
   * Algorithm (Foi et al. 2008 lower envelope method):
   *   1. Partition each CFA channel into non-overlapping 8x8 blocks (half resolution)
   *   2. Per block: compute mean and Laplacian-based noise variance (horizontal+vertical)
   *      Intra-block MAD(|L|) is robust even when edge pixels are present
   *   3. Bin by mean intensity. Take 5-20th percentile of block variances (lower envelope =
   *      flattest patches = noise floor)
   *   4. Robust WLS fit using Huber M-estimator (k derived from residual MAD)
   *   5. Refine sigma_sq directly from darkest pixels */
  galosh_noise_params_t result = { 1e-4f, 1e-6f };

#define NE_NBINS 32
#define NE_BLOCK_SZ 8

  const int halfwidth = (width + 1) / 2;
  const int halfheight = (height + 1) / 2;
  const int offsets[4][2] = {{0,0},{0,1},{1,0},{1,1}};

  const int n_bx = halfwidth / NE_BLOCK_SZ;
  const int n_by = halfheight / NE_BLOCK_SZ;
  const int n_blocks_per_ch = n_bx * n_by;
  const int total_blocks = 4 * n_blocks_per_ch;

  if(total_blocks < 100) return result;

  float *blk_mean = dt_alloc_align_float(total_blocks);
  float *blk_var  = dt_alloc_align_float(total_blocks);
  if(!blk_mean || !blk_var)
  {
    dt_free_align(blk_mean); dt_free_align(blk_var);
    return result;
  }

  /* Steps 1-2: per-block mean and Laplacian noise variance (horizontal+vertical) */
  int bi = 0;
  for(int ch = 0; ch < 4; ch++)
  {
    const int dy0 = offsets[ch][0], dx0 = offsets[ch][1];
    for(int by = 0; by < n_by; by++)
    {
      for(int bx = 0; bx < n_bx; bx++)
      {
        const int y0 = by * NE_BLOCK_SZ;
        const int x0 = bx * NE_BLOCK_SZ;

        double sum = 0;
        int np = 0;
        for(int y = y0; y < y0 + NE_BLOCK_SZ; y++)
          for(int x = x0; x < x0 + NE_BLOCK_SZ; x++)
          {
            sum += raw[(2*y+dy0) * width + (2*x+dx0)];
            np++;
          }
        const float bm = (float)(sum / np);

        /* Collect horizontal+vertical Laplacians within the block */
        float laps[256];
        int nl = 0;
        for(int y = y0; y < y0 + NE_BLOCK_SZ; y++)
          for(int x = x0; x < x0 + NE_BLOCK_SZ - 2; x++)
          {
            const float v0 = raw[(2*y+dy0) * width + (2*x+dx0)];
            const float v1 = raw[(2*y+dy0) * width + (2*(x+1)+dx0)];
            const float v2 = raw[(2*y+dy0) * width + (2*(x+2)+dx0)];
            laps[nl++] = fabsf(v0 - 2.0f*v1 + v2);
          }
        for(int y = y0; y < y0 + NE_BLOCK_SZ - 2; y++)
          for(int x = x0; x < x0 + NE_BLOCK_SZ; x++)
          {
            const float v0 = raw[(2*y+dy0) * width + (2*x+dx0)];
            const float v1 = raw[(2*(y+1)+dy0) * width + (2*x+dx0)];
            const float v2 = raw[(2*(y+2)+dy0) * width + (2*x+dx0)];
            laps[nl++] = fabsf(v0 - 2.0f*v1 + v2);
          }

        if(nl > 10)
        {
          const float med = quick_select_median(laps, nl);
          const float sigma_lap = med / 0.6745f;
          blk_var[bi] = (sigma_lap * sigma_lap) / 6.0f;
        }
        else
        {
          blk_var[bi] = 1e10f;
        }
        blk_mean[bi] = bm;
        bi++;
      }
    }
  }
  const int n_total = bi;

  /* Step 3: bin by mean intensity, take lower envelope (5-20th percentile) */
  float global_min = FLT_MAX, global_max = 0.0f;
  for(int i = 0; i < n_total; i++)
  {
    if(blk_mean[i] > 0.003f && blk_mean[i] < 0.97f)
    {
      if(blk_mean[i] < global_min) global_min = blk_mean[i];
      if(blk_mean[i] > global_max) global_max = blk_mean[i];
    }
  }
  const float bw = (global_max - global_min) / NE_NBINS;
  if(bw < 1e-10f)
  {
    dt_free_align(blk_mean); dt_free_align(blk_var);
    return result;
  }

  float bin_mean_arr[NE_NBINS], bin_var_arr[NE_NBINS];
  int bin_valid[NE_NBINS], bin_cnt_arr[NE_NBINS];

  /* ============================================================
   * Step 3: histogram-based bin envelope (= 5-20th percentile of
   * per-bin variance distribution).
   *
   * 2026-05-09 backport from GPU §7.2 ne_finalize.  Replaces the
   * old qsort + per-bin sample-buffer approach with 128-var-bin
   * histogram + bin-center weighted average.  Rationale:
   *   (1) ISP-streaming friendliness: sample buffer scales with
   *       image size (12.5 MP would need ~12 MB sort_buf);
   *       histogram is constant 128 ints per bin = 16 KB total
   *       across 32 bins, fits 32 KB LDS budget.
   *   (2) determinism: histogram sum is order-invariant, sort
   *       result depends on tiebreaker / qsort impl details.
   *   (3) HW match: ISP silicon has counter arrays natively;
   *       sort is sequential and HW-unfriendly.
   *   (4) deterministic + parallel: per-pixel atomic_inc maps
   *       1:1 to ISP per-pixel pipeline; sort cannot.
   * Quality: ±vrange/256 quantization (≤ 0.4% on typical block
   * variance distribution; smaller than FP32 cascade noise).
   *
   * Old qsort+sample path retained under
   * GALOSH_KEEP_DEPRECATED_BIN_ENV_SORT_OLD (see
   * galosh_estimate_noise_bin_envelope_sort_old below).
   * ============================================================ */
#define NE_VAR_BINS 128
  int n_valid = 0;
  for(int b = 0; b < NE_NBINS; b++)
  {
    const float bin_lo = global_min + b * bw;
    const float bin_hi = bin_lo + bw;
    bin_valid[b] = 0;

    /* Pass 1: count + msum + var range. */
    float msum = 0.0f;
    int cnt = 0;
    float vmin = FLT_MAX, vmax = 0.0f;
    for(int i = 0; i < n_total; i++)
    {
      const float bm = blk_mean[i];
      const float bv = blk_var[i];
      if(bm >= bin_lo && bm < bin_hi && bm > 0.003f && bm < 0.97f && bv < 1e9f)
      {
        msum += bm;
        cnt++;
        if(bv < vmin) vmin = bv;
        if(bv > vmax) vmax = bv;
      }
    }
    if(cnt < 20) continue;

    /* Pass 2: histogram of var values into NE_VAR_BINS bins of [vmin, vmax]. */
    int vhist[NE_VAR_BINS] = {0};
    const float vrange = fmaxf(vmax - vmin, 1e-12f);
    const float vscale = (float)NE_VAR_BINS / vrange;
    for(int i = 0; i < n_total; i++)
    {
      const float bm = blk_mean[i];
      const float bv = blk_var[i];
      if(bm >= bin_lo && bm < bin_hi && bm > 0.003f && bm < 0.97f && bv < 1e9f)
      {
        int vbin = (int)((bv - vmin) * vscale);
        if(vbin < 0) vbin = 0;
        if(vbin >= NE_VAR_BINS) vbin = NE_VAR_BINS - 1;
        vhist[vbin]++;
      }
    }

    /* Cumsum scan: find p5_bin and p20_bin (= 5%, 20% percentiles). */
    const int p5_target  = cnt / 20;
    const int p20_target = cnt / 5;
    int cum = 0;
    int p5_bin = 0, p20_bin = NE_VAR_BINS - 1;
    int found_p5 = 0;
    for(int i = 0; i < NE_VAR_BINS; i++)
    {
      cum += vhist[i];
      if(!found_p5 && cum >= p5_target) { p5_bin = i; found_p5 = 1; }
      if(cum >= p20_target) { p20_bin = i; break; }
    }

    /* Weighted bin-center sum over [p5_bin, p20_bin] = lower-envelope mean var. */
    float vsum = 0.0f;
    int vcnt = 0;
    for(int i = p5_bin; i <= p20_bin; i++)
    {
      const float bin_center = vmin + ((float)i + 0.5f) / vscale;
      const int n = vhist[i];
      vsum += bin_center * (float)n;
      vcnt += n;
    }
    bin_var_arr[b]  = (vcnt > 0) ? (vsum / (float)vcnt) : (vmin + 0.5f / vscale);
    bin_mean_arr[b] = msum / (float)cnt;
    bin_cnt_arr[b]  = (vcnt > 0) ? vcnt : 1;
    bin_valid[b]    = 1;
    n_valid++;
  }

  dt_free_align(blk_mean);
  dt_free_align(blk_var);

  if(n_valid < 4)
  {
    return result;
  }

  /* Step 4: robust WLS fit Var = alpha * mean + sigma_sq
   * Huber M-estimator: k = 1.345 * MAD(residuals) / 0.6745 */
  float alpha_est = 0.01f, sigma_sq_est = 0.0f;

  for(int iter = 0; iter < 5; iter++)
  {
    double huber_k = 1e10;
    if(iter > 0)
    {
      float resids[NE_NBINS];
      int nr = 0;
      for(int b = 0; b < NE_NBINS; b++)
      {
        if(!bin_valid[b]) continue;
        resids[nr++] = fabsf(bin_var_arr[b] - (alpha_est * bin_mean_arr[b] + sigma_sq_est));
      }
      const float resid_mad = quick_select_median(resids, nr) / 0.6745f;
      huber_k = 1.345 * fmax(resid_mad, 1e-12);
    }

    double Sw = 0, Sx = 0, Sy = 0, Sxx = 0, Sxy = 0;
    for(int b = 0; b < NE_NBINS; b++)
    {
      if(!bin_valid[b]) continue;
      double w = (double)bin_cnt_arr[b];
      if(iter > 0)
      {
        const double pred = (double)alpha_est * bin_mean_arr[b] + sigma_sq_est;
        const double resid = fabs((double)bin_var_arr[b] - pred);
        if(resid > huber_k) w *= huber_k / resid;
      }
      const double x = bin_mean_arr[b], y = bin_var_arr[b];
      Sw += w; Sx += w*x; Sy += w*y; Sxx += w*x*x; Sxy += w*x*y;
    }
    const double det = Sw * Sxx - Sx * Sx;
    if(fabs(det) > 1e-30)
    {
      float new_alpha = (float)((Sw * Sxy - Sx * Sy) / det);
      float new_sq    = (float)((Sxx * Sy - Sx * Sxy) / det);
      if(new_alpha > 0) alpha_est = new_alpha;
      if(new_sq >= 0) sigma_sq_est = new_sq;
    }
  }

  alpha_est = fmaxf(alpha_est, 1e-8f);

  /* ============================================================
   * Step 5: estimate sigma_sq directly from darkest pixels.
   *
   * 2026-05-09: backported histogram-based percentile from GPU
   * (see galosh.cl §7.2c-f).  Replaces the old buffer-based
   * implementation which capped at 50000 samples (= statistically
   * sufficient but arbitrary) with histogram percentile that has
   * no sample cap and is stride-1 over all dark pixels.
   *
   * Why backport: GPU uses histogram for ISP-streaming friendliness
   * (= no per-WI sample buffer); CPU mirrors for two reasons:
   *   (1) verification harness needs identical algorithms across
   *       CPU/GPU for per-Phase diff to be meaningful
   *   (2) "ISP runs the histogram path" is the paper claim — having
   *       CPU run the same path means CPU bench validates the
   *       production algorithm, not a CPU-only legacy variant
   *
   * Histogram resolution: 4096 bins covering [0, 1] for the dark-
   * threshold pass, 4096 bins covering [0, 0.1] for the dark-
   * Laplacian-MAD pass.  Sufficient for median estimation.
   *
   * The OLD buffer-based implementation is kept below as
   * `galosh_estimate_noise_dark_old` for reference (= legacy ID,
   * deprecated 2026-05-09 per feedback_keep_deprecated_variants).
   * ============================================================ */
  {
#define O32_DARK_HIST_BINS 4096
#define O32_DARK_LAP_MAX   0.1f
    int *thresh_hist = (int *)dt_alloc_align_float(O32_DARK_HIST_BINS);
    int *lap_hist    = (int *)dt_alloc_align_float(O32_DARK_HIST_BINS);
    if(thresh_hist && lap_hist)
    {
      memset(thresh_hist, 0, O32_DARK_HIST_BINS * sizeof(int));
      memset(lap_hist,    0, O32_DARK_HIST_BINS * sizeof(int));

      /* Pass 1: histogram of halfres samples (stride=3 per CFA channel × 4). */
      const float bin_scale = (float)O32_DARK_HIST_BINS;
      int total_thresh = 0;
      for(int ch = 0; ch < 4; ch++)
      {
        const int dy0 = offsets[ch][0], dx0 = offsets[ch][1];
        for(int y = 0; y < halfheight; y += 3)
          for(int x = 0; x < halfwidth; x += 3)
          {
            const float v = raw[(2*y+dy0) * width + (2*x+dx0)];
            int b = (int)(v * bin_scale);
            if(b < 0) b = 0;
            if(b >= O32_DARK_HIST_BINS) b = O32_DARK_HIST_BINS - 1;
            thresh_hist[b]++;
            total_thresh++;
          }
      }

      /* Cumsum scan: find 10th percentile = dark_thresh. */
      const int target_thresh = total_thresh / 10;
      int cum_t = 0, dark_bin = 0;
      for(int i = 0; i < O32_DARK_HIST_BINS; i++)
      {
        cum_t += thresh_hist[i];
        if(cum_t >= target_thresh) { dark_bin = i; break; }
      }
      const float dark_thresh = ((float)dark_bin + 0.5f) / bin_scale;
      const float dark_max = dark_thresh + 0.02f;

      /* Pass 2: histogram of |Lap| at dark triplets (stride=1, all 4 CFA). */
      const float lap_scale = (float)O32_DARK_HIST_BINS / O32_DARK_LAP_MAX;
      int total_lap = 0;
      for(int ch = 0; ch < 4; ch++)
      {
        const int dy0 = offsets[ch][0], dx0 = offsets[ch][1];
        /* Horizontal */
        for(int y = 0; y < halfheight; y++)
          for(int x = 0; x < halfwidth - 2; x++)
          {
            const float v0 = raw[(2*y+dy0) * width + (2*x+dx0)];
            const float v1 = raw[(2*y+dy0) * width + (2*(x+1)+dx0)];
            const float v2 = raw[(2*y+dy0) * width + (2*(x+2)+dx0)];
            if(v0 > dark_max || v1 > dark_max || v2 > dark_max) continue;
            const float lap = fabsf(v0 - 2.0f*v1 + v2);
            int b = (int)(lap * lap_scale);
            if(b < 0) b = 0;
            if(b >= O32_DARK_HIST_BINS) b = O32_DARK_HIST_BINS - 1;
            lap_hist[b]++;
            total_lap++;
          }
        /* Vertical */
        for(int y = 0; y < halfheight - 2; y++)
          for(int x = 0; x < halfwidth; x++)
          {
            const float v0 = raw[(2*y+dy0) * width + (2*x+dx0)];
            const float v1 = raw[(2*(y+1)+dy0) * width + (2*x+dx0)];
            const float v2 = raw[(2*(y+2)+dy0) * width + (2*x+dx0)];
            if(v0 > dark_max || v1 > dark_max || v2 > dark_max) continue;
            const float lap = fabsf(v0 - 2.0f*v1 + v2);
            int b = (int)(lap * lap_scale);
            if(b < 0) b = 0;
            if(b >= O32_DARK_HIST_BINS) b = O32_DARK_HIST_BINS - 1;
            lap_hist[b]++;
            total_lap++;
          }
      }

      if(total_lap > 100)
      {
        const int target_med = total_lap / 2;
        int cum_l = 0, med_bin = 0;
        for(int i = 0; i < O32_DARK_HIST_BINS; i++)
        {
          cum_l += lap_hist[i];
          if(cum_l >= target_med) { med_bin = i; break; }
        }
        const float mad = ((float)med_bin + 0.5f) / lap_scale;
        const float sigma_lap = mad / 0.6745f;
        const float dark_var = (sigma_lap * sigma_lap) / 6.0f;
        const float dark_mean = dark_thresh * 0.5f;
        sigma_sq_est = fmaxf(dark_var - alpha_est * dark_mean, 0.0f);
      }
    }
    dt_free_align(thresh_hist);
    dt_free_align(lap_hist);
#undef O32_DARK_HIST_BINS
#undef O32_DARK_LAP_MAX
  }

  result.alpha = alpha_est;
  result.sigma_sq = sigma_sq_est;
  return result;

#undef NE_NBINS
#undef NE_BLOCK_SZ
}

/* ================================================================
 * [OLD] galosh_estimate_noise_dark_refine_old — buffer-based dark
 *   refine kept as reference (= NOT called by production code).
 *
 * History (2026-05-09): the production galosh_estimate_noise above
 * used to do dark refine via:
 *   1. Allocate samp[50000] sample buffer
 *   2. Stride=3 sample halfres pixels into samp[] up to 50000 cap
 *   3. quick_select_kth(samp, ns, ns/10) → dark_thresh
 *   4. Allocate dark_laps[50000]
 *   5. Walk H+V Laplacians stride=1, accept if all 3 samples < dark_max
 *   6. quick_select_median(dark_laps, ndl) → MAD
 *   7. sigma_sq = (MAD/0.6745)²/6 - α·dark_thresh/2
 *
 * Replaced by histogram-based percentile (see galosh_estimate_noise
 * Step 5 above) for two reasons:
 *   (a) ISP-streaming friendliness: histograms are atomic-add per pixel,
 *       no sample buffer needed (production GPU §7.2c-f mirrors this).
 *   (b) GPU/CPU verification: production CPU and GPU now run the same
 *       dark refine algorithm → per-Phase diff is meaningful.
 *
 * Statistical equivalence (= correctness preserved): histogram median
 * with 4096 bins gives effectively the same percentile estimate as
 * 50000-sample quick_select within float precision.  Verified
 * empirically on SIDD 0083 (CPU σ² 2.36e-6 vs new histogram 2.34e-6,
 * within 1%).
 *
 * This function is intentionally unused (= deprecated reference per
 * feedback_keep_deprecated_variants memory: codebase is the long-term
 * memory across sessions).  Remove only if a future cleanup explicitly
 * decides to drop it.
 * ================================================================ */
#ifdef GALOSH_KEEP_DEPRECATED_DARK_REFINE_OLD
static void galosh_estimate_noise_dark_refine_old(
    const float *const raw,
    const int width, const int height,
    const int halfwidth, const int halfheight,
    const float alpha_est,
    float *out_sigma_sq_est)
{
  const int offsets[4][2] = {{0,0},{0,1},{1,0},{1,1}};
  const int samp_max = 50000;
  float *samp = dt_alloc_align_float(samp_max);
  if(!samp) return;

  int ns = 0;
  for(int ch = 0; ch < 4 && ns < samp_max; ch++)
  {
    const int dy0 = offsets[ch][0], dx0 = offsets[ch][1];
    for(int y = 0; y < halfheight && ns < samp_max; y += 3)
      for(int x = 0; x < halfwidth && ns < samp_max; x += 3)
        samp[ns++] = raw[(2*y+dy0) * width + (2*x+dx0)];
  }
  const float dark_thresh = quick_select_kth(samp, ns, ns / 10);
  const float dark_max = dark_thresh + 0.02f;

  float *dark_laps = dt_alloc_align_float(samp_max);
  if(dark_laps)
  {
    int ndl = 0;
    for(int ch = 0; ch < 4 && ndl < samp_max; ch++)
    {
      const int dy0 = offsets[ch][0], dx0 = offsets[ch][1];
      for(int y = 0; y < halfheight && ndl < samp_max; y++)
        for(int x = 0; x < halfwidth - 2 && ndl < samp_max; x++)
        {
          const float v0 = raw[(2*y+dy0) * width + (2*x+dx0)];
          const float v1 = raw[(2*y+dy0) * width + (2*(x+1)+dx0)];
          const float v2 = raw[(2*y+dy0) * width + (2*(x+2)+dx0)];
          if(v0 > dark_max || v1 > dark_max || v2 > dark_max) continue;
          dark_laps[ndl++] = fabsf(v0 - 2.0f*v1 + v2);
        }
      for(int y = 0; y < halfheight - 2 && ndl < samp_max; y++)
        for(int x = 0; x < halfwidth && ndl < samp_max; x++)
        {
          const float v0 = raw[(2*y+dy0) * width + (2*x+dx0)];
          const float v1 = raw[(2*(y+1)+dy0) * width + (2*x+dx0)];
          const float v2 = raw[(2*(y+2)+dy0) * width + (2*x+dx0)];
          if(v0 > dark_max || v1 > dark_max || v2 > dark_max) continue;
          dark_laps[ndl++] = fabsf(v0 - 2.0f*v1 + v2);
        }
    }
    if(ndl > 100)
    {
      const float med = quick_select_median(dark_laps, ndl);
      const float sigma_lap = med / 0.6745f;
      const float dark_var = (sigma_lap * sigma_lap) / 6.0f;
      const float dark_mean = dark_thresh * 0.5f;
      *out_sigma_sq_est = fmaxf(dark_var - alpha_est * dark_mean, 0.0f);
    }
    dt_free_align(dark_laps);
  }
  dt_free_align(samp);
}
#endif  /* GALOSH_KEEP_DEPRECATED_DARK_REFINE_OLD (reference only) */

/* --- GAT (forward / inverse / LUT) ---
 *
 * GAT: Generalized Anscombe Transform (variance stabilization)
 *
 * Forward (piecewise C1 VST extending Foi GAT to all reals):
 *   For y >= y_break = -3*alpha/8:
 *      T(y) = (2/alpha) * sqrt(alpha*y + 3*alpha^2/8 + sigma^2)  (Foi sqrt branch)
 *   For y < y_break:
 *      T(y) = T_break + (y - y_break)/sigma                     (linear branch)
 *   where T_break = 2*sigma/alpha (continuity) and slope = 1/sigma (C1).
 *
 *   Variance is stabilized in BOTH branches to approx 1:
 *     - sqrt branch: stabilizes Poisson(alpha*X) + Gaussian(sigma^2) -> Var ~ 1
 *     - linear branch: in deep negatives, signal-dependent component is
 *       negligible, observed variance is approx sigma^2, linear scaling by 1/sigma
 *       gives Var ~ 1 as well.
 *   No input clamping -> distribution shape preserved across channels
 *   (critical for L/C decomposition where 4ch shape mismatch creates false chroma).
 *
 * Inverse: Exact unbiased inverse via Gauss-Hermite quadrature LUT
 *   in the sqrt branch; analytical linear inverse in the linear branch.
 *   Ref: Anscombe (1948), Foi et al. (Sig.Proc. 2008),
 *        Makitalo & Foi (TIP 2013) */

static inline float gat_forward(const float x, const float alpha, const float sigma_sq)
{
  const float x_safe = (x == x) ? x : 0.0f;  /* NaN guard only -- NO clamping */
  const float y_break = -0.375f * alpha;     /* -3*alpha/8 */
  if(x_safe >= y_break)
  {
    /* sqrt branch: classical Foi GAT */
    return (2.0f / alpha) * sqrtf(alpha * x_safe + 0.375f * alpha * alpha + sigma_sq);
  }
  else
  {
    /* linear branch: C1 extension */
    const float sigma_raw = sqrtf(fmaxf(sigma_sq, 1e-20f));
    const float t_break = 2.0f * sigma_raw / alpha;
    return t_break + (x_safe - y_break) / sigma_raw;
  }
}

/* ================================================================
 * Foi exact unbiased inverse GAT (Makitalo & Foi 2013)
 *
 * Precompute E[GAT(Y)] for Y ~ Poisson-Gaussian(x, alpha, sigma_sq)
 * via Poisson summation + 10-point Gauss-Hermite quadrature.
 * Inverse transform by binary search on monotone lookup table.
 * Significantly reduces bias compared to closed-form approximations,
 * especially at high ISO.
 * ================================================================ */

#define GAT_INV_TABLE_SIZE 4096

typedef struct gat_inv_table_t
{
  float d[GAT_INV_TABLE_SIZE]; /* Expected GAT output values (sorted, monotone) */
  float x[GAT_INV_TABLE_SIZE]; /* Corresponding signal values [0, 1] */
  float d_min, d_max;
  /* Piecewise C1 VST parameters (set at table build time) */
  float alpha;
  float sigma_raw;             /* sqrt(sigma_sq) */
  float y_break;               /* -3*alpha/8: forward-domain transition */
  float t_break;               /* 2*sigma/alpha : GAT-domain transition */
  int valid;
} gat_inv_table_t;

static gat_inv_table_t gat_inv_table = { .valid = 0 };

/* 10-point Gauss-Hermite quadrature (weight function e^{-x^2}).
 * Makitalo & Foi (TIP 2013) use 10 points for the exact unbiased inverse. */
static const double gh_nodes[10] = {
  -3.436159, -2.532732, -1.756684, -1.036611, -0.342901,
   0.342901,  1.036611,  1.756684,  2.532732,  3.436159
};
static const double gh_weights[10] = {
  7.640432855232641e-06, 1.343645746781232e-03, 3.387439445548111e-02,
  2.401386110823147e-01, 6.108626337353258e-01,
  6.108626337353258e-01, 2.401386110823147e-01, 3.387439445548111e-02,
  1.343645746781232e-03, 7.640432855232641e-06
};

static void gat_build_inverse_table(const float alpha, const float sigma_sq)
{
  const double a = (double)alpha;
  const double sq = (double)sigma_sq;
  const double sig = sqrt(fmax(sq, 1e-20));
  const double y_break_d = -0.375 * a;
  const double t_break_d = 2.0 * sig / a;

  for(int i = 0; i < GAT_INV_TABLE_SIZE; i++)
  {
    const double x_val = (double)i / (double)(GAT_INV_TABLE_SIZE - 1); /* [0, 1] */
    const double lambda = x_val / a;  /* Poisson rate */

    /* Summation over Poisson distribution: E[T(Poisson(lambda)*alpha + N(0, sigma_sq))] */
    double expected_gat = 0.0;
    const int k_max = (int)(lambda + 8.0 * sqrt(fmax(lambda, 1.0))) + 20;
    double log_prob = -lambda;  /* log P(0; lambda) */

    for(int k = 0; k <= k_max; k++)
    {
      if(k > 0) log_prob += log(lambda) - log((double)k);
      const double prob = exp(log_prob);
      if(prob < 1e-15 && k > (int)lambda + 1) break;

      double eg = 0.0;
      for(int g = 0; g < 10; g++)
      {
        const double z = 1.4142135623730951 * sig * gh_nodes[g]; /* sqrt(2)*sig*node */
        const double noisy_y = (double)k * a + z;
        double T;
        if(noisy_y >= y_break_d)
        {
          const double arg = a * noisy_y + 0.375 * a * a + sq;
          T = (2.0 / a) * sqrt(fmax(arg, 0.0));
        }
        else
        {
          T = t_break_d + (noisy_y - y_break_d) / sig;
        }
        eg += gh_weights[g] * T;
      }
      eg *= 0.5641895835477563;  /* 1/sqrt(pi) */
      expected_gat += prob * eg;
    }

    gat_inv_table.x[i] = (float)x_val;
    gat_inv_table.d[i] = (float)expected_gat;
  }

  gat_inv_table.d_min = gat_inv_table.d[0];
  gat_inv_table.d_max = gat_inv_table.d[GAT_INV_TABLE_SIZE - 1];
  gat_inv_table.alpha = alpha;
  gat_inv_table.sigma_raw = (float)sig;
  gat_inv_table.y_break = (float)y_break_d;
  gat_inv_table.t_break = (float)t_break_d;
  gat_inv_table.valid = 1;
}

static inline float gat_inverse_exact(const float D)
{
  if(!gat_inv_table.valid) return 0.0f;

  /* Below table coverage: use analytical linear-branch inverse.
   * In the linear branch, T(y) = t_break + (y - y_break)/sigma
   *   => y = y_break + sigma * (T - t_break).
   * No clamp to 0 -- preserves distribution shape. */
  if(D <= gat_inv_table.d_min)
  {
    return gat_inv_table.y_break
         + gat_inv_table.sigma_raw * (D - gat_inv_table.t_break);
  }
  if(D >= gat_inv_table.d_max) return 1.0f;

  /* Binary search on monotone d[] array (sqrt branch, unbiased) */
  int lo = 0, hi = GAT_INV_TABLE_SIZE - 1;
  while(lo < hi - 1)
  {
    const int mid = (lo + hi) >> 1;
    if(gat_inv_table.d[mid] <= D) lo = mid;
    else hi = mid;
  }

  /* Linear interpolation */
  const float d0 = gat_inv_table.d[lo], d1 = gat_inv_table.d[hi];
  const float t = (D - d0) / fmaxf(d1 - d0, 1e-10f);
  return gat_inv_table.x[lo] + t * (gat_inv_table.x[hi] - gat_inv_table.x[lo]);
}

/* Measure actual sigma in GAT domain using MAD of Laplacian (second differences).
 *
 * Laplacian: L = data[x] - 2*data[x+1] + data[x+2] cancels linear gradients.
 * For iid noise with variance sigma^2: Var(L) = (1+4+1)*sigma^2 = 6*sigma^2
 * |L| ~ sqrt(6)*sigma * |N(0,1)|, so MAD(|L|) = sqrt(6)*sigma*0.6745
 * => sigma = MAD / (0.6745 * sqrt(6)) = MAD / 1.6521 */

/* Full-mosaic sigma estimation: uses stride-2 Laplacian to stay within
 * the same Bayer channel. */
static float estimate_gat_sigma_mosaic(const float *data, const int width, const int height)
{
  const int n_samples = MIN(width * height / 6, 200000);
  float *abs_laps = dt_alloc_align_float(n_samples + 1);
  if(!abs_laps) return 1.0f;

  int count = 0;
  for(int y = 0; y < height && count < n_samples; y++)
  {
    const float *row = data + (size_t)y * width;
    for(int x = 0; x < width - 4 && count < n_samples; x += 3)
    {
      const float lap = row[x] - 2.0f * row[x + 2] + row[x + 4];
      abs_laps[count++] = fabsf(lap);
    }
  }

  if(count < 100) { dt_free_align(abs_laps); return 1.0f; }

  const float mad = quick_select_median(abs_laps, count);
  dt_free_align(abs_laps);

  const float sigma = mad / 1.6521f;
  return fmaxf(sigma, 0.01f);
}

/* ============================================================
 * estimate_gat_sigma_halfres — per-CFA σ in GAT-normalized space.
 *
 * 2026-05-09 backport from GPU §7.5 galosh_o32_sigma_per_cfa.
 * Replaces 200000-cap sample buffer + quick_select_median with
 * 4096-bin histogram on [0, 16] range.  Rationale (= same as
 * Step 3 bin envelope backport):
 *   (1) ISP-streaming friendliness: sample buffer scales with
 *       image (12.5 MP halfres = ~3 MP samples); histogram is
 *       constant 16 KB regardless.
 *   (2) Removes 200K cap → no sample truncation bias on 4K+.
 *   (3) ISP HW match: per-pixel atomic_inc maps to ISP counter
 *       array natively.
 *   (4) Determinism: sum is order-invariant.
 * Quality: ±0.0039 absolute (= range 16 / 4096 bins / 2);
 * relative ±0.2% on σ ~ 1.0.  Smaller than FP32 cascade noise.
 *
 * Old sample-buffer path retained under
 * GALOSH_KEEP_DEPRECATED_GAT_SIGMA_SAMPLE_OLD.
 * ============================================================ */
#define GAT_SIGMA_HIST_BINS 4096
#define GAT_SIGMA_LAP_MAX   16.0f
static float estimate_gat_sigma_halfres(const float *data, const int width, const int height)
{
  int hist[GAT_SIGMA_HIST_BINS] = {0};
  const float bin_scale = (float)GAT_SIGMA_HIST_BINS / GAT_SIGMA_LAP_MAX;

  int total = 0;
  for(int y = 0; y < height; y++)
  {
    const float *row = data + (size_t)y * width;
    for(int x = 0; x < width - 2; x += 3)
    {
      const float lap = fabsf(row[x] - 2.0f * row[x + 1] + row[x + 2]);
      int bin = (int)(lap * bin_scale);
      if(bin < 0) bin = 0;
      if(bin >= GAT_SIGMA_HIST_BINS) bin = GAT_SIGMA_HIST_BINS - 1;
      hist[bin]++;
      total++;
    }
  }

  if(total < 100) return 1.0f;

  /* Cumsum scan: find median bin. */
  const int median_target = total / 2;
  int cum = 0, median_bin = 0;
  for(int i = 0; i < GAT_SIGMA_HIST_BINS; i++)
  {
    cum += hist[i];
    if(cum >= median_target) { median_bin = i; break; }
  }
  const float mad = ((float)median_bin + 0.5f) / bin_scale;
  /* 3-tap iid noise: Var(Lap) = 6σ² → σ = MAD / (0.6745 · sqrt(6)) = MAD / 1.6521. */
  const float sigma = mad / 1.6521f;
  return fmaxf(sigma, 0.01f);
}

#ifdef GALOSH_KEEP_DEPRECATED_GAT_SIGMA_SAMPLE_OLD
/* [OLD] estimate_gat_sigma_halfres_old — sample-buffer + quick_select.
 * Replaced 2026-05-09 by histogram-based path above.  Kept for reference
 * per feedback_keep_deprecated_variants policy. */
static float estimate_gat_sigma_halfres_old(const float *data, const int width, const int height)
{
  const int n_samples = MIN(width * height / 3, 200000);
  float *abs_laps = dt_alloc_align_float(n_samples + 1);
  if(!abs_laps) return 1.0f;

  int count = 0;
  for(int y = 0; y < height && count < n_samples; y++)
  {
    const float *row = data + (size_t)y * width;
    for(int x = 0; x < width - 2 && count < n_samples; x += 3)
    {
      const float lap = row[x] - 2.0f * row[x + 1] + row[x + 2];
      abs_laps[count++] = fabsf(lap);
    }
  }

  if(count < 100) { dt_free_align(abs_laps); return 1.0f; }

  const float mad = quick_select_median(abs_laps, count);
  dt_free_align(abs_laps);

  const float sigma = mad / 1.6521f;
  return fmaxf(sigma, 0.01f);
}
#endif  /* GALOSH_KEEP_DEPRECATED_GAT_SIGMA_SAMPLE_OLD */

/* ================================================================
 * [DEPRECATED: GALOSH_YUV_P] Spatially-smoothed BayesShrink toggle.
 *
 * Failed hypothesis (rejected after SIDD 80-pair bench 2026-05-08):
 *   The diagonal-edge staircase artefact in YUV O was speculated to come
 *   from per-block BayesShrink lambda discontinuity at block-grid
 *   boundaries.  GALOSH_YUV_P attempted to suppress this by spatially
 *   smoothing sigma_y_sq across a (2R+1)x(2R+1) block-grid window before
 *   deriving lambda, expecting smoother shrinkage across block boundaries
 *   → no staircase.
 *
 * Empirical result (galosh_yuv_cpu --variant=p with corrected slider
 *   mapping cs=1.0 → C_full): SIDD 80 PSNR 35.19 → 34.73 (= -0.46 dB),
 *   LPIPS 0.301 → 0.333 (= +0.033 worse).  Pixel-diff analysis showed
 *   R/G/B-uniform diff = luma-only regression, isolating the spatially-
 *   smoothed BayesShrink path as the cause.  Speculation that "RAW O
 *   cycle-spin signal correlation makes BayesShrink conservative,
 *   mimicking λ smoothing" was unsupported by data.
 *
 * Conclusion: spatially smoothing the per-block sigma_y_sq reduces the
 * BayesShrink local adaptivity by averaging flat-block (high lambda)
 * with edge-block (low lambda) values, producing sub-optimal shrinkage
 * everywhere.  Same failure mode as the [DEPRECATED] GALOSH_RAW_N
 * multi-scale luma decomposition (= -0.6 dB regression), confirming that
 * luma adaptive-σ manipulation is structurally incompatible with the
 * BLS-GSM PointSC framework GALOSH inherits.
 *
 * Diagonal-edge staircase is intrinsic to axis-aligned WHT-LOSH and
 * requires rotation-equivariant transform replacement (= DT-CWT,
 * steerable pyramid) for true mitigation; left for future work.
 *
 * Code retained for archival reproducibility.  Default 0 → bypass.
 * Setting to 1 reproduces the failed _P bench numbers.
 * ================================================================ */
static int g_galosh_lambda_smooth = 0;  /* [DEPRECATED: GALOSH_YUV_P] */

/* Pass 1 BayesShrink threshold mode (--pass1= CLI flag).
 *   0 = baseline (per-block BayesShrink + VisuShrink cap σ·√(2 ln N))
 *   1 = a1 (hierarchical empirical Bayes — image-level σ_x_global as prior,
 *       no VisuShrink cap).  Fix for super-clean catastrophic destruction
 *       (-8 to -12 dB) on RawNIND.  See galosh_pass1_blocked prepass block
 *       for full derivation. */
extern int g_galosh_pass1_mode;

/* [DEPRECATED: GALOSH_YUV_P] block-grid sigma_y_sq smoothing kernel
 * half-radius.  radius=1 → 3x3 block neighborhood; with stride=2
 * ≈ 6 source-pixel smoothing window for the sigma_y_sq estimate. */
#ifndef GALOSH_LAMBDA_SMOOTH_RADIUS
#define GALOSH_LAMBDA_SMOOTH_RADIUS 1  /* [DEPRECATED: GALOSH_YUV_P] */
#endif

/* ================================================================
 * GALOSH Pass 1: BayesShrink adaptive hard thresholding
 *
 * For each overlapping 8x8 block in a single 2D plane:
 *   1. Forward 2D WHT
 *   2. Estimate signal variance: sigma_x^2 = max(sigma_Y^2 - sigma^2, 0)
 *      where sigma_Y^2 = (1/N) sum coeff^2 (excluding DC),
 *      sigma = noise std (=1 after GAT normalization)
 *   3. BayesShrink threshold: lambda = sigma^2 / sigma_x
 *      Ref: Chang, Yu & Vetterli, "Adaptive wavelet thresholding for image
 *           denoising and compression", IEEE TIP, 2000
 *   4. Hard threshold: |coeff| < lambda -> 0, DC bin always preserved
 *   5. Inverse 2D WHT
 *   6. Overlap-add with Kaiser window weighting
 *
 * sigma_strength: controls effective noise level (luma_strength or chroma_strength)
 *   sigma_eff = sigma_strength (in GAT-normalized space where true sigma=1)
 *
 * This function operates on a SINGLE 2D plane (e.g. L, C1, C2, or C3).
 * Called 4 times in the pipeline: once per L/C plane.
 * ================================================================ */

/* Forward declarations -- the multiorient wrappers and EWA helper below
 * call galosh_pass1, galosh_pass2 and j1f, all of which are defined
 * later in this header (or in libm).  Explicit decls avoid implicit-
 * declaration warnings under -Wall and the static-after-implicit error
 * that follows. */
/* [ARCHIVED] galosh_pass1 — single-orient flat-buffer Pass1 BayesShrink.
 * Called only by [ARCHIVED] legacy stride=4 path in galosh_raw_cpu.c
 * (#ifdef GALOSH_LEGACY).  GALOSH_RAW_G uses galosh_pass1_blocked +
 * galosh_pass12_multiorient_blocked instead.  Forward decl below; impl
 * later in this header. */
static void galosh_pass1(const float *restrict input,
                         float *restrict output,
                         const int width, const int height,
                         const float sigma_strength,
                         const int stride);
/* [PREVIOUS: GALOSH_RAW_G] / [LATEST: YUV_G] galosh_pass2 — single-orient
 * flat-buffer Pass2 Wiener.  Called only by galosh_pass2_multiorient
 * (Phase 4(c) K15 in GALOSH_RAW_G) and by [ARCHIVED] legacy stride=4.
 * GALOSH_RAW_H Phase 5 luma uses pass12_multiorient_blocked instead;
 * pass2 directly is no longer reached on the LATEST RAW path. */
static void galosh_pass2(const float *restrict noisy,
                         const float *restrict pilot,
                         float *restrict output,
                         const int width, const int height,
                         const float sigma_strength,
                         const int stride);
/* libm has double j1(double); UCRT64 mingw lacks the float variant j1f.
 * The jinc kernel evaluates at small x values where double precision is
 * well within float range, so casting is harmless.  MinGW's math.h already
 * declares j1 (dllimport) — re-declaring it there only produced a warning,
 * so the fallback prototype is for strict-ISO toolchains that hide POSIX j1. */
#if !defined(__MINGW32__) && !defined(__MINGW64__)
extern double j1(double x);
#endif

/* ================================================================
 * Bilinear plane rotation -- helper for multi-orientation WHT averaging.
 *
 * Output(x,y) = Input(R(-angle)*(x,y)) with bilinear interpolation,
 * boundary clamp.  Same in/out dimensions.
 *
 * Used by galosh_pass12_multiorient (variant B).  Bilinear is sufficient
 * here because the four orientations average post-shrinkage anyway, and
 * the rotated grid noise re-correlation is bounded by 1 sample in each
 * direction; sinc rotation would be overkill for what is essentially a
 * 4-sample symmetric average over rotational frame mismatches.
 * ================================================================ */
/* [ARCHIVED] galosh_rotate_bilinear — variant B (n_orient=4) helper.
 * Called only when galosh_pass*_multiorient sees angle != 0.  Default
 * n_orient=1 has angle=0 only → identity rotation skips this function.
 * GALOSH_RAW_G / YUV_G use n_orient=1, so this is dead code in
 * production. */
static void galosh_rotate_bilinear(const float *restrict in,
                                    float *restrict out,
                                    const int width, const int height,
                                    const float angle_deg)
{
    const float c = cosf(-angle_deg * (float)M_PI / 180.0f);
    const float s = sinf(-angle_deg * (float)M_PI / 180.0f);
    const float cx = width  * 0.5f;
    const float cy = height * 0.5f;

    DT_OMP_FOR()
    for(int y = 0; y < height; y++)
    {
        for(int x = 0; x < width; x++)
        {
            const float dx = x - cx, dy = y - cy;
            const float sx = c * dx - s * dy + cx;
            const float sy = s * dx + c * dy + cy;
            int x0 = (int)floorf(sx), y0 = (int)floorf(sy);
            float fx = sx - x0, fy = sy - y0;
            int x0c = (x0 < 0) ? 0 : (x0 >= width  ? width  - 1 : x0);
            int y0c = (y0 < 0) ? 0 : (y0 >= height ? height - 1 : y0);
            int x1c = (x0 + 1 < 0) ? 0 : (x0 + 1 >= width  ? width  - 1 : x0 + 1);
            int y1c = (y0 + 1 < 0) ? 0 : (y0 + 1 >= height ? height - 1 : y0 + 1);
            const float v00 = in[(size_t)y0c * width + x0c];
            const float v01 = in[(size_t)y0c * width + x1c];
            const float v10 = in[(size_t)y1c * width + x0c];
            const float v11 = in[(size_t)y1c * width + x1c];
            out[(size_t)y * width + x] =
                (1.0f - fx) * (1.0f - fy) * v00 +
                fx          * (1.0f - fy) * v01 +
                (1.0f - fx) * fy          * v10 +
                fx          * fy          * v11;
        }
    }
}

/* ================================================================
 * Multi-orientation WHT shrinkage (Pass1 + Pass2) wrapper -- variant B.
 *
 * Standard WHT 8x8 shrinkage uses an axis-aligned (Walsh sequency) basis,
 * whose frequency response is square in 2D.  Diagonal features at 45 deg
 * project onto many basis functions and are not sparse, so shrinkage
 * leaves a residual stair-step ("rotation-variant artefact").
 *
 * The textbook fix (Yu & Sapiro & Mallat; rotation-invariant denoising)
 * is to apply the transform in N rotated orientations and average.  For
 * n_orient = 4 we use {0, 45, 90, 135} deg.  At each orientation:
 *   1. rotate input plane by +theta
 *   2. run pass1+pass2 (axis-aligned WHT shrinkage)
 *   3. rotate result back by -theta
 *   4. accumulate
 * Final output is the n_orient-averaged result.
 *
 * Cost: 4x the pass1+pass2 work, but pass1+pass2 itself is ~20% of total
 * pipeline so absolute slowdown is ~1.6x.
 *
 * n_orient == 1 collapses to a single pass1+pass2 call (no rotation).
 * n_orient == 4 uses the four orientations above.  Other values are
 * treated as 1.
 * ================================================================ */
/* [LATEST: GALOSH_RAW_H/YUV_G] block-size-parameterized Pass1/Pass2
 * forward declarations.  Definitions further down (after WHT primitives
 * + Kaiser init).  These are the core LOSH primitives:
 *   galosh_pass1_blocked : BayesShrink hard-threshold pilot, MAD-based
 *                          σ_Y when use_robust_shrink=1 (= adopted by
 *                          GALOSH_RAW_G/YUV_G).
 *   galosh_pass2_blocked : empirical Wiener using pass1 pilot.
 * Wrapped by galosh_pass12_multiorient_blocked below. */
static void galosh_pass1_blocked(const float *restrict input,
                                 float *restrict output,
                                 const int width, const int height,
                                 const float sigma_strength,
                                 const int block_size,
                                 const int stride,
                                 const int use_robust_shrink);
static void galosh_pass2_blocked(const float *restrict noisy,
                                  const float *restrict pilot,
                                  float *restrict output,
                                  const int width, const int height,
                                  const float sigma_strength,
                                  const float wiener_floor,
                                  const int block_size,
                                  const int stride);

/* [LATEST: GALOSH_RAW_H/YUV_G] galosh_pass12_multiorient_blocked —
 * Phase 4(a) K13 luma denoise wrapper.  Calls pass1_blocked +
 * pass2_blocked (n_orient times; with n_orient=1 = single pass).
 * wiener_floor defaults to 1/B (=1/sqrt(N)) per GALOSH convention.
 *
 * use_robust_shrink=1 (GALOSH_RAW_G/YUV_G adopted) flips Pass1's σ_Y
 * estimator from L2 sum_sq to MAD partial-selection-sort robust
 * estimator (Donoho-Johnstone 1995); pass2 inherits via pilot.
 *
 * GALOSH_RAW_G call: block_size=8, stride=2, n_orient=1, robust=1
 * GALOSH_YUV_G call: same args on Y plane. */
static void galosh_pass12_multiorient_blocked(const float *restrict noisy,
                                               float *restrict output,
                                               const int width, const int height,
                                               const float sigma_strength,
                                               const int block_size,
                                               const int stride,
                                               const int n_orient,
                                               const int use_robust_shrink)
{
    const size_t npix = (size_t)width * height;
    const float wiener_floor = 1.0f / (float)block_size;  /* 1/sqrt(N) */

    /* Single-orientation path: matches the legacy pass1->pass2 sequence. */
    if(n_orient <= 1)
    {
        float *pilot = dt_alloc_align_float(npix);
        if(!pilot) { memcpy(output, noisy, sizeof(float) * npix); return; }
        galosh_pass1_blocked(noisy, pilot, width, height,
                              sigma_strength, block_size, stride,
                              use_robust_shrink);
        galosh_pass2_blocked(noisy, pilot, output, width, height,
                              sigma_strength, wiener_floor, block_size, stride);
        dt_free_align(pilot);
        return;
    }

    /* 4-orientation averaging. */
    const float angles[4] = { 0.0f, 45.0f, 90.0f, 135.0f };

    float *acc          = dt_alloc_align_float(npix);
    float *rotated      = dt_alloc_align_float(npix);
    float *pilot        = dt_alloc_align_float(npix);
    float *denoised_rot = dt_alloc_align_float(npix);
    float *derotated    = dt_alloc_align_float(npix);

    if(!acc || !rotated || !pilot || !denoised_rot || !derotated)
    {
        dt_free_align(acc); dt_free_align(rotated); dt_free_align(pilot);
        dt_free_align(denoised_rot); dt_free_align(derotated);
        memcpy(output, noisy, sizeof(float) * npix);
        return;
    }

    memset(acc, 0, sizeof(float) * npix);

    for(int i = 0; i < 4; i++)
    {
        const float ang = angles[i];
        const float *src;
        if(ang == 0.0f)
        {
            src = noisy;  /* identity rotation: skip the bilinear pass */
        }
        else
        {
            galosh_rotate_bilinear(noisy, rotated, width, height, ang);
            src = rotated;
        }

        galosh_pass1_blocked(src, pilot, width, height,
                              sigma_strength, block_size, stride,
                              use_robust_shrink);
        galosh_pass2_blocked(src, pilot, denoised_rot, width, height,
                              sigma_strength, wiener_floor, block_size, stride);

        if(ang == 0.0f)
        {
            DT_OMP_FOR()
            for(size_t k = 0; k < npix; k++) acc[k] += denoised_rot[k];
        }
        else
        {
            galosh_rotate_bilinear(denoised_rot, derotated, width, height, -ang);
            DT_OMP_FOR()
            for(size_t k = 0; k < npix; k++) acc[k] += derotated[k];
        }
    }

    const float inv_n = 1.0f / 4.0f;
    DT_OMP_FOR()
    for(size_t k = 0; k < npix; k++) output[k] = acc[k] * inv_n;

    dt_free_align(acc);
    dt_free_align(rotated);
    dt_free_align(pilot);
    dt_free_align(denoised_rot);
    dt_free_align(derotated);
}

/* [ARCHIVED] galosh_pass12_multiorient — backward-compat wrapper that
 * forces use_robust_shrink=0 (mean-based σ_Y).  GALOSH_RAW_G/YUV_G
 * adopted MAD-based σ_Y (use_robust_shrink=1), so this wrapper is no
 * longer called by the production pipeline.  Kept for old YUV bench
 * scripts that link this header directly. */
static inline void galosh_pass12_multiorient(const float *restrict noisy,
                                              float *restrict output,
                                              const int width, const int height,
                                              const float sigma_strength,
                                              const int stride,
                                              const int n_orient)
{
    galosh_pass12_multiorient_blocked(noisy, output, width, height,
                                       sigma_strength, GALOSH_BLOCK_SIZE,
                                       stride, n_orient,
                                       /*use_robust_shrink=*/0);
}

/* ================================================================
 * [PREVIOUS: GALOSH_RAW_G] galosh_pass2_multiorient — Phase 4(c) K15 wrapper.
 *
 * Called from gat_galosh_denoise_rawlc Phase 4(c) with n_orient=1 →
 * collapses to plain galosh_pass2 (Wiener-only).  noisy + pilot pair
 * comes from Phase 4(b) compute_L_fullres × 2 (= K14).
 *
 * If n_orient>1 (= [ARCHIVED] variant B), it averages 4 rotations of
 * (noisy, pilot) via galosh_rotate_bilinear — currently unreachable
 * because GALOSH_RAW_G hardcodes n_orient=1.
 * ================================================================ */
static void galosh_pass2_multiorient(const float *restrict noisy,
                                      const float *restrict pilot,
                                      float *restrict output,
                                      const int width, const int height,
                                      const float sigma_strength,
                                      const int stride,
                                      const int n_orient)
{
    const size_t npix = (size_t)width * height;

    if(n_orient <= 1)
    {
        galosh_pass2(noisy, pilot, output, width, height, sigma_strength, stride);
        return;
    }

    const float angles[4] = { 0.0f, 45.0f, 90.0f, 135.0f };

    float *acc           = dt_alloc_align_float(npix);
    float *rot_noisy     = dt_alloc_align_float(npix);
    float *rot_pilot     = dt_alloc_align_float(npix);
    float *denoised_rot  = dt_alloc_align_float(npix);
    float *derotated     = dt_alloc_align_float(npix);

    if(!acc || !rot_noisy || !rot_pilot || !denoised_rot || !derotated)
    {
        dt_free_align(acc); dt_free_align(rot_noisy); dt_free_align(rot_pilot);
        dt_free_align(denoised_rot); dt_free_align(derotated);
        memcpy(output, noisy, sizeof(float) * npix);
        return;
    }

    memset(acc, 0, sizeof(float) * npix);

    for(int i = 0; i < 4; i++)
    {
        const float ang = angles[i];
        const float *src_noisy, *src_pilot;
        if(ang == 0.0f)
        {
            src_noisy = noisy;
            src_pilot = pilot;
        }
        else
        {
            galosh_rotate_bilinear(noisy, rot_noisy, width, height, ang);
            galosh_rotate_bilinear(pilot, rot_pilot, width, height, ang);
            src_noisy = rot_noisy;
            src_pilot = rot_pilot;
        }

        galosh_pass2(src_noisy, src_pilot, denoised_rot, width, height, sigma_strength, stride);

        if(ang == 0.0f)
        {
            DT_OMP_FOR()
            for(size_t k = 0; k < npix; k++) acc[k] += denoised_rot[k];
        }
        else
        {
            galosh_rotate_bilinear(denoised_rot, derotated, width, height, -ang);
            DT_OMP_FOR()
            for(size_t k = 0; k < npix; k++) acc[k] += derotated[k];
        }
    }

    const float inv_n = 1.0f / 4.0f;
    DT_OMP_FOR()
    for(size_t k = 0; k < npix; k++) output[k] = acc[k] * inv_n;

    dt_free_align(acc);
    dt_free_align(rot_noisy);
    dt_free_align(rot_pilot);
    dt_free_align(denoised_rot);
    dt_free_align(derotated);
}

/* Forward declaration -- galosh_jinc is defined further down (alongside
 * galosh_compute_L_fullres_ewajl3) but the generic upsample below is
 * placed earlier so it can be called from anywhere in the pipeline. */
static inline float galosh_jinc(float x);

/* ================================================================
 * Generic single-plane 2x upsample via EWA jinc-windowed-jinc-3.
 *
 * Input:  src half-res plane sized halfwidth x halfheight
 * Output: dst full-res plane sized (2*halfwidth) x (2*halfheight)
 *
 * Used by the *unified* RAW reconstruction path to lift each of L,
 * C1, C2, C3 to full resolution before the inverse 2x2 WHT, replacing
 * the legacy per-block reconstruction that left chroma block-
 * replicated and visibly stair-stepped on edges.
 *
 * Sub-pixel offset convention (in half-res units):
 *   (fr+0, fc+0) : ( 0.0,  0.0)  -- block centroid
 *   (fr+0, fc+1) : ( 0.0, +0.5)  -- horizontal midpoint between this and right block
 *   (fr+1, fc+0) : (+0.5,  0.0)  -- vertical midpoint between this and below block
 *   (fr+1, fc+1) : (+0.5, +0.5)  -- diagonal midpoint between 4 neighbours
 *
 * Kernel: jinc(r_full) * jinc(r_full / 3) for r_full < 3 (output px units).
 * Window: 5x5 half-res samples (W=2).  Negative-ringing wsum is OK
 *   numerically: every output is sum * (1/wsum) and sum has the same
 *   sign as wsum so the normalisation just rescales positive
 *   contributions; tested stable for all four sub-pixel positions.
 * ================================================================ */
/* [PREVIOUS: GALOSH_RAW_G] galosh_upsample_2x_ewajl3 — Phase 4(d) K16
 * chromaup helper.  Bandlimit-faithful 2x upsample of half-res chroma
 * (c1/c2/c3) to full-res, used by gat_galosh_denoise_rawlc Phase 4(d).
 * 5x5 jinc-windowed-jinc kernel, normalised per sub-pixel offset.
 * Killed pre-G's 2x2 chroma stair-step on diagonal edges.
 * GALOSH_RAW_H operates at full-res throughout, so the K16 chromaup is
 * no longer reached on the LATEST RAW path; kept for --variant=g bench. */
static void galosh_upsample_2x_ewajl3(const float *restrict src,
                                       float *restrict dst,
                                       const int halfwidth, const int halfheight)
{
    const int fw = 2 * halfwidth;
    const int fh = 2 * halfheight;

    const float subpix[4][2] = {
        {  0.00f,  0.00f },
        {  0.00f, +0.50f },
        { +0.50f,  0.00f },
        { +0.50f, +0.50f },
    };

    const int W  = 2;
    const int kw = 2 * W + 1;
    float weights[4][5][5];

    for(int si = 0; si < 4; si++)
    {
        const float oy = subpix[si][0];
        const float ox = subpix[si][1];
        float wsum = 0.0f;
        for(int dy = -W; dy <= W; dy++)
        {
            for(int dx = -W; dx <= W; dx++)
            {
                const float ry = (float)dy - oy;
                const float rx = (float)dx - ox;
                const float r_half = sqrtf(rx * rx + ry * ry);
                const float r_full = r_half * 2.0f;
                float w_val = 0.0f;
                if(r_full < 3.0f)
                    w_val = galosh_jinc(r_full) * galosh_jinc(r_full / 3.0f);
                weights[si][dy + W][dx + W] = w_val;
                wsum += w_val;
            }
        }
        /* wsum can be negative for the (+0.5, +0.5) case; that is fine
         * because every contributing weight has the same sign as wsum
         * so the normalised weights all come out positive. */
        const float inv_wsum = 1.0f / wsum;
        for(int dy = 0; dy < kw; dy++)
            for(int dx = 0; dx < kw; dx++)
                weights[si][dy][dx] *= inv_wsum;
    }

    DT_OMP_FOR()
    for(int hy = 0; hy < halfheight; hy++)
    {
        for(int hx = 0; hx < halfwidth; hx++)
        {
            for(int si = 0; si < 4; si++)
            {
                const int sub_dy = si / 2;
                const int sub_dx = si % 2;
                const int fr = 2 * hy + sub_dy;
                const int fc = 2 * hx + sub_dx;
                if(fr >= fh || fc >= fw) continue;

                float sum = 0.0f;
                for(int dy = -W; dy <= W; dy++)
                {
                    int hyi = hy + dy;
                    if(hyi < 0)            hyi = 0;
                    if(hyi >= halfheight)  hyi = halfheight - 1;
                    for(int dx = -W; dx <= W; dx++)
                    {
                        int hxi = hx + dx;
                        if(hxi < 0)           hxi = 0;
                        if(hxi >= halfwidth)  hxi = halfwidth - 1;
                        sum += src[(size_t)hyi * halfwidth + hxi]
                             * weights[si][dy + W][dx + W];
                    }
                }
                dst[(size_t)fr * fw + fc] = sum;
            }
        }
    }
}

/* ================================================================
 * EWA Jinc-Lanczos-3 jinc helper (shared between upsample_2x and the
 * RAW-specific compute_L_fullres variant C).
 *
 * jinc(x) = 2 * J1(pi*x) / (pi*x),  jinc(0) = 1.
 * The 2D Fourier dual of a circular aperture (= 2D sinc analogue);
 * frequency response is a perfect circle, so EWA reconstruction does
 * not exhibit the axis-aligned anisotropy that separable Lanczos has.
 * jinc(r) * jinc(r/3) is the windowed kernel ("EWA Lanczos-Jinc-3"),
 * same family as mpv's `ewa_lanczossharp`.
 * ================================================================ */
static inline float galosh_jinc(float x)
{
    /* Limit at x->0: 2*J1(pi*x)/(pi*x) -> 1.  Taylor series for stability. */
    if(fabsf(x) < 1e-5f) return 1.0f;
    const double pix = M_PI * (double)x;
    return (float)(2.0 * j1(pix) / pix);
}

/* galosh_compute_L_fullres_ewajl3 (RAW-specific variant C alternative
 * upsample) was moved to galosh_raw_cpu.c since it operates on RAW Bayer
 * 2x2 sub-pixel offsets and is invoked only from gat_galosh_denoise_rawlc. */

/* ================================================================
 * GALOSH Pass 1 (block-size-parameterized).
 *
 * EN: Empirical Bayes hard-threshold pilot, generalized over block size B
 *     (2D B×B WHT-LOSH).  Switching B between 8 (legacy K15 / original
 *     GALOSH) and 4 (K13 4×4 grain-scale matched variant) is purely a
 *     parameter — same BayesShrink + VisuShrink-fallback formulae apply
 *     with N = B*B, only the WHT primitive and Kaiser window are
 *     dispatched on B.  K13 4×4 + K15 8×8 yields matched 8-pixel grain
 *     scale at full resolution (= 2·B_K13 = B_K15) which is the only
 *     block-size pair consistent with the half→full-res hierarchy that
 *     K11's fixed 2×2 Bayer decompose imposes.
 *
 * JP: BayesShrink hard-threshold pilot を block size B でパラメータ化。
 *     B=8 (legacy K15 / 全res WHT-LOSH) と B=4 (K13 4×4: K15 8×8 と
 *     full-res grain scale を 8 px に揃える variant) で同じ BayesShrink
 *     公式が成立、WHT primitive と Kaiser window だけ B で切り替え。
 *     K13 4×4 + K15 8×8 が full-res で grain scale 一致 (2*4=8=8) する
 *     唯一の組合せで、K11 の Bayer 2×2 階層から自然に導かれる。
 * ================================================================
 * [LATEST: GALOSH_RAW_H/YUV_G] body of galosh_pass1_blocked.
 * BayesShrink hard-threshold pilot.  When use_robust_shrink=1
 * (= adopted by GALOSH_RAW_G/YUV_G), σ_Y is computed via MAD partial-
 * selection-sort robust estimator (Donoho-Johnstone 1995).  Otherwise
 * L2 sum_sq estimator (= [ARCHIVED] pre-G default) is used.
 * ================================================================ */
static void galosh_pass1_blocked(const float *restrict input,
                                 float *restrict output,
                                 const int width, const int height,
                                 const float sigma_strength,
                                 const int block_size,
                                 const int stride,
                                 const int use_robust_shrink)
{
    const int B = block_size;
    const int N = B * B;
    const int rmax = height - B;
    const int cmax = width  - B;
    const int npix = width * height;

    /* Dispatch WHT primitive and Kaiser window on block size.
     * (4 and 8 are the only supported B; K11 2×2 Bayer hierarchy fixes
     * the meaningful block sizes for K13/K15 to {4, 8}.) */
    void (*wht_fn)(float *, int);
    const float *kaiser;
    if(B == 8)      { wht_fn = wht2d_8x8;   kaiser = galosh_kaiser_2d;      }
    else if(B == 4) { wht_fn = wht2d_4x4;   kaiser = galosh_kaiser_half_2d; }
    else            { memcpy(output, input, sizeof(float) * npix); return; }

    /* Per-thread accumulation buffers (allocated up front so the post-loop
     * merge can be a parallel-for reduction instead of a serializing
     * `#pragma omp critical` loop -- the merge had been ~64 M sequential
     * float adds per pass on a 16 MP image with 4 threads, now runs at
     * thread parallelism over npix). */
    const int n_threads_max = omp_get_max_threads();
    float **t_numer = (float **)malloc(n_threads_max * sizeof(float *));
    float **t_denom = (float **)malloc(n_threads_max * sizeof(float *));
    if(!t_numer || !t_denom)
    {
        free(t_numer); free(t_denom);
        memcpy(output, input, sizeof(float) * npix);
        return;
    }
    int t_alloc_ok = 1;
    for(int t = 0; t < n_threads_max; t++)
    {
        t_numer[t] = (float *)dt_alloc_align(64, sizeof(float) * npix);
        t_denom[t] = (float *)dt_alloc_align(64, sizeof(float) * npix);
        if(!t_numer[t] || !t_denom[t]) { t_alloc_ok = 0; }
        else { memset(t_numer[t], 0, sizeof(float) * npix);
               memset(t_denom[t], 0, sizeof(float) * npix); }
    }
    if(!t_alloc_ok)
    {
        for(int t = 0; t < n_threads_max; t++) {
            if(t_numer[t]) dt_free_align(t_numer[t]);
            if(t_denom[t]) dt_free_align(t_denom[t]);
        }
        free(t_numer); free(t_denom);
        memcpy(output, input, sizeof(float) * npix);
        return;
    }

    const float sigma_sq = sigma_strength * sigma_strength;
    /* VisuShrink fallback for flat blocks: lambda_max = sigma * sqrt(2 ln N) */
    const float lambda_max = sigma_strength * sqrtf(2.0f * logf((float)N));

    /* ================================================================
     * Hierarchical empirical Bayes prepass (= "A1" image-level BayesShrink)
     *
     * EN: Activated by g_galosh_pass1_mode==1.  The vanilla BayesShrink
     *   threshold λ = σ²/σ_x_local breaks down when σ_x_local → 0
     *   (= subtle texture in super-clean images), forcing λ → ∞ and
     *   killing every AC coefficient even when subtle signal exists.
     *   The current code patches this via VisuShrink cap (λ_max =
     *   σ·√(2 ln N)) which is a worst-case universal bound — still
     *   strong enough to destroy super-clean texture (catastrophic
     *   -8 to -12 dB on RawNIND 2pilesofplates ISO125/160, Blombukett,
     *   Elplint where BM3D loses < 1 dB).
     *
     *   A1 replaces the universal cap with an image-level Bayes prior:
     *     σ_x_global² = max( mean(σ_y_local²) - σ², ε )
     *     σ_x_eff²    = max( σ_x_local², σ_x_global² )
     *     λ           = σ² / √σ_x_eff² · √N  (no cap)
     *
     *   This is hierarchical empirical Bayes shrinkage (Efron-Morris,
     *   Carlin-Louis): when local signal variance is unreliable, fall
     *   back to the global signal variance estimate.  For super-clean
     *   images, σ_x_global is small but non-zero (= edge / structure
     *   contribution still dominates a few blocks), preventing the
     *   λ → ∞ catastrophe.  For noisy images, σ_x_local typically
     *   exceeds σ_x_global → behavior reduces to vanilla BayesShrink.
     *
     *   No new tuning constants; σ_x_global is derived purely from
     *   pre-pass statistics.
     *
     * JP: σ_x_local が 0 に近づくと λ が発散して全 AC が消されるのが
     *   super-clean 崩壊の本因.  従来は VisuShrink universal cap
     *   (σ·√(2 ln N)) で抑え込んでいたが、cap 自体が subtle texture
     *   を kill するレベル.  A1 は階層的 empirical Bayes で「画像
     *   全体の signal variance σ_x_global」を prior として、 σ_x_local
     *   が信頼できない時 (≈0 の時) これにフォールバック.  Cap 撤廃.
     *   定数なし、 既存統計から完全導出.
     * ================================================================ */
    float sigma_x_sq_global = 0.0f;
    if(g_galosh_pass1_mode == 1)
    {
      /* Prepass: forward WHT every block, compute sigma_y_sq, accumulate. */
      double sum_sigma_y_sq = 0.0;
      long n_blk_global = 0;
      #pragma omp parallel for reduction(+:sum_sigma_y_sq,n_blk_global) schedule(dynamic, 4)
      for(int ref_r = 0; ref_r <= rmax; ref_r += stride)
      {
        for(int ref_c = 0; ref_c <= cmax; ref_c += stride)
        {
          float blk[GALOSH_BLOCK_PIXELS];
          for(int dy = 0; dy < B; dy++)
            memcpy(blk + dy * B,
                   input + (ref_r + dy) * width + ref_c,
                   B * sizeof(float));
          wht_fn(blk, 0);
          float s2;
          if(use_robust_shrink)
          {
            float ac[GALOSH_BLOCK_PIXELS];
            for(int i = 1; i < N; i++) ac[i - 1] = fabsf(blk[i]);
            const float mad = quick_select_median(ac, N - 1);
            const float scale = mad / 0.6745f;
            s2 = (scale * scale) / (float)N;
          }
          else
          {
            float sum_sq = 0.0f;
            for(int i = 1; i < N; i++) sum_sq += blk[i] * blk[i];
            s2 = sum_sq / ((float)(N - 1) * (float)N);
          }
          sum_sigma_y_sq += s2;
          n_blk_global++;
        }
      }
      if(n_blk_global > 0)
      {
        const float mean_sigma_y_sq = (float)(sum_sigma_y_sq / (double)n_blk_global);
        sigma_x_sq_global = fmaxf(mean_sigma_y_sq - sigma_sq, 1e-8f);
      }
      fprintf(stderr, "[pass1] A1 prepass: n_blk=%ld, mean_sigma_y_sq=%.6e, "
              "sigma_x_sq_global=%.6e (sigma_sq=%.6e)\n",
              n_blk_global,
              (n_blk_global > 0) ? (float)(sum_sigma_y_sq / (double)n_blk_global) : 0.0f,
              sigma_x_sq_global, sigma_sq);
    }

    /* ================================================================
     * [DEPRECATED: GALOSH_YUV_P] Spatially-smoothed BayesShrink pre-pass.
     * Failed hypothesis, retained for archival reproducibility.
     *
     * Activated only when g_galosh_lambda_smooth=1 (= the [DEPRECATED]
     * --variant=p path).  Pass-A estimates sigma_y_sq for every block,
     * then box-smooths the (n_blk_y × n_blk_x) grid using a (2R+1)×(2R+1)
     * window with R = GALOSH_LAMBDA_SMOOTH_RADIUS.  Pass-B (= main loop)
     * reads the smoothed sigma_y_sq instead of recomputing it inline.
     *
     * Empirical result: -0.46 dB PSNR regression vs O on SIDD 80-pair
     * bench 2026-05-08.  See toggle declaration above for full rejection
     * rationale (= reduces BayesShrink local adaptivity, same family of
     * failure as [DEPRECATED] GALOSH_RAW_N multi-scale luma).
     *
     * Production [LATEST: GALOSH_*_O] and [LATEST: GALOSH_YUV_Q] keep
     * g_galosh_lambda_smooth=0 (= bypass this pre-pass entirely).
     * ================================================================ */
    float *sigma_smooth = NULL;
    int n_blk_x = 0, n_blk_y = 0;
    if(g_galosh_lambda_smooth)
    {
        n_blk_x = (cmax / stride) + 1;
        n_blk_y = (rmax / stride) + 1;
        float *sigma_grid = (float *)dt_alloc_align(64,
                                (size_t)n_blk_x * n_blk_y * sizeof(float));
        sigma_smooth      = (float *)dt_alloc_align(64,
                                (size_t)n_blk_x * n_blk_y * sizeof(float));
        if(!sigma_grid || !sigma_smooth)
        {
            if(sigma_grid)   dt_free_align(sigma_grid);
            if(sigma_smooth) { dt_free_align(sigma_smooth); sigma_smooth = NULL; }
            /* fall through with sigma_smooth=NULL → main loop uses inline sigma_y_sq */
        }
        else
        {
            /* Pass-A: per-block sigma_y_sq via WHT + MAD (or L2 sum_sq). */
            #pragma omp parallel for schedule(dynamic, 4)
            for(int ref_r = 0; ref_r <= rmax; ref_r += stride)
            {
                for(int ref_c = 0; ref_c <= cmax; ref_c += stride)
                {
                    float blk[GALOSH_BLOCK_PIXELS];
                    for(int dy = 0; dy < B; dy++)
                        memcpy(blk + dy * B,
                               input + (ref_r + dy) * width + ref_c,
                               B * sizeof(float));
                    wht_fn(blk, 0);
                    float s2;
                    if(use_robust_shrink)
                    {
                        float ac[GALOSH_BLOCK_PIXELS];
                        for(int i = 1; i < N; i++) ac[i - 1] = fabsf(blk[i]);
                        const float mad   = quick_select_median(ac, N - 1);
                        const float scale = mad / 0.6745f;
                        s2 = (scale * scale) / (float)N;
                    }
                    else
                    {
                        float ss = 0.0f;
                        for(int i = 1; i < N; i++) ss += blk[i] * blk[i];
                        s2 = ss / ((float)(N - 1) * (float)N);
                    }
                    const int by = ref_r / stride;
                    const int bx = ref_c / stride;
                    sigma_grid[(size_t)by * n_blk_x + bx] = s2;
                }
            }
            /* Spatial box-smoothing on block grid. */
            const int R = GALOSH_LAMBDA_SMOOTH_RADIUS;
            #pragma omp parallel for schedule(static)
            for(int by = 0; by < n_blk_y; by++)
            {
                for(int bx = 0; bx < n_blk_x; bx++)
                {
                    float sum = 0.0f;
                    int   cnt = 0;
                    const int by0 = (by - R < 0) ? 0 : (by - R);
                    const int by1 = (by + R >= n_blk_y) ? (n_blk_y - 1) : (by + R);
                    const int bx0 = (bx - R < 0) ? 0 : (bx - R);
                    const int bx1 = (bx + R >= n_blk_x) ? (n_blk_x - 1) : (bx + R);
                    for(int ny = by0; ny <= by1; ny++)
                        for(int nx = bx0; nx <= bx1; nx++)
                        {
                            sum += sigma_grid[(size_t)ny * n_blk_x + nx];
                            cnt++;
                        }
                    sigma_smooth[(size_t)by * n_blk_x + bx] = sum / (float)cnt;
                }
            }
            dt_free_align(sigma_grid);
        }
    }

    #pragma omp parallel
    {
        const int tid = omp_get_thread_num();
        float *my_numer = t_numer[tid];
        float *my_denom = t_denom[tid];

        #pragma omp for schedule(dynamic, 4)
        for(int ref_r = 0; ref_r <= rmax; ref_r += stride)
        {
            for(int ref_c = 0; ref_c <= cmax; ref_c += stride)
            {
                /* Extract B×B block (max stack alloc 64 floats covers both B=4 and B=8) */
                float block[GALOSH_BLOCK_PIXELS];
                for(int dy = 0; dy < B; dy++)
                    memcpy(block + dy * B,
                           input + (ref_r + dy) * width + ref_c,
                           B * sizeof(float));

                /* Forward 2D WHT (unnormalized: coefficients scaled by N) */
                wht_fn(block, 0);

                /* sigma_Y^2 from AC coefficients (exclude DC = block[0]).
                 * Two estimators:
                 *
                 * mean (use_robust_shrink == 0, legacy):
                 *   sigma_Y^2 = sum_sq / ((N-1) * N)
                 *   Maximum-likelihood under iid Gaussian, but L2 sensitive
                 *   to outliers — when noise happens to cluster spatially
                 *   the WHT compacts it into a few large coefficients, sum_sq
                 *   is inflated, lambda becomes too small, and the cluster
                 *   survives shrinkage as a "false signal" blob.
                 *
                 * MAD (use_robust_shrink == 1, robust):
                 *   sigma_Y = (median |coef|) / 0.6745 in *unnormalized* WHT
                 *   scale, since median(|N(0, N·σ²)|) = 0.6745·sqrt(N)·σ.
                 *   Then sigma_Y^2_per_pixel = (mad / 0.6745)^2 / N.
                 *   Robust to up to ~25% outlier coefficients (median is in
                 *   the bulk of the distribution).  When a few coefficients
                 *   are spuriously large from a noise cluster, the median
                 *   stays at the typical noise magnitude, sigma_Y stays ≈ 1,
                 *   sigma_X estimates 0, lambda → ∞, and the cluster is
                 *   killed.  This is Donoho-Johnstone (1995) robust noise
                 *   estimation transplanted from wavelet shrinkage to
                 *   GALOSH's local WHT-LOSH.
                 *
                 * MAD は GALOSH の GPU/streaming 互換性を保ったまま、孤立
                 * noise cluster を構造的に消す唯一の局所手法 (NL に踏み込まず
                 * BM3D-style cluster suppression に近づける)。 */
                float sigma_y_sq;
                if(sigma_smooth)
                {
                    /* [DEPRECATED: GALOSH_YUV_P] read pre-computed and
                     * spatially-smoothed sigma_y_sq from the block grid.
                     * Eliminates the per-block lambda discontinuity that
                     * otherwise leaves block-grid trace at curved edges. */
                    const int by = ref_r / stride;
                    const int bx = ref_c / stride;
                    sigma_y_sq = sigma_smooth[(size_t)by * n_blk_x + bx];
                }
                else if(use_robust_shrink)
                {
                    float abs_coefs[GALOSH_BLOCK_PIXELS];  /* max 64, holds N-1 ACs */
                    for(int i = 1; i < N; i++)
                        abs_coefs[i - 1] = fabsf(block[i]);
                    const float mad = quick_select_median(abs_coefs, N - 1);
                    /* mad estimates 0.6745·sqrt(N)·σ_Y in unnormalized scale,
                     * so per-pixel σ_Y² = (mad / 0.6745)² / N. */
                    const float scale = mad / 0.6745f;
                    sigma_y_sq = (scale * scale) / (float)N;
                }
                else
                {
                    float sum_sq = 0.0f;
                    for(int i = 1; i < N; i++)
                        sum_sq += block[i] * block[i];
                    sigma_y_sq = sum_sq / ((float)(N - 1) * (float)N);
                }

                /* sigma_X^2 = max(sigma_Y^2 - sigma^2, 0)
                 *
                 * A1 (g_galosh_pass1_mode==1): replace cap-based safety net
                 * with hierarchical empirical Bayes — when local σ_x is
                 * unreliable (< σ_x_global), fall back to image-level prior.
                 * Avoids λ → ∞ catastrophe in super-clean blocks without
                 * the over-aggressive VisuShrink cap. */
                const float sigma_x_sq_local = fmaxf(sigma_y_sq - sigma_sq, 0.0f);
                const float sigma_x_sq = (g_galosh_pass1_mode == 1)
                  ? fmaxf(sigma_x_sq_local, sigma_x_sq_global)
                  : sigma_x_sq_local;

                /* BayesShrink threshold (in unnormalized WHT scale) */
                float lambda;
                if(sigma_x_sq < 1e-10f)
                {
                    /* Flat block: kill all AC (only noise). In A1 mode this
                     * branch is effectively unreachable (sigma_x_sq_global is
                     * clamped to >=1e-8), but kept as safety. */
                    lambda = 1e30f;
                }
                else
                {
                    /* lambda = sigma^2 / sigma_x scaled by sqrt(N) for unnormalized coef domain */
                    lambda = (sigma_sq / sqrtf(sigma_x_sq)) * sqrtf((float)N);
                    if(g_galosh_pass1_mode != 1)
                    {
                        /* Baseline VisuShrink cap retained for non-A1 mode. */
                        const float lambda_max_unorm = lambda_max * sqrtf((float)N);
                        if(lambda > lambda_max_unorm) lambda = lambda_max_unorm;
                    }
                }

                /* Hard threshold: zero coefficients below lambda, preserve DC */
                int n_nonzero = 1; /* DC always kept */
                for(int i = 1; i < N; i++)
                {
                    if(fabsf(block[i]) < lambda)
                        block[i] = 0.0f;
                    else
                        n_nonzero++;
                }

                /* Inverse 2D WHT (with normalization: divide by N) */
                wht_fn(block, 1);

                /* Overlap-add with Kaiser window + per-block sparsity
                 * weight (= 1/n_nonzero, BLS-GSM PointSC convention). */
                const float weight = 1.0f / (float)n_nonzero;

                for(int dy = 0; dy < B; dy++)
                    for(int dx = 0; dx < B; dx++)
                    {
                        const int pos = (ref_r + dy) * width + (ref_c + dx);
                        const float kw = kaiser[dy * B + dx];
                        my_numer[pos] += weight * kw * block[dy * B + dx];
                        my_denom[pos] += weight * kw;
                    }
            }
        }
    }  /* end omp parallel */

    /* Parallel reduction across threads + per-pixel normalization in one
     * pass.  Replaces the legacy omp-critical merge (which was sequential
     * O(npix * n_threads)) with a thread-parallel sum, fixing the merge
     * bottleneck on multi-core machines. */
    #pragma omp parallel for schedule(static)
    for(int i = 0; i < npix; i++)
    {
        float n = 0.0f, d = 0.0f;
        for(int t = 0; t < n_threads_max; t++)
        {
            n += t_numer[t][i];
            d += t_denom[t][i];
        }
        output[i] = (d > 1e-10f) ? n / d : input[i];
    }

    for(int t = 0; t < n_threads_max; t++)
    {
        dt_free_align(t_numer[t]);
        dt_free_align(t_denom[t]);
    }
    free(t_numer);
    free(t_denom);
    if(sigma_smooth) dt_free_align(sigma_smooth);
}

/* [ARCHIVED] galosh_pass1 — backward-compat wrapper that forces
 * use_robust_shrink=0 (mean-based σ_Y).  GALOSH_RAW_G/YUV_G adopted
 * MAD-based σ_Y (use_robust_shrink=1) so this wrapper is no longer
 * called by the [LATEST] pipeline.  Used by:
 *   - galosh_raw_cpu.c [ARCHIVED] legacy stride=4 #ifdef GALOSH_LEGACY
 *     branch (line ~1500: galosh_pass1 + galosh_pass2)
 *   - old YUV bench scripts that link this header directly */
static inline void galosh_pass1(const float *restrict input,
                                float *restrict output,
                                const int width, const int height,
                                const float sigma_strength,
                                const int stride)
{
    galosh_pass1_blocked(input, output, width, height,
                          sigma_strength, GALOSH_BLOCK_SIZE, stride,
                          /*use_robust_shrink=*/0);
}

/* ================================================================
 * GALOSH Pass 2: Wiener shrinkage using pilot estimate
 *
 * Uses Pass 1 output as pilot (signal estimate) to compute
 * empirical Wiener gains:
 *   w(k) = |Y_pilot(k)|^2 / (|Y_pilot(k)|^2 + sigma^2)
 *
 * This is the textbook James-Stein / empirical Wiener estimator.
 * With a good pilot, this achieves near-oracle performance.
 *
 * Like Pass 1, this operates on a SINGLE 2D plane.
 * Called 4 times in the pipeline: once per L/C plane.
 *
 * WHT normalization note:
 *   Unnormalized WHT scales coefficients by N (=64 for 8x8).
 *   Coefficient energy in unnormalized domain: coeff^2 = (N * true_coeff)^2
 *   Noise variance in unnormalized domain: sigma^2 * N^2
 *   These cancel in the Wiener ratio, so the formula works directly
 *   on unnormalized coefficients with sigma^2_unorm = sigma^2 * N^2.
 * ================================================================ */
/* ================================================================
 * GALOSH Pass 2 (block-size-parameterized).
 *
 * EN: Empirical Wiener using pilot estimate, generalized over block size
 *     B (= 4 or 8).  Wiener floor and DC protection apply identically
 *     regardless of B; only the WHT primitive and Kaiser window switch.
 *     wiener_floor scaling: 1/sqrt(N) gives the per-block-size default
 *     (B=8 → 0.125, B=4 → 0.25), but caller passes it explicitly.
 *
 * JP: Wiener pass を block size B でパラメータ化。Pass1 と同じ B={4,8}
 *     dispatch、wiener_floor は明示渡し。default は 1/sqrt(N) で B=4 だと
 *     0.25 (8×8 の倍に粗く保護)。
 * ================================================================
 * [LATEST: GALOSH_RAW_H/YUV_G] body of galosh_pass2_blocked.  Empirical
 * Wiener Pass2 using a precomputed pilot.  Called from
 * galosh_pass12_multiorient_blocked (Phase 4(a) K13) and from
 * galosh_pass2 / galosh_pass2_multiorient (Phase 4(c) K15).
 * ================================================================ */
static void galosh_pass2_blocked(const float *restrict noisy,
                                  const float *restrict pilot,
                                  float *restrict output,
                                  const int width, const int height,
                                  const float sigma_strength,
                                  const float wiener_floor,
                                  const int block_size,
                                  const int stride)
{
    const int B = block_size;
    const int N = B * B;
    const int rmax = height - B;
    const int cmax = width  - B;
    const int npix = width * height;

    void (*wht_fn)(float *, int);
    const float *kaiser;
    if(B == 8)      { wht_fn = wht2d_8x8;   kaiser = galosh_kaiser_2d;      }
    else if(B == 4) { wht_fn = wht2d_4x4;   kaiser = galosh_kaiser_half_2d; }
    else            { memcpy(output, noisy, sizeof(float) * npix); return; }

    /* Per-thread accumulation buffers (see pass1_blocked for rationale --
     * eliminates the omp-critical merge bottleneck via parallel-for
     * reduction across threads). */
    const int n_threads_max = omp_get_max_threads();
    float **t_numer = (float **)malloc(n_threads_max * sizeof(float *));
    float **t_denom = (float **)malloc(n_threads_max * sizeof(float *));
    if(!t_numer || !t_denom)
    {
        free(t_numer); free(t_denom);
        memcpy(output, noisy, sizeof(float) * npix);
        return;
    }
    int t_alloc_ok = 1;
    for(int t = 0; t < n_threads_max; t++)
    {
        t_numer[t] = (float *)dt_alloc_align(64, sizeof(float) * npix);
        t_denom[t] = (float *)dt_alloc_align(64, sizeof(float) * npix);
        if(!t_numer[t] || !t_denom[t]) { t_alloc_ok = 0; }
        else { memset(t_numer[t], 0, sizeof(float) * npix);
               memset(t_denom[t], 0, sizeof(float) * npix); }
    }
    if(!t_alloc_ok)
    {
        for(int t = 0; t < n_threads_max; t++) {
            if(t_numer[t]) dt_free_align(t_numer[t]);
            if(t_denom[t]) dt_free_align(t_denom[t]);
        }
        free(t_numer); free(t_denom);
        memcpy(output, noisy, sizeof(float) * npix);
        return;
    }

    /* sigma^2 in unnormalized WHT domain: sigma^2 * N */
    const float sigma_sq_unorm = sigma_strength * sigma_strength * (float)N;

    #pragma omp parallel
    {
        const int tid = omp_get_thread_num();
        float *my_numer = t_numer[tid];
        float *my_denom = t_denom[tid];

        #pragma omp for schedule(dynamic, 4)
        for(int ref_r = 0; ref_r <= rmax; ref_r += stride)
        {
            for(int ref_c = 0; ref_c <= cmax; ref_c += stride)
            {
                float blk_noisy[GALOSH_BLOCK_PIXELS];
                float blk_pilot[GALOSH_BLOCK_PIXELS];
                for(int dy = 0; dy < B; dy++)
                {
                    memcpy(blk_noisy + dy * B,
                           noisy + (ref_r + dy) * width + ref_c,
                           B * sizeof(float));
                    memcpy(blk_pilot + dy * B,
                           pilot + (ref_r + dy) * width + ref_c,
                           B * sizeof(float));
                }

                wht_fn(blk_noisy, 0);
                wht_fn(blk_pilot, 0);

                float wiener_energy = 0.0f;
                for(int i = 0; i < N; i++)
                {
                    float w;
                    if(i == 0)
                    {
                        w = 1.0f; /* DC protected */
                    }
                    else
                    {
                        const float s2 = blk_pilot[i] * blk_pilot[i];
                        w = s2 / (s2 + sigma_sq_unorm);
                        if(w < wiener_floor) w = wiener_floor;
                    }
                    blk_noisy[i] *= w;
                    wiener_energy += w * w;
                }

                wht_fn(blk_noisy, 1);

                const float weight = 1.0f / fmaxf(wiener_energy, 1e-6f);
                for(int dy = 0; dy < B; dy++)
                    for(int dx = 0; dx < B; dx++)
                    {
                        const int pos = (ref_r + dy) * width + (ref_c + dx);
                        const float kw = kaiser[dy * B + dx];
                        my_numer[pos] += weight * kw * blk_noisy[dy * B + dx];
                        my_denom[pos] += weight * kw;
                    }
            }
        }
    }  /* end omp parallel */

    /* Parallel reduction + per-pixel normalization (replaces omp-critical
     * merge -- see pass1_blocked). */
    #pragma omp parallel for schedule(static)
    for(int i = 0; i < npix; i++)
    {
        float n = 0.0f, d = 0.0f;
        for(int t = 0; t < n_threads_max; t++)
        {
            n += t_numer[t][i];
            d += t_denom[t][i];
        }
        output[i] = (d > 1e-10f) ? n / d : noisy[i];
    }

    for(int t = 0; t < n_threads_max; t++)
    {
        dt_free_align(t_numer[t]);
        dt_free_align(t_denom[t]);
    }
    free(t_numer);
    free(t_denom);
}

/* [ARCHIVED] galosh_pass2_ex — Pass2 wrapper with caller-specified
 * Wiener floor.  Used by archived chroma WHT-LOSH paths (B-cfa etc.)
 * that needed a higher floor (0.3 vs default 0.125) to prevent
 * over-shrinkage of chroma AC.  GALOSH_RAW_G/YUV_G use LOESS for
 * chroma so this wrapper is no longer called by [LATEST]. */
static inline void galosh_pass2_ex(const float *restrict noisy,
                                    const float *restrict pilot,
                                    float *restrict output,
                                    const int width, const int height,
                                    const float sigma_strength,
                                    const float wiener_floor,
                                    const int stride)
{
    galosh_pass2_blocked(noisy, pilot, output, width, height,
                          sigma_strength, wiener_floor,
                          GALOSH_BLOCK_SIZE, stride);
}

/* [LATEST: GALOSH_RAW_H/YUV_G] body of galosh_pass2 (forward decl above).
 * Wiener-shrinkage Pass2 with default floor (1/B = 0.125 for B=8).
 * Called by:
 *   - galosh_pass2_multiorient (LATEST Phase 4(c) K15, n_orient=1 case)
 *   - galosh_raw_cpu.c [ARCHIVED] legacy stride=4 path (#ifdef GALOSH_LEGACY)
 * Convenience wrapper around galosh_pass2_blocked with B=GALOSH_BLOCK_SIZE
 * and the standard Wiener floor. */
static void galosh_pass2(const float *restrict noisy,
                         const float *restrict pilot,
                         float *restrict output,
                         const int width, const int height,
                         const float sigma_strength,
                         const int stride)
{
    galosh_pass2_ex(noisy, pilot, output, width, height,
                    sigma_strength, GALOSH_WIENER_FLOOR, stride);
}

/* ================================================================
 * CFA-protected GALOSH Pass 1 & 2 for full-res raw Bayer.
 *
 * On full-res raw, the CFA Bayer pattern creates color information
 * at specific WHT frequency bins: (0,1), (1,0), (1,1). These carry
 * the DC of inter-channel differences (R−Gr, R−Gb, R−Gr−Gb+B).
 * Standard GALOSH shrinks these in dark areas → homogenizes channels
 * toward G-biased luma → green color shift.
 *
 * CFA-protected variants: treat indices {0, 1, 8, 9} the same as DC
 * — always preserved, never shrunk. Only 3 extra protected coefficients
 * out of 64 (4.7%), so denoising impact is minimal.
 *
 * 全解像度 raw Bayer 上の GALOSH で、CFA 色 DC 係数 (0,1), (1,0), (1,1)
 * を shrinkage から保護。暗部でのチャンネル均一化を防ぎ、green shift を
 * 抑制する。保護するのは 64 係数中 3 個 (4.7%) のみ。
 * ================================================================ */

/* ================================================================
 * galosh_loess_chroma: luma-guided chroma denoise via locally-weighted
 * linear regression (LOESS / bilateral-weighted guided filter).
 *
 * EN: For each half-res pixel (x,y), fit the local linear model
 *         Cb_i = a·Y_i + b + ε_i,  i ∈ (2·R+1)² window
 *     weighted by w_i = exp(-(Y_i-Y_c)²/(2·σ²)) so neighbours whose
 *     luma differs from the centre's by >σ are excluded (this makes
 *     the regression robust to specular highlights in silver surfaces).
 *     Bayes MAP with Gaussian prior a~N(0,τ²) gives
 *         a = cov(Y,Cb) / (var(Y) + 1/τ²),   b = mean_Cb - a·mean_Y
 *     yielding  Cb_out = a·Y_c + b.  In GAT-stabilised space per-pixel
 *     noise is uniformly ≈1, so τ²=1 (|a|≤2 prior) gives ε=1/τ²=1.
 *
 *     Drop-in replacement for the 3-plane WHT-LOSH chroma pipeline
 *     (galosh_pass1 + galosh_pass2 on each of C1, C2, C3); mirrors the
 *     GPU `galosh_yuv_guided_loess` kernel exactly (same formula, same
 *     τ², same bandwidth σ).  The L plane keeps WHT-LOSH; only the
 *     three chroma sub-bands switch to LOESS.
 *
 * JP: 半解像度各画素 (x,y) を中心に (2R+1)² 窓を bilateral 重み
 *     w_i = exp(-(Y_i-Y_c)²/(2σ²)) 付きで局所線形回帰。σ=3 (GAT per-pixel
 *     std=1 の 3σ) で silver+specular bimodal window を silver 側のみに
 *     絞り込み、silver 面の赤青粒を防ぐ。Bayes MAP から ε=1/τ²=1。
 *     WHT-LOSH の pass1+pass2 chroma をこの 1 関数で置換。GPU の
 *     galosh_yuv_guided_loess と完全同一理論。 */
#ifndef GALOSH_LOESS_RADIUS
#define GALOSH_LOESS_RADIUS 7          /* 15×15 window matches GPU default */
#endif
#ifndef GALOSH_LOESS_BW
#define GALOSH_LOESS_BW 3.0f           /* σ in GAT units (per-pixel noise ≈1) */
#endif
#ifndef GALOSH_LOESS_TAU_SQ_INV
#define GALOSH_LOESS_TAU_SQ_INV 1.0f   /* ε = 1/τ²  (τ²=1 → |a|≤2 prior) */
#endif

static inline int reflect_idx(int i, int n)
{
  if(i < 0) return -i;
  if(i >= n) return 2 * n - i - 2;
  return i;
}

/* [LATEST: GALOSH_RAW_I/H/YUV_G] galosh_loess_chroma_r — Y-guided
 * bilateral LOESS on chroma planes, R-parameterized.
 *   GALOSH_RAW_I  use: Phase 5(C) at half-res w/ R=GALOSH_LOESS_RADIUS,
 *                      AND Phase 6.5 at full-res w/ R=3 for L-edge
 *                      alignment refinement (joint bilateral upsample).
 *   GALOSH_RAW_H  use: Phase 5 at full-res w/ R=GALOSH_LOESS_RADIUS.
 *   GALOSH_RAW_G  use: Phase 3.5 at half-res w/ R=GALOSH_LOESS_RADIUS.
 *   GALOSH_YUV_G  use: chroma stage on Cb/Cr after Y plane LOSH.
 *
 * Per-pixel local linear regression with bilateral weight
 *   w_i = exp(-(Y_i - Y_c)² / (2σ²))
 * (σ = GALOSH_LOESS_BW = 3.0).  Specular highlights excluded by the
 * weight, kills silver-window red/blue chroma noise spurs.
 *
 * R parameterization usage:
 *   R=GALOSH_LOESS_RADIUS (=7, 15x15 window) — primary denoise
 *   R=3 (7x7 window)                          — refinement / edge align
 *
 * Replaces pre-G WHT-LOSH-on-chroma + pre-G separable mean guided. */
static void galosh_loess_chroma_r(const float *restrict y_guide,
                                  const float *restrict cb_in,
                                  const float *restrict cr_in,
                                  float *restrict cb_out,
                                  float *restrict cr_out,
                                  const int width, const int height,
                                  const float strength_c, const int R)
{
  const float eps_gat = strength_c * strength_c * GALOSH_LOESS_TAU_SQ_INV;
  const float inv_2sigma_sq = 1.0f / (2.0f * GALOSH_LOESS_BW * GALOSH_LOESS_BW);

  const int x0_int = (R < width)  ? R          : width;
  const int x1_int = (width  > R) ? width - R  : 0;
  const int y0_int = (R < height) ? R          : height;
  const int y1_int = (height > R) ? height - R : 0;

  DT_OMP_FOR()
  for(int y = 0; y < height; y++)
  {
    const int y_interior = (y >= y0_int && y < y1_int);
    for(int x = 0; x < width; x++)
    {
      const size_t cx = (size_t)y * width + x;
      const float Y_c = y_guide[cx];

      float sumW = 0.f, sumY = 0.f, sumYY = 0.f;
      float sumCb = 0.f, sumCr = 0.f;
      float sumYCb = 0.f, sumYCr = 0.f;
#ifdef GALOSH_CHROMA_CLAMP
      /* full-window input-chroma range, to clamp the degree-1 LOESS extrapolation
       * overshoot (mirrors galosh_loess_chroma_3ch_r; reflected to YUV/RGB). */
      float lo_cb=1e30f, hi_cb=-1e30f, lo_cr=1e30f, hi_cr=-1e30f;
#endif

      if(y_interior && x >= x0_int && x < x1_int)
      {
        /* Interior fast path: no boundary reflection, contiguous reads.
         * Inner (2R+1) loop is linear-stride, gcc auto-vectorises. */
        for(int dy = -R; dy <= R; dy++)
        {
          const size_t row_off = (size_t)(y + dy) * width;
          const float *rowY  = y_guide + row_off;
          const float *rowCb = cb_in   + row_off;
          const float *rowCr = cr_in   + row_off;
          for(int dx = -R; dx <= R; dx++)
          {
            const int xi = x + dx;
            const float Yi  = rowY [xi];
            const float Cbi = rowCb[xi];
            const float Cri = rowCr[xi];
#ifdef GALOSH_CHROMA_CLAMP
            if(Cbi<lo_cb)lo_cb=Cbi; if(Cbi>hi_cb)hi_cb=Cbi;
            if(Cri<lo_cr)lo_cr=Cri; if(Cri>hi_cr)hi_cr=Cri;
#endif
            const float dY  = Yi - Y_c;
            const float w   = expf(-dY * dY * inv_2sigma_sq);
            sumW   += w;
            sumY   += w * Yi;
            sumYY  += w * Yi * Yi;
            sumCb  += w * Cbi;
            sumCr  += w * Cri;
            sumYCb += w * Yi * Cbi;
            sumYCr += w * Yi * Cri;
          }
        }
      }
      else
      {
        /* Slow path: reflect padding at image edges (≤1% of pixels). */
        for(int dy = -R; dy <= R; dy++)
        {
          const int yi = reflect_idx(y + dy, height);
          const float *rowY  = y_guide + (size_t)yi * width;
          const float *rowCb = cb_in   + (size_t)yi * width;
          const float *rowCr = cr_in   + (size_t)yi * width;
          for(int dx = -R; dx <= R; dx++)
          {
            const int xi = reflect_idx(x + dx, width);
            const float Yi  = rowY [xi];
            const float Cbi = rowCb[xi];
            const float Cri = rowCr[xi];
#ifdef GALOSH_CHROMA_CLAMP
            if(Cbi<lo_cb)lo_cb=Cbi; if(Cbi>hi_cb)hi_cb=Cbi;
            if(Cri<lo_cr)lo_cr=Cri; if(Cri>hi_cr)hi_cr=Cri;
#endif
            const float dY  = Yi - Y_c;
            const float w   = expf(-dY * dY * inv_2sigma_sq);
            sumW   += w;
            sumY   += w * Yi;
            sumYY  += w * Yi * Yi;
            sumCb  += w * Cbi;
            sumCr  += w * Cri;
            sumYCb += w * Yi * Cbi;
            sumYCr += w * Yi * Cri;
          }
        }
      }

      const float invW     = 1.0f / fmaxf(sumW, 1e-10f);
      const float mean_Y   = sumY   * invW;
      const float mean_YY  = sumYY  * invW;
      const float mean_Cb  = sumCb  * invW;
      const float mean_Cr  = sumCr  * invW;
      const float mean_YCb = sumYCb * invW;
      const float mean_YCr = sumYCr * invW;

      const float var_Y   = fmaxf(mean_YY - mean_Y * mean_Y, 0.0f);
      const float cov_YCb = mean_YCb - mean_Y * mean_Cb;
      const float cov_YCr = mean_YCr - mean_Y * mean_Cr;
      const float denom   = fmaxf(var_Y + eps_gat, 1e-6f);
      const float a_cb    = cov_YCb / denom;
      const float a_cr    = cov_YCr / denom;
      const float b_cb    = mean_Cb - a_cb * mean_Y;
      const float b_cr    = mean_Cr - a_cr * mean_Y;

      float ocb = a_cb * Y_c + b_cb;
      float ocr = a_cr * Y_c + b_cr;
#ifdef GALOSH_CHROMA_CLAMP
      /* clamp degree-1 luma-guided regression extrapolation to the local input
       * chroma band -> kills chroma overshoot at luma edges (= RAW chroma-clamp
       * reflected to the YUV/RGB canonical pipeline, 2026-06-28). */
      if(hi_cb>=lo_cb) ocb = fminf(fmaxf(ocb, lo_cb), hi_cb);
      if(hi_cr>=lo_cr) ocr = fminf(fmaxf(ocr, lo_cr), hi_cr);
#endif
      cb_out[cx] = ocb;
      cr_out[cx] = ocr;
    }
  }
}

/* [LATEST: GALOSH_RAW_J/I/H/YUV_G] galosh_loess_chroma — thin wrapper that
 * delegates to galosh_loess_chroma_r with R = GALOSH_LOESS_RADIUS (=7).
 * Preserved for backward compatibility with all existing callers
 * (gat_galosh_denoise_rawlc, gat_galosh_denoise_rawlc_h,
 *  galosh_yuv_cpu).  GALOSH_RAW_J / I prefer galosh_loess_chroma_3ch_r
 * (3-channel fused) for performance. */
static inline void galosh_loess_chroma(const float *restrict y_guide,
                                       const float *restrict cb_in,
                                       const float *restrict cr_in,
                                       float *restrict cb_out,
                                       float *restrict cr_out,
                                       const int width, const int height,
                                       const float strength_c)
{
  galosh_loess_chroma_r(y_guide, cb_in, cr_in, cb_out, cr_out,
                        width, height, strength_c, GALOSH_LOESS_RADIUS);
}

/* [LATEST: GALOSH_RAW_M] galosh_loess_chroma_3ch_r_with_var — same as
 * galosh_loess_chroma_3ch_r below but ALSO emits per-pixel sample
 * variance estimates (var_C1/var_C2/var_C3) used by hierarchical
 * Bayesian denoising (= GALOSH_RAW_M Phase 5(C)).
 *
 * Per-pixel σ_local²(x) is the bias-corrected weighted sample variance
 * within the LOESS window:
 *   σ_local²(x) = max( weighted_var(C_in in window) - σ_n², 0 )
 *   weighted_var = Σ w_i C_i² / Σ w_i  −  (Σ w_i C_i / Σ w_i)²
 * In GAT-normalized space σ_n² = 1 (= structural property of GAT
 * normalization, NOT a magic constant).
 *
 * The variance estimate is computed from sumC²_per_channel which is
 * accumulated alongside the existing LOESS sums (= one extra mul-add
 * per window position per chroma channel = +~15% LOESS cost).
 *
 * Used by GALOSH_RAW_M Phase 5(C) for the LOCAL LOESS stage; the
 * GLOBAL stage uses galosh_loess_chroma_3ch_r (without var) since
 * hierarchical Bayesian only needs σ²_local, not σ²_global. */
static void galosh_loess_chroma_3ch_r_with_var(
    const float *restrict y_guide,
    const float *restrict c1_in,
    const float *restrict c2_in,
    const float *restrict c3_in,
    float *restrict c1_out,
    float *restrict c2_out,
    float *restrict c3_out,
    float *restrict var_c1_out,
    float *restrict var_c2_out,
    float *restrict var_c3_out,
    const int width, const int height,
    const float strength_c, const int R, const float BW)
{
  const float eps_gat = strength_c * strength_c * GALOSH_LOESS_TAU_SQ_INV;
  const float inv_2sigma_sq = 1.0f / (2.0f * BW * BW);

  const int x0_int = (R < width)  ? R          : width;
  const int x1_int = (width  > R) ? width - R  : 0;
  const int y0_int = (R < height) ? R          : height;
  const int y1_int = (height > R) ? height - R : 0;

  /* σ_n² = 1 in GAT-normalized space (= structural property post-Phase 1
   * RMS unified-sigma normalization).  Not a magic constant. */
  const float sigma_n_sq = 1.0f;

  DT_OMP_FOR()
  for(int y = 0; y < height; y++)
  {
    const int y_interior = (y >= y0_int && y < y1_int);
    for(int x = 0; x < width; x++)
    {
      const size_t cx = (size_t)y * width + x;
      const float Y_c = y_guide[cx];

      float sumW = 0.f, sumY = 0.f, sumYY = 0.f;
      float sumC1 = 0.f, sumC2 = 0.f, sumC3 = 0.f;
      float sumYC1 = 0.f, sumYC2 = 0.f, sumYC3 = 0.f;
      /* Per-channel sumC²_i for sample variance (Bayesian σ_local²). */
      float sumC1sq = 0.f, sumC2sq = 0.f, sumC3sq = 0.f;

      if(y_interior && x >= x0_int && x < x1_int)
      {
        for(int dy = -R; dy <= R; dy++)
        {
          const size_t row_off = (size_t)(y + dy) * width;
          const float *rowY  = y_guide + row_off;
          const float *rowC1 = c1_in   + row_off;
          const float *rowC2 = c2_in   + row_off;
          const float *rowC3 = c3_in   + row_off;
          for(int dx = -R; dx <= R; dx++)
          {
            const int xi = x + dx;
            const float Yi  = rowY [xi];
            const float C1i = rowC1[xi];
            const float C2i = rowC2[xi];
            const float C3i = rowC3[xi];
            const float dY  = Yi - Y_c;
            const float w   = expf(-dY * dY * inv_2sigma_sq);
            sumW   += w;
            sumY   += w * Yi;
            sumYY  += w * Yi * Yi;
            sumC1  += w * C1i;
            sumC2  += w * C2i;
            sumC3  += w * C3i;
            sumYC1 += w * Yi * C1i;
            sumYC2 += w * Yi * C2i;
            sumYC3 += w * Yi * C3i;
            sumC1sq += w * C1i * C1i;
            sumC2sq += w * C2i * C2i;
            sumC3sq += w * C3i * C3i;
          }
        }
      }
      else
      {
        for(int dy = -R; dy <= R; dy++)
        {
          const int yi = reflect_idx(y + dy, height);
          const size_t row_off = (size_t)yi * width;
          const float *rowY  = y_guide + row_off;
          const float *rowC1 = c1_in   + row_off;
          const float *rowC2 = c2_in   + row_off;
          const float *rowC3 = c3_in   + row_off;
          for(int dx = -R; dx <= R; dx++)
          {
            const int xi = reflect_idx(x + dx, width);
            const float Yi  = rowY [xi];
            const float C1i = rowC1[xi];
            const float C2i = rowC2[xi];
            const float C3i = rowC3[xi];
            const float dY  = Yi - Y_c;
            const float w   = expf(-dY * dY * inv_2sigma_sq);
            sumW   += w;
            sumY   += w * Yi;
            sumYY  += w * Yi * Yi;
            sumC1  += w * C1i;
            sumC2  += w * C2i;
            sumC3  += w * C3i;
            sumYC1 += w * Yi * C1i;
            sumYC2 += w * Yi * C2i;
            sumYC3 += w * Yi * C3i;
            sumC1sq += w * C1i * C1i;
            sumC2sq += w * C2i * C2i;
            sumC3sq += w * C3i * C3i;
          }
        }
      }

      const float invW   = 1.0f / fmaxf(sumW, 1e-10f);
      const float meanY  = sumY  * invW;
      const float meanYY = sumYY * invW;
      const float meanC1 = sumC1 * invW;
      const float meanC2 = sumC2 * invW;
      const float meanC3 = sumC3 * invW;
      const float meanYC1 = sumYC1 * invW;
      const float meanYC2 = sumYC2 * invW;
      const float meanYC3 = sumYC3 * invW;
      const float meanC1sq = sumC1sq * invW;
      const float meanC2sq = sumC2sq * invW;
      const float meanC3sq = sumC3sq * invW;

      const float var_Y   = fmaxf(meanYY - meanY * meanY, 0.0f);
      const float denom   = fmaxf(var_Y + eps_gat, 1e-6f);
      const float inv_denom = 1.0f / denom;

      const float a_c1 = (meanYC1 - meanY * meanC1) * inv_denom;
      const float a_c2 = (meanYC2 - meanY * meanC2) * inv_denom;
      const float a_c3 = (meanYC3 - meanY * meanC3) * inv_denom;
      const float b_c1 = meanC1 - a_c1 * meanY;
      const float b_c2 = meanC2 - a_c2 * meanY;
      const float b_c3 = meanC3 - a_c3 * meanY;

      c1_out[cx] = a_c1 * Y_c + b_c1;
      c2_out[cx] = a_c2 * Y_c + b_c2;
      c3_out[cx] = a_c3 * Y_c + b_c3;

      /* Per-channel σ_local²(x) = max(sample_variance - σ_n², 0).
       * Sample variance from weighted second moment, bias-corrected by
       * subtracting structural σ_n² = 1 (GAT-norm). */
      const float var_c1_raw = meanC1sq - meanC1 * meanC1;
      const float var_c2_raw = meanC2sq - meanC2 * meanC2;
      const float var_c3_raw = meanC3sq - meanC3 * meanC3;
      var_c1_out[cx] = fmaxf(var_c1_raw - sigma_n_sq, 0.0f);
      var_c2_out[cx] = fmaxf(var_c2_raw - sigma_n_sq, 0.0f);
      var_c3_out[cx] = fmaxf(var_c3_raw - sigma_n_sq, 0.0f);
    }
  }
}

/* [LATEST: GALOSH_RAW_M] gat_m_bayesian_fusion_3ch — per-pixel
 * hierarchical Bayesian inverse-variance weighted fusion of local
 * vs global LOESS estimates.
 *
 * Per-channel update (k = 1, 2, 3):
 *   σ_n²_eff = chroma_strength² × σ_n²        (= chroma_strength² in GAT-norm)
 *   w_data_k = (N_local · σ_local_k²) / (N_local · σ_local_k² + σ_n²_eff)
 *   C_k_out  = w_data_k · C_k_local + (1 - w_data_k) · C_k_global
 *
 * Behavior:
 *   chroma_strength = 0  → w_data = 1 → C_out = C_local (= H equivalent)
 *   chroma_strength = 1  → balanced (σ_local²(x)-driven adaptive)
 *   chroma_strength → ∞  → w_data = 0 → C_out = C_global (= max smoothing)
 *
 * This is the MAP estimator under the hierarchical model:
 *   C_true ~ N(C_global, σ_local²)        (= local-vs-global prior)
 *   C_local_hat ~ N(C_true, σ_n²/N_local) (= local LOESS estimator)
 * with chroma_strength scaling the noise variance σ_n² (= user's
 * "stronger denoise = treat noise as larger" intuition, principled).
 *
 * (日) MAP estimator: σ_n² scaling で「noise を k×大きいと仮定」して
 *   denoise を強めるという user 直感を Bayesian 階層モデルで実現。
 *   平坦部は σ_local² ≈ 0 → w_data → 0 → C_global (強平滑化)。
 *   detail 部は σ_local² > 0 → w_data → 1 → C_local (詳細保存)。 */
static void gat_m_bayesian_fusion_3ch(
    const float *c1_local,
    const float *c2_local,
    const float *c3_local,
    const float *c1_global,
    const float *c2_global,
    const float *c3_global,
    const float *var_c1,
    const float *var_c2,
    const float *var_c3,
    float *c1_out,
    float *c2_out,
    float *c3_out,
    const int width, const int height,
    const int N_local,
    const float chroma_strength)
{
  /* σ_n²_eff = chroma_strength² × σ_n² = chroma_strength² (GAT-norm σ_n²=1). */
  const float sigma_n_sq_eff = chroma_strength * chroma_strength;
  const float Nl = (float)N_local;

  DT_OMP_FOR()
  for(int y = 0; y < height; y++)
  {
    const size_t row_off = (size_t)y * width;
    for(int x = 0; x < width; x++)
    {
      const size_t cx = row_off + x;

      const float v1 = var_c1[cx], v2 = var_c2[cx], v3 = var_c3[cx];
      const float Nv1 = Nl * v1;
      const float Nv2 = Nl * v2;
      const float Nv3 = Nl * v3;
      /* Per-channel inverse-variance weight w_data_k.
       * Floor on denom guards against simultaneous v_k=0 AND
       * chroma_strength=0 (= edge case → w_data→1, output=local). */
      const float w1 = Nv1 / fmaxf(Nv1 + sigma_n_sq_eff, 1e-12f);
      const float w2 = Nv2 / fmaxf(Nv2 + sigma_n_sq_eff, 1e-12f);
      const float w3 = Nv3 / fmaxf(Nv3 + sigma_n_sq_eff, 1e-12f);

      c1_out[cx] = w1 * c1_local[cx] + (1.0f - w1) * c1_global[cx];
      c2_out[cx] = w2 * c2_local[cx] + (1.0f - w2) * c2_global[cx];
      c3_out[cx] = w3 * c3_local[cx] + (1.0f - w3) * c3_global[cx];
    }
  }
}

/* [LATEST: GALOSH_RAW_J] galosh_loess_chroma_3ch_r — multi-channel
 * (3 chroma plane) Y-guided bilateral LOESS, fused single-pass version
 * of galosh_loess_chroma_r.  Y-related stats (sumW, sumY, sumYY) are
 * computed once per window and shared across the 3 chroma regressions
 * → ~36% compute reduction vs 2 separate chroma_r calls (which would
 * recompute Y stats independently in each call).  Per-channel sums
 * (sumC1/2/3, sumYC1/2/3) computed once each.
 *
 * Parameters:
 *   strength_c — sets ε = strength_c² × GALOSH_LOESS_TAU_SQ_INV
 *                (Bayesian: ε = σ_C² / τ²; should match the input C's
 *                 noise variance for principled regularization)
 *   R          — bilateral window half-size (window = (2R+1)×(2R+1))
 *   BW         — bilateral bandwidth (σ in GAT-norm units; 3.0 for
 *                Phase 5(C) primary denoise = 3σ Mahalanobis cluster
 *                threshold; 1.5 for Phase 8 refinement = tighter
 *                separation for moderate-to-weak edges where input is
 *                already low-noise)
 *
 * Used by:
 *   GALOSH_RAW_J Phase 5(C): half-res chroma denoise
 *     (strength_c = chroma slider, R = GALOSH_LOESS_RADIUS, BW = 3.0)
 *   GALOSH_RAW_J Phase 8:    full-res chroma refinement
 *     (strength_c = 0.1 hardcoded for ε=0.01, R=3, BW=1.5) */
static void galosh_loess_chroma_3ch_r(
    const float *restrict y_guide,
    const float *restrict c1_in,
    const float *restrict c2_in,
    const float *restrict c3_in,
    float *restrict c1_out,
    float *restrict c2_out,
    float *restrict c3_out,
    const int width, const int height,
    const float strength_c, const int R, const float BW)
{
  const float eps_gat = strength_c * strength_c * GALOSH_LOESS_TAU_SQ_INV;
  const float inv_2sigma_sq = 1.0f / (2.0f * BW * BW);

  const int x0_int = (R < width)  ? R          : width;
  const int x1_int = (width  > R) ? width - R  : 0;
  const int y0_int = (R < height) ? R          : height;
  const int y1_int = (height > R) ? height - R : 0;

  DT_OMP_FOR()
  for(int y = 0; y < height; y++)
  {
    const int y_interior = (y >= y0_int && y < y1_int);
    for(int x = 0; x < width; x++)
    {
      const size_t cx = (size_t)y * width + x;
      const float Y_c = y_guide[cx];

      float sumW = 0.f, sumY = 0.f, sumYY = 0.f;
      float sumC1 = 0.f, sumC2 = 0.f, sumC3 = 0.f;
      float sumYC1 = 0.f, sumYC2 = 0.f, sumYC3 = 0.f;
#ifdef GALOSH_CHROMA_CLAMP
      /* input-chroma window range to clamp the degree-1 regression extrapolation */
      float lo1= 1e30f, lo2= 1e30f, lo3= 1e30f, hi1=-1e30f, hi2=-1e30f, hi3=-1e30f;
#endif

      if(y_interior && x >= x0_int && x < x1_int)
      {
        for(int dy = -R; dy <= R; dy++)
        {
          const size_t row_off = (size_t)(y + dy) * width;
          const float *rowY  = y_guide + row_off;
          const float *rowC1 = c1_in   + row_off;
          const float *rowC2 = c2_in   + row_off;
          const float *rowC3 = c3_in   + row_off;
          for(int dx = -R; dx <= R; dx++)
          {
            const int xi = x + dx;
            const float Yi  = rowY [xi];
            const float C1i = rowC1[xi];
            const float C2i = rowC2[xi];
            const float C3i = rowC3[xi];
            const float dY  = Yi - Y_c;
            const float w   = expf(-dY * dY * inv_2sigma_sq);
            sumW   += w;
            sumY   += w * Yi;
            sumYY  += w * Yi * Yi;
            sumC1  += w * C1i;
            sumC2  += w * C2i;
            sumC3  += w * C3i;
            sumYC1 += w * Yi * C1i;
            sumYC2 += w * Yi * C2i;
            sumYC3 += w * Yi * C3i;
#ifdef GALOSH_CHROMA_CLAMP
            if(1){ if(C1i<lo1)lo1=C1i; if(C1i>hi1)hi1=C1i;
                          if(C2i<lo2)lo2=C2i; if(C2i>hi2)hi2=C2i;
                          if(C3i<lo3)lo3=C3i; if(C3i>hi3)hi3=C3i; }
#endif
          }
        }
      }
      else
      {
        for(int dy = -R; dy <= R; dy++)
        {
          const int yi = reflect_idx(y + dy, height);
          const size_t row_off = (size_t)yi * width;
          const float *rowY  = y_guide + row_off;
          const float *rowC1 = c1_in   + row_off;
          const float *rowC2 = c2_in   + row_off;
          const float *rowC3 = c3_in   + row_off;
          for(int dx = -R; dx <= R; dx++)
          {
            const int xi = reflect_idx(x + dx, width);
            const float Yi  = rowY [xi];
            const float C1i = rowC1[xi];
            const float C2i = rowC2[xi];
            const float C3i = rowC3[xi];
            const float dY  = Yi - Y_c;
            const float w   = expf(-dY * dY * inv_2sigma_sq);
            sumW   += w;
            sumY   += w * Yi;
            sumYY  += w * Yi * Yi;
            sumC1  += w * C1i;
            sumC2  += w * C2i;
            sumC3  += w * C3i;
            sumYC1 += w * Yi * C1i;
            sumYC2 += w * Yi * C2i;
            sumYC3 += w * Yi * C3i;
#ifdef GALOSH_CHROMA_CLAMP
            if(1){ if(C1i<lo1)lo1=C1i; if(C1i>hi1)hi1=C1i;
                          if(C2i<lo2)lo2=C2i; if(C2i>hi2)hi2=C2i;
                          if(C3i<lo3)lo3=C3i; if(C3i>hi3)hi3=C3i; }
#endif
          }
        }
      }

      const float invW   = 1.0f / fmaxf(sumW, 1e-10f);
      const float meanY  = sumY  * invW;
      const float meanYY = sumYY * invW;
      const float meanC1 = sumC1 * invW;
      const float meanC2 = sumC2 * invW;
      const float meanC3 = sumC3 * invW;
      const float meanYC1 = sumYC1 * invW;
      const float meanYC2 = sumYC2 * invW;
      const float meanYC3 = sumYC3 * invW;

      const float var_Y   = fmaxf(meanYY - meanY * meanY, 0.0f);
      const float denom   = fmaxf(var_Y + eps_gat, 1e-6f);
      const float inv_denom = 1.0f / denom;

      const float a_c1 = (meanYC1 - meanY * meanC1) * inv_denom;
      const float a_c2 = (meanYC2 - meanY * meanC2) * inv_denom;
      const float a_c3 = (meanYC3 - meanY * meanC3) * inv_denom;
      const float b_c1 = meanC1 - a_c1 * meanY;
      const float b_c2 = meanC2 - a_c2 * meanY;
      const float b_c3 = meanC3 - a_c3 * meanY;

      float oc1 = a_c1 * Y_c + b_c1;
      float oc2 = a_c2 * Y_c + b_c2;
      float oc3 = a_c3 * Y_c + b_c3;
#ifdef GALOSH_CHROMA_CLAMP
      /* clamp degree-1 luma-guided regression extrapolation to the local input
       * chroma band -> kills chroma overshoot at luma edges (= magenta source #1). */
      if(hi1>=lo1) oc1 = fminf(fmaxf(oc1, lo1), hi1);
      if(hi2>=lo2) oc2 = fminf(fmaxf(oc2, lo2), hi2);
      if(hi3>=lo3) oc3 = fminf(fmaxf(oc3, lo3), hi3);
#endif
      c1_out[cx] = oc1;
      c2_out[cx] = oc2;
      c3_out[cx] = oc3;
    }
  }
}

/* =================================================================
 * [LATEST: GALOSH_RAW_H] H-pipeline helpers — stride=1 cycle-spinning
 * forward WHT, CFA sign-flip demodulation/remodulation, 4-block
 * overlap-add inverse.
 *
 * GALOSH_RAW_H operates entirely at full resolution, eliminating the
 * half-res ↔ full-res round-trip that GALOSH_RAW_G needed (K14 box
 * compute_L_fullres + K16 EWA-JL3 chromaup).  The 2x2 Bayer block is
 * decomposed via cycle-spinning forward WHT (one decomposition per
 * pixel TL with right/bottom mirror padding), giving 4 full-res planes
 * (L, C1, C2, C3).
 *
 * CFA periodicity manifests as deterministic sign flips on chroma bins:
 *   C1 sign at (r,c) = (-1)^r       (vertical Bayer flip)
 *   C2 sign at (r,c) = (-1)^c       (horizontal Bayer flip)
 *   C3 sign at (r,c) = (-1)^(r+c)   (checkerboard Bayer flip)
 * Demodulating these flips before denoise yields smooth full-res
 * chroma planes amenable to LOESS / WHT-LOSH.  Re-modulating before
 * inverse WHT is the exact algebraic inverse (the demod operator is
 * self-inverse: applying it twice is identity).
 *
 * Reconstruction is per-pixel, summing contributions from up to 4
 * cycle-shifted block decompositions (overlap-add average — 4 blocks
 * in interior, fewer at boundary).
 *
 * (日本語) raw 全解像度上で stride=1 cycle-spinning 順方向 WHT を
 *   周辺ミラー詰めで実行 → 4 plane (L, C1, C2, C3) を full-res で得る。
 *   CFA 周期由来の C1/C2/C3 符号反転を脱変調で除去 → LOESS / WHT-LOSH
 *   適用可能、denoise 後に再変調 + 4-block 重なり加算逆変換で
 *   full-res に戻す。GALOSH_RAW_G の半解像度往復 (K14 box / K16
 *   chromaup) を完全消滅。
 *
 * Reference: cycle-spinning denoising — Coifman & Donoho (Wavelets
 *   and Statistics, 1995); CFA frequency analysis on Bayer raw —
 *   Hirakawa & Wolfe (TIP 2008).
 * ================================================================= */

/* Mirror index — same convention as reflect_idx above (line ~2118),
 * kept here for hot-loop callsites that want to inline.  Mirrors
 * strictly across the boundary so that mirror(N) = N-2 (not N-1, which
 * would alias one pixel). */
static inline int galosh_h_mirror_idx(const int i, const int n)
{
  if(i < 0)  return -i;
  if(i >= n) return 2 * n - i - 2;
  return i;
}

/* [LATEST: GALOSH_RAW_H] gat_h_forward_wht_stride1 — forward 2x2 WHT
 * at every pixel position (cycle-spinning, stride=1) with right/bottom
 * mirror padding.  Output planes (L, C1, C2, C3) are all full-res; the
 * value at (r,c) corresponds to the 2x2 block whose TL is at (r,c),
 * BL at (r+1,c), TR at (r,c+1), BR at (r+1,c+1).
 *
 * Forward formulas (a=TL, b=BL, c=TR, d=BR):
 *   L  = (a + b + c + d) / 2
 *   C1 = (a - b + c - d) / 2     (vertical chroma — TL−BL paired)
 *   C2 = (a + b - c - d) / 2     (horizontal chroma — TL+BL vs TR+BR)
 *   C3 = (a - b - c + d) / 2     (diagonal chroma — TL+BR vs TR+BL)
 *
 * (日) 各画素 (r,c) を TL とする 2x2 WHT を full-res で全画素実行。
 *     右下境界はミラー詰め(galosh_h_mirror_idx)で 2x2 が成立するよう
 *     にする。順方向 WHT は単位的 (orthonormal) → 各 plane の
 *     GAT-norm 雑音標準偏差は 1 を維持する。 */
static void gat_h_forward_wht_stride1(const float *restrict in,
                                      float *restrict L,
                                      float *restrict C1,
                                      float *restrict C2,
                                      float *restrict C3,
                                      const int width, const int height)
{
  DT_OMP_FOR()
  for(int r = 0; r < height; r++)
  {
    const int rb = galosh_h_mirror_idx(r + 1, height);
    const float *row_a = in + (size_t)r  * width;
    const float *row_b = in + (size_t)rb * width;
    const size_t out_off = (size_t)r * width;
    for(int c = 0; c < width; c++)
    {
      const int cb = galosh_h_mirror_idx(c + 1, width);
      const float a  = row_a[c];
      const float b  = row_b[c];
      const float cc = row_a[cb];
      const float d  = row_b[cb];
      L [out_off + c] = (a + b + cc + d) * 0.5f;
      C1[out_off + c] = (a - b + cc - d) * 0.5f;
      C2[out_off + c] = (a + b - cc - d) * 0.5f;
      C3[out_off + c] = (a - b - cc + d) * 0.5f;
    }
  }
}

/* [LATEST: GALOSH_RAW_J] gat_h_forward_l_only_stride1 — forward 2x2 WHT
 * at every pixel position, computing ONLY the L plane (not C1/C2/C3).
 *
 * Used by GALOSH_RAW_J Phase 3: J's chroma path operates at half-res
 * (intrinsic CFA chroma Nyquist limit), so the cycle-spun full-res
 * C planes that gat_h_forward_wht_stride1 would compute are immediately
 * sub-sampled and discarded.  This L-only variant skips the C compute
 * entirely (~75% of forward WHT cost saved + 3 × full-res buffers
 * (= ~150 MB at 16 MP) eliminated) and is mathematically equivalent
 * to gat_h_forward_wht_stride1's L output.
 *
 * The half-res C planes for J are computed separately by extracting
 * the 2x2 block at every-other-pixel positions of in_gat directly
 * (a stride=2 forward 2x2 WHT, mathematically identical to the
 * sub-sample of the would-be full-res C_cs at even (r,c) positions). */
static void gat_h_forward_l_only_stride1(const float *restrict in,
                                         float *restrict L,
                                         const int width, const int height)
{
  DT_OMP_FOR()
  for(int r = 0; r < height; r++)
  {
    const int rb = galosh_h_mirror_idx(r + 1, height);
    const float *row_a = in + (size_t)r  * width;
    const float *row_b = in + (size_t)rb * width;
    const size_t out_off = (size_t)r * width;
    for(int c = 0; c < width; c++)
    {
      const int cb = galosh_h_mirror_idx(c + 1, width);
      const float a  = row_a[c];
      const float b  = row_b[c];
      const float cc = row_a[cb];
      const float d  = row_b[cb];
      L[out_off + c] = (a + b + cc + d) * 0.5f;
    }
  }
}

/* [LATEST: GALOSH_RAW_J] gat_j_forward_c_halfres — half-res 2x2 forward
 * WHT extracting ONLY the chroma planes from the full-res GAT-domain
 * raw, at every-other-pixel (stride=2) positions.
 *
 * For RGGB at half-res position (hr, hc), reads in_gat at full-res
 * positions (2hr, 2hc) [TL=R], (2hr+1, 2hc) [BL=Gb], (2hr, 2hc+1)
 * [TR=Gr], (2hr+1, 2hc+1) [BR=B] and computes:
 *   C1[hr,hc] = (R - Gb + Gr - B) / 2
 *   C2[hr,hc] = (R + Gb - Gr - B) / 2
 *   C3[hr,hc] = (R - Gb - Gr + B) / 2
 *
 * This is mathematically identical to (a) running gat_h_forward_wht_stride1
 * to get full-res cycle-spun C, then (b) sub-sampling at even positions.
 * The direct half-res computation is ~4× cheaper and avoids the
 * intermediate full-res C buffers (~150 MB saved at 16 MP).
 *
 * CFA-aligned at every-other-pixel → no sign flips needed (= G's half-res
 * 2x2 WHT convention).  Boundary pixels skipped if (2hr+1, 2hc+1) would
 * land outside the input image.
 *
 * (日) full-res GAT raw から半解像度 C を直接抽出 (stride=2 forward
 *   2x2 WHT)。J Phase 3 の full-res C 経由 sub-sample (Phase 4) を
 *   1 ステップ化。CFA 偶数位置整列なので符号反転不要。 */
static void gat_j_forward_c_halfres(const float *restrict in,
                                    float *restrict C1_h,
                                    float *restrict C2_h,
                                    float *restrict C3_h,
                                    const int width, const int height,
                                    const int halfwidth, const int halfheight)
{
  DT_OMP_FOR()
  for(int hr = 0; hr < halfheight; hr++)
  {
    const int fr0 = 2 * hr;
    const int fr1 = fr0 + 1;
    if(fr1 >= height) continue;  /* skip last row if odd-sized */
    const float *row_a = in + (size_t)fr0 * width;
    const float *row_b = in + (size_t)fr1 * width;
    const size_t hp_off = (size_t)hr * halfwidth;
    for(int hc = 0; hc < halfwidth; hc++)
    {
      const int fc0 = 2 * hc;
      const int fc1 = fc0 + 1;
      if(fc1 >= width) continue;
      const float a  = row_a[fc0];
      const float b  = row_b[fc0];
      const float cc = row_a[fc1];
      const float d  = row_b[fc1];
      C1_h[hp_off + hc] = (a - b + cc - d) * 0.5f;
      C2_h[hp_off + hc] = (a + b - cc - d) * 0.5f;
      C3_h[hp_off + hc] = (a - b - cc + d) * 0.5f;
    }
  }
}

/* [LATEST: GALOSH_RAW_H] gat_h_demodulate_chroma — apply CFA-periodic
 * sign flips to C1/C2/C3 in-place.  Self-inverse (call twice = identity).
 *   Phase 4: forward demodulate (raw cycle-spun WHT → smooth chroma)
 *   Phase 6: re-modulate (smooth chroma → raw cycle-spun WHT)
 *
 *   C1[r,c] *= (-1)^r       (row-periodic; vertical Bayer flip)
 *   C2[r,c] *= (-1)^c       (col-periodic; horizontal Bayer flip)
 *   C3[r,c] *= (-1)^(r+c)   (checkerboard; diagonal Bayer flip)
 *
 * Derivation: at TL=(r+1,c) the 2x2 reads {b, mirror, d, mirror} of
 * the (r,c) block, i.e. the (a,b,c,d) tuple is row-cycled by one Bayer
 * row.  C1=(a−b+c−d)/2 ↦ (b−a+d−c)/2 = −C1 (sign flip).  C2 is invariant
 * under row cycle; C3=(a−b−c+d)/2 ↦ (b−a−d+c)/2 = −C3.  Similarly for
 * column cycle.  Hence the (-1)^r / (-1)^c / (-1)^(r+c) pattern.
 *
 * The pattern is CFA-symmetric: for any of the four Bayer phases
 * (RGGB, GRBG, GBRG, BGGR) the periodicity is the same (only the
 * channel labels at (0,0) differ — irrelevant for the WHT signs). */
static void gat_h_demodulate_chroma(float *restrict C1,
                                    float *restrict C2,
                                    float *restrict C3,
                                    const int width, const int height)
{
  DT_OMP_FOR()
  for(int r = 0; r < height; r++)
  {
    const float sr = (r & 1) ? -1.0f : 1.0f;       /* (-1)^r */
    float *p1 = C1 + (size_t)r * width;
    float *p2 = C2 + (size_t)r * width;
    float *p3 = C3 + (size_t)r * width;
    for(int c = 0; c < width; c++)
    {
      const float sc = (c & 1) ? -1.0f : 1.0f;     /* (-1)^c */
      p1[c] *= sr;
      p2[c] *= sc;
      p3[c] *= sr * sc;                            /* (-1)^(r+c) */
    }
  }
}

/* [LATEST: GALOSH_RAW_H] gat_h_inverse_overlap_add — per-pixel inverse
 * 2x2 WHT averaged over up to 4 cycle-shifted block decompositions.
 *
 * For output pixel (fr, fc), gather contributions from blocks whose
 * TL is at (br, bc):
 *   (fr,   fc  )  → role TL : val = (L+C1+C2+C3)/2
 *   (fr-1, fc  )  → role BL : val = (L-C1+C2-C3)/2
 *   (fr,   fc-1)  → role TR : val = (L+C1-C2-C3)/2
 *   (fr-1, fc-1)  → role BR : val = (L-C1-C2+C3)/2
 * Average over the 1..4 blocks that exist (boundary handling: top-left
 * corner has only TL contribution, top edge has TL+TR, left edge has
 * TL+BL, interior has all 4).
 *
 * (日) 各画素 (fr,fc) は最大 4 個の cycle-shifted 2x2 block 逆変換に
 *     寄与する (TL/BL/TR/BR のいずれかとして)。存在する block の寄与
 *     を平均することで cycle-spinning denoising 効果を得る (Coifman-
 *     Donoho 1995)。
 *
 * Caller must have re-modulated C1/C2/C3 (= Phase 6, identical to Phase 4
 * since demod is self-inverse) before calling this function. */
static void gat_h_inverse_overlap_add(const float *restrict L,
                                      const float *restrict C1,
                                      const float *restrict C2,
                                      const float *restrict C3,
                                      float *restrict out,
                                      const int width, const int height)
{
  DT_OMP_FOR()
  for(int fr = 0; fr < height; fr++)
  {
    for(int fc = 0; fc < width; fc++)
    {
      const size_t p_tl = (size_t)fr * width + fc;
      float sum = 0.0f;
      int count = 0;

      /* role TL @ block (fr, fc) — always exists */
      sum += (L[p_tl] + C1[p_tl] + C2[p_tl] + C3[p_tl]) * 0.5f;
      count++;

      if(fr > 0)
      {
        /* role BL @ block (fr-1, fc) */
        const size_t p_bl = (size_t)(fr - 1) * width + fc;
        sum += (L[p_bl] - C1[p_bl] + C2[p_bl] - C3[p_bl]) * 0.5f;
        count++;
      }
      if(fc > 0)
      {
        /* role TR @ block (fr, fc-1) */
        const size_t p_tr = (size_t)fr * width + (fc - 1);
        sum += (L[p_tr] + C1[p_tr] - C2[p_tr] - C3[p_tr]) * 0.5f;
        count++;
      }
      if(fr > 0 && fc > 0)
      {
        /* role BR @ block (fr-1, fc-1) */
        const size_t p_br = (size_t)(fr - 1) * width + (fc - 1);
        sum += (L[p_br] - C1[p_br] - C2[p_br] + C3[p_br]) * 0.5f;
        count++;
      }

      out[p_tl] = sum / (float)count;
    }
  }
}

/* ================================================================
 * [V2.0 A3] gat_fast_guided_upsample — fast alternative to
 * gat_k16_joint_bilateral_upsample (selected by --upsample=fast).
 *
 * EN: Guided BILINEAR 2x upsample.  Per output pixel only the nearest
 * 2x2 half-res samples contribute (1/2/4 taps depending on sub-pixel
 * phase), weighted bilinear x the SAME bilateral L-guide modulation as
 * K16.  All weights are >= 0, so the result is a convex combination of
 * source samples: jinc-style ringing is impossible BY CONSTRUCTION
 * (the anti-ringing hull clamp is the identity here) and no
 * sign-preserving sum floor is needed.  Cost: <= 4 taps vs K16's 25.
 * Trade-off: the jinc's bandlimit-faithful sharpness is lost (slightly
 * softer chroma) — quality labeled by the V2.0 bench.
 * JP: 最近傍 2x2 のみのガイド付き bilinear（L bilateral 変調は K16 と
 * 同一）。全重み非負の凸結合なのでリンギングは構造的にゼロ
 * （クランプ不要）。jinc の帯域忠実な鋭さは失う（クロマがやや柔らか
 * くなる）代わりに大幅に軽い。品質ラベルは V2.0 ベンチで確定。
 * ================================================================ */
static void gat_fast_guided_upsample(
    const float *restrict c1_h,
    const float *restrict c2_h,
    const float *restrict c3_h,
    const float *restrict L_pixel,
    float *restrict c1_full,
    float *restrict c2_full,
    float *restrict c3_full,
    const int halfwidth, const int halfheight,
    const float BW)
{
  const int fw = 2 * halfwidth;
  const int fh = 2 * halfheight;
  const float inv_2sigma_sq = 1.0f / (2.0f * BW * BW);

  DT_OMP_FOR()
  for(int hy = 0; hy < halfheight; hy++)
  {
    for(int hx = 0; hx < halfwidth; hx++)
    {
      for(int si = 0; si < 4; si++)
      {
        const int sub_dy = si / 2;
        const int sub_dx = si % 2;
        const int fr = 2 * hy + sub_dy;
        const int fc = 2 * hx + sub_dx;
        if(fr >= fh || fc >= fw) continue;

        const float L_c = L_pixel[(size_t)fr * fw + fc];
        /* Bilinear tap weights per sub-pixel phase (offset 0 or +0.5):
         * aligned (0)  -> single tap, weight 1
         * offset (0.5) -> two taps, weights 0.5 / 0.5                 */
        const float wy[2] = { sub_dy ? 0.5f : 1.0f, 0.5f };
        const float wx[2] = { sub_dx ? 0.5f : 1.0f, 0.5f };

        float sum_w = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
        for(int dy = 0; dy <= sub_dy; dy++)
        {
          int hyi = hy + dy;
          if(hyi >= halfheight) hyi = halfheight - 1;
          for(int dx = 0; dx <= sub_dx; dx++)
          {
            int hxi = hx + dx;
            if(hxi >= halfwidth) hxi = halfwidth - 1;

            /* L at the half-res sample's full-res TL position (same
             * convention as K16; boundary-clamped). */
            const int fri = 2 * hyi;
            const int fci = 2 * hxi;
            const float L_i = L_pixel[(size_t)((fri < fh) ? fri : fh - 1) * fw
                                      + ((fci < fw) ? fci : fw - 1)];
            const float dL = L_i - L_c;
            const float w = wy[dy] * wx[dx] * expf(-dL * dL * inv_2sigma_sq);

            const size_t hp = (size_t)hyi * halfwidth + hxi;
            sum_w += w;
            s1 += w * c1_h[hp];
            s2 += w * c2_h[hp];
            s3 += w * c3_h[hp];
          }
        }

        const size_t fp = (size_t)fr * fw + fc;
        if(sum_w > 1e-12f)
        {
          const float inv = 1.0f / sum_w;
          c1_full[fp] = s1 * inv;
          c2_full[fp] = s2 * inv;
          c3_full[fp] = s3 * inv;
        }
        else
        {
          /* bilateral killed all taps (pathological): nearest sample */
          const size_t hp = (size_t)hy * halfwidth + hx;
          c1_full[fp] = c1_h[hp];
          c2_full[fp] = c2_h[hp];
          c3_full[fp] = c3_h[hp];
        }
      }
    }
  }
}

/* =================================================================
 * [LATEST: GALOSH_RAW_L] gat_k16_joint_bilateral_upsample — guide-aware
 * EWA Jinc-Lanczos-3 upsample of half-res chroma to full-res, with
 * per-sample bilateral L weighting (joint bilateral upsample, Kopf et
 * al. SIGGRAPH 2007 / He et al. TPAMI 2010 guided filter framework).
 *
 * Replaces the GALOSH_RAW_K's two-stage chain
 *   Phase 6: galosh_upsample_2x_ewajl3 (L-unaware bandlimit interp)
 *   Phase 8: galosh_loess_chroma_3ch_r (L-guided LOESS post-process)
 * with a single fused stage that combines bandlimit interp AND
 * cross-channel L-edge alignment in one filter pass.
 *
 * Per output pixel (fr, fc), the kernel weight is:
 *   w[i] = w_jinc(d_i) × exp( -(L_pixel[fr,fc] - L_at_h[i])² / (2·BW²) )
 *          ───────────       ──────────────────────────────────────────
 *          K16 bandlimit     bilateral L-edge alignment
 *
 *   C_full[fr,fc] = Σ w[i] · C_h[i] / Σ w[i]
 *
 * In flat L regions: bilateral ≈ uniform → effective kernel = pure
 * jinc → bandlimit-faithful (= K16 standard behavior preserved).
 * At L edges: bilateral kills cross-edge samples → effective kernel
 * = one-sided jinc → C edges snap to L edges via cross-channel prior
 * (= principled demosaic-style super-resolution from CFA likelihood
 * + L-correlated structural prior).
 *
 * The L value at each half-res chroma sample is taken from L_pixel at
 * the sample's full-res TL position (= L_pixel[2·hyi, 2·hxi]).  This
 * is consistent because the half-res C samples were extracted at TL
 * positions of the 2x2 Bayer blocks (= G's half-res convention).
 *
 * Multi-channel: C1/C2/C3 all share the same bilateral L weight per
 * window position → 3-channel fusion saves ~67% of the bilateral
 * compute (1 expf shared vs 3 separate calls) and ~67% of the kernel
 * weight precomputation.
 *
 * (日) K16 EWA-JL3 を L_pixel guide 込みの bilateral 重み付き版に置換。
 *   K の Phase 6 (帯域制限 upsample) + Phase 8 (LOESS edge alignment)
 *   を 1 段融合。flat 部では純 jinc (帯域忠実)、edge 部では bilateral
 *   が cross-edge sample を kill して L edge に snap した chroma を
 *   生成 (cross-channel super-resolution、Kopf 2007 / He 2010 系)。 */
static void gat_k16_joint_bilateral_upsample(
    const float *restrict c1_h,
    const float *restrict c2_h,
    const float *restrict c3_h,
    const float *restrict L_pixel,
    float *restrict c1_full,
    float *restrict c2_full,
    float *restrict c3_full,
    const int halfwidth, const int halfheight,
    const float BW)
{
  const int fw = 2 * halfwidth;
  const int fh = 2 * halfheight;

  /* Sub-pixel offsets: for each output pixel position (fr, fc) within
   * the upsampled grid, the sub-pixel index si = (fr&1)*2 + (fc&1)
   * picks one of 4 jinc kernel orientations. */
  const float subpix[4][2] = {
      {  0.00f,  0.00f },
      {  0.00f, +0.50f },
      { +0.50f,  0.00f },
      { +0.50f, +0.50f },
  };

  const int W  = 2;
  const int kw = 2 * W + 1;

  /* UN-normalized jinc weights per sub-pixel offset.  Bilateral
   * modulation re-normalizes per output pixel (= can't pre-normalize). */
  float jw[4][5][5];
  /* anti-ringing: per-sub-pixel mask of the nearest 2x2 source samples = the
   * convex hull the interpolated chroma must stay inside (standard "+clamp"). */
  unsigned char nearm[4][5][5];
  /* kernel selection for the A/B/C bench: default = EWA jinc-jinc (sharp, band-
   * faithful); GSIG set = Gaussian (no ring but over-blurs chroma detail). */
  const char *_gs = getenv("GSIG");
  const int   use_gauss = (_gs != NULL);
  const float gsig = _gs ? (float)atof(_gs) : 0.8f;
  for(int si = 0; si < 4; si++)
  {
    const float oy = subpix[si][0];
    const float ox = subpix[si][1];
    for(int dy = -W; dy <= W; dy++)
    {
      for(int dx = -W; dx <= W; dx++)
      {
        const float ry = (float)dy - oy;
        const float rx = (float)dx - ox;
        const float r_half = sqrtf(rx * rx + ry * ry);
        const float r_full = r_half * 2.0f;
#ifdef GALOSH_CHROMA_CLAMP
        /* EWA jinc-jinc kept by default (sharp, band-limit-faithful — a pure
         * Gaussian removes ring but over-blurs chroma on some scenes).  The jinc
         * negative-side-lobe RING (= residual magenta from clean input at high-
         * contrast L edges) is removed instead by the nearest-2x2 anti-ringing
         * hull clamp below (ImageMagick/mpv '+clamp' antiring): the interpolated
         * value is bounded to the convex hull of the 4 nearest source samples, so
         * only the overshoot is cut while the sharp interpolation is preserved.
         * EN/JP: jinc の鋭さを保持し ring は最近傍 2x2 凸包 clamp で除去。 */
        float w_val;
        if(use_gauss) w_val = expf(-r_half * r_half / (2.0f * gsig * gsig));
        else          w_val = (r_full < 3.0f)
                            ? galosh_jinc(r_full) * galosh_jinc(r_full / 3.0f) : 0.0f;
        const int ny = (dy == 0) || (oy > 0.25f && dy == 1);
        const int nx = (dx == 0) || (ox > 0.25f && dx == 1);
        nearm[si][dy + W][dx + W] = (ny && nx) ? 1 : 0;
#else
        float w_val = 0.0f;
        if(r_full < 3.0f)
          w_val = galosh_jinc(r_full) * galosh_jinc(r_full / 3.0f);
#endif
        jw[si][dy + W][dx + W] = w_val;
      }
    }
  }

  const float inv_2sigma_sq = 1.0f / (2.0f * BW * BW);
#ifdef GALOSH_CHROMA_CLAMP
  /* Anti-ringing (nearest-2x2 source-sample hull) is the DEFAULT: it removes the
   * jinc side-lobe RING (= residual magenta GALOSH created from clean input at
   * high-contrast L edges) at zero speed cost while preserving jinc sharpness.
   * Validated net-positive (sticks magenta 616->0, no high-ISO break, Matthias
   * detail kept).  Opt out to the legacy conservative full-window band via
   * GALOSH_CLAMP_FULLWIN (kept for A/B reproducibility).  [DEPRECATED: the
   * Gaussian kernel via GSIG removes ring too but over-blurs chroma detail.] */
  const int antiring = (getenv("GALOSH_CLAMP_FULLWIN") == NULL);
#endif

  DT_OMP_FOR()
  for(int hy = 0; hy < halfheight; hy++)
  {
    for(int hx = 0; hx < halfwidth; hx++)
    {
      for(int si = 0; si < 4; si++)
      {
        const int sub_dy = si / 2;
        const int sub_dx = si % 2;
        const int fr = 2 * hy + sub_dy;
        const int fc = 2 * hx + sub_dx;
        if(fr >= fh || fc >= fw) continue;

        const float L_c = L_pixel[(size_t)fr * fw + fc];

        float sum_w = 0.0f;
        float sum_c1 = 0.0f, sum_c2 = 0.0f, sum_c3 = 0.0f;
#ifdef GALOSH_CHROMA_CLAMP
        /* Range of the bilaterally-relevant (positive-bilateral-weight) input
         * chroma samples, used to clamp jinc-ringing overshoot below.
         * lo/hi = full window (conservative); tlo/thi = nearest-2x2 hull (antiring). */
        float lo1= 1e30f, lo2= 1e30f, lo3= 1e30f;
        float hi1=-1e30f, hi2=-1e30f, hi3=-1e30f;
        float tlo1= 1e30f, tlo2= 1e30f, tlo3= 1e30f;
        float thi1=-1e30f, thi2=-1e30f, thi3=-1e30f;
#endif

        for(int dy = -W; dy <= W; dy++)
        {
          int hyi = hy + dy;
          if(hyi < 0)            hyi = 0;
          if(hyi >= halfheight)  hyi = halfheight - 1;
          for(int dx = -W; dx <= W; dx++)
          {
            int hxi = hx + dx;
            if(hxi < 0)           hxi = 0;
            if(hxi >= halfwidth)  hxi = halfwidth - 1;

            /* L value at this half-res chroma sample's full-res TL
             * position.  Boundary-clamped for last odd row/col. */
            const int fri = 2 * hyi;
            const int fci = 2 * hxi;
            const int fri_c = (fri < fh) ? fri : fh - 1;
            const int fci_c = (fci < fw) ? fci : fw - 1;
            const float L_i = L_pixel[(size_t)fri_c * fw + fci_c];

            const float dL = L_i - L_c;
            const float w_bilat = expf(-dL * dL * inv_2sigma_sq);
            const float w = jw[si][dy + W][dx + W] * w_bilat;

            sum_w  += w;
            const size_t hp = (size_t)hyi * halfwidth + hxi;
            sum_c1 += w * c1_h[hp];
            sum_c2 += w * c2_h[hp];
            sum_c3 += w * c3_h[hp];
#ifdef GALOSH_CHROMA_CLAMP
            /* track FULL-window input-chroma range (every contributing sample):
             * a conservative band that bounds gross jinc-ringing overshoot WITHOUT
             * collapsing legitimate chroma at edges (a tight w>thresh band was
             * found to over-clamp and itself create a few magenta specks). */
            {
              const float v1=c1_h[hp], v2=c2_h[hp], v3=c3_h[hp];
              if(v1<lo1)lo1=v1; if(v1>hi1)hi1=v1;
              if(v2<lo2)lo2=v2; if(v2>hi2)hi2=v2;
              if(v3<lo3)lo3=v3; if(v3>hi3)hi3=v3;
              if(nearm[si][dy + W][dx + W]){
                if(v1<tlo1)tlo1=v1; if(v1>thi1)thi1=v1;
                if(v2<tlo2)tlo2=v2; if(v2>thi2)thi2=v2;
                if(v3<tlo3)tlo3=v3; if(v3>thi3)thi3=v3;
              }
            }
#endif
          }
        }

        /* Safety: sum_w can have sign matching the per-sub-pixel jinc
         * sum sign (in flat L regions, all jinc weights have same sign
         * so the sum is well-defined; bilateral mod preserves this).
         * Floor on |sum_w| guards against pathological cases where
         * bilateral kills nearly all weights. */
        /* Floor |sum_w| but PRESERVE ITS SIGN (under GALOSH_CHROMA_CLAMP).  The
         * old `: 1e-6f` forced a POSITIVE floor: when the jinc negative lobes
         * cancel the centre in a high-contrast chroma spot, sum_w drifts to a
         * tiny NEGATIVE value and the positive floor flipped the chroma sign ->
         * a spurious chroma spike (= magenta after the blue WB gain).  Sign-
         * preserving floor mirrors the INT pipeline (galosh_raw_cpu_int.c jinc
         * upsample).  EN/JP: sum_w≈0 時に符号を失い chroma 反転していたバグの符号保持修正。 */
#ifdef GALOSH_CHROMA_CLAMP
        const float safe_w = (fabsf(sum_w) > 1e-6f) ? sum_w
                                                    : ((sum_w < 0.0f) ? -1e-6f : 1e-6f);
#else
        const float safe_w = (fabsf(sum_w) > 1e-6f) ? sum_w : 1e-6f;
#endif
        const float inv_w = 1.0f / safe_w;
        const size_t fp = (size_t)fr * fw + fc;
        float oc1 = sum_c1 * inv_w, oc2 = sum_c2 * inv_w, oc3 = sum_c3 * inv_w;
#ifdef GALOSH_CHROMA_CLAMP
        /* Clamp jinc-sidelobe ringing: the non-convex (negative-sidelobe) jinc
         * weights let the weighted sum exceed the local input chroma band at
         * high-contrast L edges -> chroma overshoot -> magenta after the large
         * blue WB gain.  Clamp the output to the admissible band (= INT-pipeline
         * behaviour, which never showed this).  EN/JP: jinc 負サイドローブ起因の
         * chroma overshoot を入力レンジに clamp。 */
        if(antiring){
          if(thi1 >= tlo1){ oc1 = fminf(fmaxf(oc1, tlo1), thi1); }
          if(thi2 >= tlo2){ oc2 = fminf(fmaxf(oc2, tlo2), thi2); }
          if(thi3 >= tlo3){ oc3 = fminf(fmaxf(oc3, tlo3), thi3); }
        } else {
          if(hi1 >= lo1){ oc1 = fminf(fmaxf(oc1, lo1), hi1); }
          if(hi2 >= lo2){ oc2 = fminf(fmaxf(oc2, lo2), hi2); }
          if(hi3 >= lo3){ oc3 = fminf(fmaxf(oc3, lo3), hi3); }
        }
#endif
        c1_full[fp] = oc1;
        c2_full[fp] = oc2;
        c3_full[fp] = oc3;
      }
    }
  }
}

/* =================================================================
 * [LATEST: GALOSH_RAW_I] I-pipeline helper — L-only overlap-add average.
 *
 * GALOSH_RAW_I is a hybrid pipeline that takes "L from RAW_H, C from
 * RAW_G".  Motivation: H's full-res cycle-spinning eliminates the K14
 * box stair on luma, BUT the cycle-spun C plane has autocov=0.5 noise
 * correlation that LOESS can't fully suppress, leaving long-wavelength
 * chroma blotch visible in DISTS/NIQE/LPIPS metrics.  G's K16 EWA-JL3
 * upsample of half-res C provides bandlimit-faithful chroma (CFA chroma
 * sampling is intrinsically half-Nyquist limited, so K16 loses no
 * signal information by going through half-res).  Combining the two
 * yields the principled best of each plane.
 *
 * Pipeline difference vs H (chroma path only):
 *   I keeps H Phase 0..3 (forward stride=1 cycle-spinning at full-res)
 *   I sub-samples cycle-spun C planes at stride=2 to get half-res C
 *     (= mathematically identical to G's half-res 2x2 WHT on the 4ch
 *     RGGB extract; CFA-aligned at every-other-pixel, no demod needed)
 *   I runs half-res LOESS on C with sub-sampled L_den as Y guide
 *     (= G's Phase 3.5 chroma path)
 *   I upsamples C to full-res via K16 EWA-JL3 (= G's Phase 4(d) chromaup)
 *   I does per-pixel WHT inverse using L from cycle-spun + C from K16
 *
 * Open question solved by gat_i_lpixel_overlap_avg below: how to
 * convert the cycle-spun L plane (where L[r,c] is the L of the 2x2
 * block whose TL is (r,c), implying half-pixel position offset) into
 * a per-pixel L for use with G's K16 inverse formula at pixel (fr,fc).
 *
 * (日) GALOSH_RAW_I: L パスは H 全部 (full-res cycle-spinning + LOSH)、
 *   C パスは G (half-res LOESS + K16 EWA-JL3 chromaup) のハイブリッド。
 *   理論的根拠: CFA chroma サンプリングは元々 half-Nyquist 帯域制限で、
 *   full-res cycle-spinning は C plane に余計な相関ノイズを導入する
 *   だけ。L はその逆で、4ch 全てが luma に寄与 → full-res cycle-spinning
 *   の恩恵を享受できる。
 * ================================================================= */

/* [LATEST: GALOSH_RAW_I] gat_i_lpixel_overlap_avg — derive per-pixel L
 * from cycle-spun L plane via 2x2 overlap average.
 *
 * Cycle-spun L_cs[r,c] = ((a+b+c+d)/2) where (a,b,c,d) are the 2x2 block
 * pixels with TL at (r,c).  L_cs represents the "block luma at half-pixel
 * shifted center (r+0.5, c+0.5)".  To get a pixel-centered luma
 * estimate at (fr,fc), average the 4 cycle-shifted blocks containing
 * (fr,fc) (TL@(fr,fc), BL@(fr-1,fc), TR@(fr,fc-1), BR@(fr-1,fc-1)).
 * The 4-block average covers a 3x3 weighted area centered on (fr,fc)
 * with weights (1,2,1; 2,4,2; 1,2,1)/16 — = the L kernel obtained from
 * 2x2 block overlap, mathematically equivalent to a separable [1,2,1]/4
 * box-tent self-convolution.
 *
 * Boundary: top-left corner has only TL (1 contribution); top edge has
 * TL+TR (2); left edge has TL+BL (2); interior has all 4.
 *
 * (日) cycle-spun L から per-pixel L 推定。L_cs[r,c] は半画素ずれた
 *   block 中心の L なので、(fr,fc) に整合する 4 つの cycle-shifted
 *   block の平均を取ると 3x3 重み付き平均 (1,2,1;2,4,2;1,2,1)/16 に
 *   相当 → 半画素ずれが消えて pixel-centered になる。
 *
 * G's K14 box reconstruct uses 4-tap box on half-res L+C and produces
 * 2x2 stair on diagonals.  This helper uses 4-tap box on FULL-RES
 * cycle-spun L (different grid!) and produces no stair because the
 * input grid is already at full resolution. */
static void gat_i_lpixel_overlap_avg(const float *restrict L_cs,
                                     float *restrict L_pixel,
                                     const int width, const int height)
{
  DT_OMP_FOR()
  for(int fr = 0; fr < height; fr++)
  {
    for(int fc = 0; fc < width; fc++)
    {
      const size_t p_tl = (size_t)fr * width + fc;
      float sum = L_cs[p_tl];
      int count = 1;
      if(fr > 0)
      {
        sum += L_cs[(size_t)(fr - 1) * width + fc];
        count++;
      }
      if(fc > 0)
      {
        sum += L_cs[(size_t)fr * width + (fc - 1)];
        count++;
      }
      if(fr > 0 && fc > 0)
      {
        sum += L_cs[(size_t)(fr - 1) * width + (fc - 1)];
        count++;
      }
      L_pixel[p_tl] = sum / (float)count;
    }
  }
}

/* =================================================================
 * [DEPRECATED: GALOSH_RAW_N] N pipeline helpers — Laplacian pyramid
 * decomposition / reconstruction + L-coupled WHT-LOSH.
 *
 * GALOSH_RAW_N extends the existing single-scale 8x8 WHT-LOSH to:
 *   1. Multi-scale via 3-level Laplacian pyramid (= 8x8 WHT-LOSH at
 *      each pyramid band), capturing noise across spatial frequencies
 *   2. Cross-channel L-coupling for chroma (= per-block BayesShrink
 *      threshold derived from pooled L+C signal variance, leveraging
 *      L_pixel structure as cross-channel prior)
 *
 * This generalizes "Local Shrinkage" (= GALOSH name's "LO" + "SH") to
 *   - Multi-scale Local Shrinkage (= scale generalization)
 *   - Cross-channel coupled shrinkage (= L/C decomposition synergy)
 * keeping GAT × WHT-LOSH × L/C philosophy fully intact.
 * ================================================================= */

/* [DEPRECATED: GALOSH_RAW_N] gat_box_downsample_2x — 2x2 box-average
 * downsample.  Used in Laplacian pyramid decompose step.  Boundary:
 * src dimensions assumed even; if odd, last row/col silently dropped
 * (caller guarantees padding or accepts boundary loss). */
static void gat_box_downsample_2x(const float *restrict src,
                                  float *restrict dst,
                                  const int sw, const int sh)
{
  const int dw = sw / 2;
  const int dh = sh / 2;
  DT_OMP_FOR()
  for(int y = 0; y < dh; y++)
  {
    const int sy = 2 * y;
    const float *row0 = src + (size_t)sy       * sw;
    const float *row1 = src + (size_t)(sy + 1) * sw;
    float *drow = dst + (size_t)y * dw;
    for(int x = 0; x < dw; x++)
    {
      const int sx = 2 * x;
      drow[x] = (row0[sx] + row0[sx + 1] + row1[sx] + row1[sx + 1]) * 0.25f;
    }
  }
}

/* [LATEST: GALOSH_RAW_O] gat_crop_2d_topleft — top-left crop with stride
 * conversion.  Copies src[0..dh, 0..dw] into a tightly-packed dst at
 * stride dw (= different from src's stride sw).  Used at K16 boundaries
 * when source dim (sw, sh) doesn't divide cleanly by 2 in the chroma
 * pyramid round-trip (e.g. SIDD halfwidth=2663 odd → cq_w=1331 → 2*cq_w
 * =2662 < halfwidth, lose 1 col).  Caller guarantees dw <= sw, dh <= sh. */
static inline void gat_crop_2d_topleft(const float *restrict src,
                                        const int sw, const int sh,
                                        float *restrict dst,
                                        const int dw, const int dh)
{
  (void)sh;
  DT_OMP_FOR()
  for(int y = 0; y < dh; y++)
    memcpy(dst + (size_t)y * dw, src + (size_t)y * sw, (size_t)dw * sizeof(float));
}

/* [LATEST: GALOSH_RAW_O] gat_pad_2d_edge — top-left pad with edge
 * replication.  Copies src (sw × sh) into dst (dw × dh) such that
 * dst[0..sh, 0..sw] = src and the remaining right column / bottom row
 * are replicated from src's last col / last row.  Used at K16 output
 * boundaries to convert raw K16 output (= 2*input_w × 2*input_h) back
 * to chsize stride for the smoothstep blend.  Caller guarantees
 * dw >= sw, dh >= sh. */
static inline void gat_pad_2d_edge(const float *restrict src,
                                    const int sw, const int sh,
                                    float *restrict dst,
                                    const int dw, const int dh)
{
  DT_OMP_FOR()
  for(int y = 0; y < dh; y++)
  {
    const int sy = (y < sh) ? y : sh - 1;
    const float *srow = src + (size_t)sy * sw;
    float *drow = dst + (size_t)y * dw;
    /* Copy the in-bounds columns. */
    const int copy_w = (dw < sw) ? dw : sw;
    memcpy(drow, srow, (size_t)copy_w * sizeof(float));
    /* Replicate last column for x in [sw, dw). */
    if(dw > sw)
    {
      const float edge = srow[sw - 1];
      for(int x = sw; x < dw; x++) drow[x] = edge;
    }
  }
}

/* [DEPRECATED: GALOSH_RAW_N] gat_box_replicate_upsample_2x — 2x2
 * nearest-neighbor (box-replicate) upsample, adjoint of
 * gat_box_downsample_2x.  Used in Laplacian pyramid reconstruct step.
 * Each src pixel duplicated into a 2x2 dst block. */
static void gat_box_replicate_upsample_2x(const float *restrict src,
                                          float *restrict dst,
                                          const int sw, const int sh,
                                          const int dw, const int dh)
{
  DT_OMP_FOR()
  for(int sy = 0; sy < sh; sy++)
  {
    const float *srow = src + (size_t)sy * sw;
    const int dy0 = 2 * sy;
    const int dy1 = dy0 + 1;
    if(dy0 >= dh) continue;
    float *drow0 = dst + (size_t)dy0 * dw;
    float *drow1 = (dy1 < dh) ? (dst + (size_t)dy1 * dw) : NULL;
    for(int sx = 0; sx < sw; sx++)
    {
      const float v = srow[sx];
      const int dx0 = 2 * sx;
      const int dx1 = dx0 + 1;
      if(dx0 >= dw) continue;
      drow0[dx0] = v;
      if(dx1 < dw) drow0[dx1] = v;
      if(drow1)
      {
        drow1[dx0] = v;
        if(dx1 < dw) drow1[dx1] = v;
      }
    }
  }
}

/* [DEPRECATED: GALOSH_RAW_N] galosh_pass12_lcoupled_multiorient_blocked —
 * L-coupled BayesShrink + Wiener WHT-LOSH on chroma plane, using
 * cross-channel L plane to inform per-coefficient signal variance
 * estimate.
 *
 * Differs from galosh_pass12_multiorient_blocked (= luma version) in:
 *   - Takes additional L_guide plane (= same resolution as input)
 *   - For each block, decomposes BOTH C_block and L_guide_block via WHT
 *   - BayesShrink threshold derived from pooled (C + L) signal variance
 *     instead of C-only variance
 *
 * Per-block formula:
 *   sigma_y_C²  = sample variance of C AC coefficients
 *   sigma_y_L²  = sample variance of L AC coefficients
 *   sigma_x_pool² = max((sigma_y_C² + sigma_y_L²) / 2 - sigma_n², 0)
 *                 ↑ averaged across L+C, bias-corrected by σ_n²
 *   threshold(k)  = sigma_n² / sqrt(sigma_x_pool²)  per AC coefficient k
 *
 * Rationale: chroma signal is typically correlated with luma signal in
 *   natural images (= edges/textures align across channels).  Using
 *   L's signal variance as a prior for chroma signal variance gives
 *   smaller (= less aggressive) BayesShrink threshold in regions where
 *   L has structure → preserves cross-channel-aligned chroma detail.
 *   In flat L regions, threshold remains aggressive → strong chroma
 *   smoothing (= consistent with GALOSH design intent of strong chroma
 *   denoise where signal is weak).
 *
 * (日) Chroma 用 BayesShrink + Wiener WHT-LOSH の cross-channel 拡張。
 *   Per-block で C + L の WHT 分解を両方計算し、 pool variance で
 *   BayesShrink threshold を決定。 L 構造のある所で chroma detail を
 *   保持、 L 平坦な所で chroma を強く smoothing。
 */
static void galosh_pass12_lcoupled_multiorient_blocked(
    const float *restrict C_in,
    const float *restrict L_guide,
    float *restrict C_out,
    const int width, const int height,
    const float chroma_strength,
    const int block_size,
    const int stride,
    const int n_orient,
    const int use_robust_shrink)
{
  /* For now, delegate to a per-block L-coupled implementation built on
   * top of galosh_pass1/galosh_pass2 mechanism.  Multi-orient and
   * robust-shrink follow the same structure as galosh_pass12_multiorient_blocked.
   *
   * Implementation strategy:
   *   - Pass 1: L-coupled BayesShrink → pilot (per-block WHT, pooled
   *             variance from C + L_guide, soft-threshold C AC)
   *   - Pass 2: empirical Wiener using pilot (= standard galosh_pass2,
   *             but wrapped in cycle-spinning aggregation)
   *
   * For minimal code duplication, we inline the per-block loop here
   * with WHT-LOSH applied to both C and L_guide blocks.  Output is the
   * cycle-spun overlap-add aggregation of denoised C blocks. */

  const int npix = width * height;
  const int rmax = height - block_size;
  const int cmax = width  - block_size;
  const int N_block = block_size * block_size;
  const float sigma_sq = chroma_strength * chroma_strength;
  const float lambda_max = chroma_strength * sqrtf(2.0f * logf((float)N_block));

  float *pilot = dt_alloc_align_float((size_t)npix);
  float *numer = dt_alloc_align_float((size_t)npix);
  float *denom = dt_alloc_align_float((size_t)npix);
  if(!pilot || !numer || !denom)
  {
    if(pilot) dt_free_align(pilot);
    if(numer) dt_free_align(numer);
    if(denom) dt_free_align(denom);
    memcpy(C_out, C_in, sizeof(float) * (size_t)npix);
    return;
  }

  /* ============== Pass 1: L-coupled BayesShrink → pilot ============== */
  memset(numer, 0, sizeof(float) * (size_t)npix);
  memset(denom, 0, sizeof(float) * (size_t)npix);

  #pragma omp parallel
  {
    float *my_numer = dt_alloc_align_float((size_t)npix);
    float *my_denom = dt_alloc_align_float((size_t)npix);
    float block_C[64];   /* assumes block_size <= 8 (= GALOSH_BLOCK_PIXELS) */
    float block_L[64];

    if(my_numer && my_denom)
    {
      memset(my_numer, 0, sizeof(float) * (size_t)npix);
      memset(my_denom, 0, sizeof(float) * (size_t)npix);

      #pragma omp for schedule(dynamic, 4)
      for(int ref_r = 0; ref_r <= rmax; ref_r += stride)
      {
        for(int ref_c = 0; ref_c <= cmax; ref_c += stride)
        {
          /* Extract C and L blocks at same position */
          for(int dy = 0; dy < block_size; dy++)
          {
            const float *src_C_row = C_in   + (ref_r + dy) * width + ref_c;
            const float *src_L_row = L_guide + (ref_r + dy) * width + ref_c;
            float *blk_C_row = block_C + dy * block_size;
            float *blk_L_row = block_L + dy * block_size;
            for(int dx = 0; dx < block_size; dx++)
            {
              blk_C_row[dx] = src_C_row[dx];
              blk_L_row[dx] = src_L_row[dx];
            }
          }

          /* Forward WHT on both blocks (in-place) */
          if(block_size == 8)
          {
            wht2d_8x8(block_C, 0);
            wht2d_8x8(block_L, 0);
          }
          else if(block_size == 4)
          {
            wht2d_4x4(block_C, 0);
            wht2d_4x4(block_L, 0);
          }

          /* Pooled signal variance estimate from L + C AC */
          float sum_sq_C = 0.0f;
          float sum_sq_L = 0.0f;
          for(int i = 1; i < N_block; i++)
          {
            sum_sq_C += block_C[i] * block_C[i];
            sum_sq_L += block_L[i] * block_L[i];
          }
          const float sigma_y_C_sq = sum_sq_C / ((float)(N_block - 1) * (float)N_block);
          const float sigma_y_L_sq = sum_sq_L / ((float)(N_block - 1) * (float)N_block);
          const float sigma_y_pool_sq = (sigma_y_C_sq + sigma_y_L_sq) * 0.5f;
          const float sigma_x_sq = fmaxf(sigma_y_pool_sq - sigma_sq, 0.0f);

          float lambda;
          if(sigma_x_sq < 1e-10f)
            lambda = 1e30f;
          else
          {
            lambda = (sigma_sq / sqrtf(sigma_x_sq)) * sqrtf((float)N_block);
            const float lambda_max_unorm = lambda_max * sqrtf((float)N_block);
            if(lambda > lambda_max_unorm) lambda = lambda_max_unorm;
          }

          /* Hard threshold C AC (preserve DC) */
          int n_nonzero = 1;
          for(int i = 1; i < N_block; i++)
          {
            if(fabsf(block_C[i]) < lambda)
              block_C[i] = 0.0f;
            else
              n_nonzero++;
          }

          /* Inverse WHT */
          if(block_size == 8) wht2d_8x8(block_C, 1);
          else if(block_size == 4) wht2d_4x4(block_C, 1);

          /* Overlap-add accumulate to thread-local buffers */
          const float weight = 1.0f / (float)n_nonzero;
          for(int dy = 0; dy < block_size; dy++)
          {
            const size_t row_off = (size_t)(ref_r + dy) * width + ref_c;
            const float *blk_row = block_C + dy * block_size;
            for(int dx = 0; dx < block_size; dx++)
            {
              my_numer[row_off + dx] += blk_row[dx] * weight;
              my_denom[row_off + dx] += weight;
            }
          }
          (void)use_robust_shrink; (void)n_orient;
        }
      }

      /* Reduce thread-local buffers into shared numer/denom */
      #pragma omp critical
      {
        for(int i = 0; i < npix; i++)
        {
          numer[i] += my_numer[i];
          denom[i] += my_denom[i];
        }
      }
    }
    if(my_numer) dt_free_align(my_numer);
    if(my_denom) dt_free_align(my_denom);
  }

  /* Normalize pilot */
  DT_OMP_FOR()
  for(int i = 0; i < npix; i++)
  {
    pilot[i] = (denom[i] > 1e-10f) ? (numer[i] / denom[i]) : C_in[i];
  }

  /* ============== Pass 2: empirical Wiener (= standard galosh_pass2) ============== */
  galosh_pass2(C_in, pilot, C_out, width, height, chroma_strength, stride);

  dt_free_align(pilot);
  dt_free_align(numer);
  dt_free_align(denom);
}

#pragma GCC diagnostic pop

#endif /* GALOSH_CPU_H */
