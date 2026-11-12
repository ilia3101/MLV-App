/* galosh_raw_cpu.c — GALOSH RAW: fully-blind RAW Bayer denoiser (CPU FP32 reference).
 *
 * GALOSH = Generalized Anscombe LOcal SHrinkage.  Pipeline (8 phases):
 *   P0  Foi-Alenius blind noise estimation -> alpha, sigma^2, dark_ref
 *   P1  GAT forward (variance-stabilize) + per-CFA sigma + unit-variance normalize
 *   P2  dark-reference IRLS (achromatic) + CFA-aware subtract
 *   P3  stride-1 cycle-spun 2x2 WHT -> L (luma) / C1,C2,C3 (chroma), full-res
 *   P4  L kept full-res; chroma sub-sampled to half-res
 *   P5  luma: overlapping-block BayesShrink-MAD + empirical Wiener (cycle-spun);
 *       chroma: 3-level multi-scale LOESS pyramid (Y-guided bilateral)
 *   P6  L_pixel guide + L_h_den
 *   P7  L-guided joint-bilateral (EWA-JL3) chroma upsample to full-res
 *   P8+9  inverse WHT + dark restore + denormalize + inverse GAT -> output
 *
 * No block-matching, no sorting, no training, no noise profile.  The same
 * algorithm ships at four precisions: CPU FP32 (this file) / CPU INT32
 * (galosh_raw_cpu_int.c) / GPU FP32 (galosh_raw_gpu.c) / GPU INT16
 * (galosh_int_*.cl).  See README.md (standalone).
 *
 * Usage: galosh_raw_cpu in.bin out.bin W H galosh <strength> <luma> <chroma> <alpha> <sigma_sq>
 *   I/O = raw float32, row-major, single-channel Bayer in [0,1].  alpha=sigma=0 -> blind.
 *   GALOSH_VERBOSE=1 -> per-phase progress on stderr.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

#include "galosh_cpu.h"

static int g_verbose = 0;  /* set by GALOSH_VERBOSE env; gates per-phase stderr progress */

/* ============================================================
 * [MIXED] Pipeline tuning flags (set from CLI in main()).
 *
 *   g_galosh_stride     : [LATEST] 2 = GALOSH_RAW_G default (75% overlap)
 *                         [ARCHIVED] 1 = full cycle-spinning (variant A,
 *                                        slow, no measurable quality gain)
 *   g_galosh_n_orient   : [LATEST] 1 = GALOSH_RAW_G default
 *                         [ARCHIVED] 4 = 4-orientation WHT averaging
 *                                        (variant B, no quality gain)
 *   g_galosh_lfr_kernel : [LATEST] 0 = box compute_L_fullres (default)
 *                         [ARCHIVED] 1 = EWA-JL3 compute_L_fullres
 *                                        (variant C, no diagonal-artifact
 *                                        improvement vs box)
 *   g_galosh_unified    : [LATEST] 0 = use K14/K15/K16 chromaup pipeline
 *                         [ARCHIVED] 1 = single 4-plane EWA-JL3 upsample
 *                                        (var=1 不変性破壊で denoise 性能↓)
 *   g_galosh_k13_block  : [LATEST] 8 = GALOSH_RAW_G default
 *                         [ARCHIVED] 4 = K13 4x4 grain-scale-matched
 *                                        (K15 8x8 が支配で実効 gain なし)
 *
 * Defaults below produce the [LATEST] GALOSH_RAW_G canonical pipeline.
 * Non-default values are accepted only for bench archive reproducibility.
 * ============================================================ */
static int g_galosh_stride     = 2;   /* [PREVIOUS: GALOSH_RAW_G] */
static int g_galosh_n_orient   = 1;   /* [PREVIOUS: GALOSH_RAW_G] */
static int g_galosh_lfr_kernel = 0;   /* [PREVIOUS: GALOSH_RAW_G] */
static int g_galosh_unified    = 0;   /* [PREVIOUS: GALOSH_RAW_G] */
static int g_galosh_k13_block  = 8;   /* [PREVIOUS: GALOSH_RAW_G] */

/* [V2.0 A2] Full-res luma LOSH block size (--wht=4|8).
 * EN: 8 = canonical O (default, byte-identical to the published pipeline).
 *     4 = fast mode: Phase-5 WHT/shrinkage work scales ~B*log2(B) per pixel,
 *     so 4x4 roughly halves the dominant luma stage in exchange for coarser
 *     grain-scale separation (quality labeled by the V2.0 bench; the GPU
 *     port mirrors this flag).  Distinct from g_galosh_k13_block above,
 *     which selects the ARCHIVED K13 half-res-chroma block and is kept
 *     untouched for bench-archive reproducibility.
 * JP: 8 = 正準 O（既定、公開パイプラインと byte-identical）。4 = fast
 *     モード: Phase 5 支配段の演算量がほぼ半減する代わりに粒状感の分離が
 *     粗くなる（V2.0 ベンチでラベル付け、GPU 移植でも同フラグ）。上の
 *     g_galosh_k13_block（ARCHIVED K13 の半解像度クロマ用）とは別物。 */
static int g_galosh_wht_block  = 8;

/* [V2.0 A3] Chroma upsample kernel (--upsample=jinc|fast).
 * EN: 0 = jinc (canonical O: K16 EWA jinc-jinc + bilateral L guide +
 *     anti-ringing hull clamp, default, byte-identical).  1 = fast:
 *     guided bilinear (<= 4 taps, convex => ring-free by construction);
 *     applied to ALL O-pipeline chroma upsample sites (pyramid
 *     transitions + final half->full).  CPU-oriented speed option —
 *     on GPU the K16 stage is ~1% of frame time (measured 2026-07-09).
 * JP: 0 = 正準 jinc（既定、byte-identical）。1 = fast: ガイド付き
 *     bilinear。O パイプラインの全アップサンプル箇所（ピラミッド遷移
 *     ＋最終 half→full）に適用。CPU 向け速度オプション。 */
static int g_galosh_upsample_fast = 0;

/* [V2.0 A3] Dispatch wrapper: same signature as the two kernels. */
static void gat_chroma_upsample(
    const float *c1_h, const float *c2_h, const float *c3_h,
    const float *L_pixel,
    float *c1_full, float *c2_full, float *c3_full,
    const int halfwidth, const int halfheight, const float BW)
{
  if(g_galosh_upsample_fast)
    gat_fast_guided_upsample(c1_h, c2_h, c3_h, L_pixel,
                             c1_full, c2_full, c3_full,
                             halfwidth, halfheight, BW);
  else
    gat_k16_joint_bilateral_upsample(c1_h, c2_h, c3_h, L_pixel,
                                     c1_full, c2_full, c3_full,
                                     halfwidth, halfheight, BW);
}

/* [LATEST: GALOSH_RAW_M] Variant dispatch.
 *   'm' = GALOSH_RAW_M — H + hierarchical Bayesian Phase 5(C):
 *         R_local=7 LOESS (with σ_local²(x) emission) + R_global=15
 *         LOESS (= scale-doubled prior) + per-pixel inverse-variance
 *         fusion.  chroma_strength acts as σ_n scaling, affecting BOTH
 *         flat-region noise AND edge behavior at FIXED cost.  All
 *         constants principled (CFA Nyquist, scale-doubling, GAT σ_n=1,
 *         data-driven σ_local²(x)).  No magic numbers.
 *   (preserved for bench reproducibility:)
 *   '_' (unused L/K/J/I/H/G dispatch documented below)
 */

/* [PREVIOUS: GALOSH_RAW_L] Variant dispatch (preserved).
 *   'l' = GALOSH_RAW_L — K's K16 + LOESS post-process two-stage chain
 *         FUSED into single guide-aware K16 (joint bilateral upsample).
 *         Replaces K's Phase 6 (K16 unaware) + Phase 8 (LOESS post)
 *         with single Phase 7 that combines bandlimit interp AND
 *         L-edge alignment in one filter pass — bilateral operates on
 *         original half-res C samples (not pre-smoothed) → preserves
 *         sub-pixel edge information.  Kopf 2007 / He 2010 framework.
 *   'k' = [PREVIOUS: GALOSH_RAW_K] — J + Bayesian-correct Phase 8 ε/BW.
 *         Two-stage K16 + LOESS-with-L-guide chain.
 *   'j' = [PREVIOUS: GALOSH_RAW_J] — I + L-guided chroma refinement
 *         (slider-coupled ε, BW=3).
 *   'i' = [PREVIOUS: GALOSH_RAW_I] — hybrid "L from H, C from G" no
 *         L-guided refinement.
 *   'h' = [PREVIOUS: GALOSH_RAW_H] — full-res stride=1 cycle-spinning
 *         forward + 4-block overlap-add inverse.
 *   'g' = [PREVIOUS: GALOSH_RAW_G] — half-res LOSH + K14 box + K15
 *         + K16 EWA-JL3 chromaup.
 * Defaults to 'm' (LATEST).  Overridable via CLI --variant=m|l|k|j|i|h|g. */

/* Pass 1 BayesShrink threshold mode (CLI --pass1=).
 *   0 = baseline (BayesShrink + VisuShrink cap)
 *   1 = a1 (hierarchical empirical Bayes, σ_x_global prior, no cap).
 *       Targets super-clean catastrophic destruction without ad-hoc constants.
 *       See galosh_pass1_blocked prepass for theory. */
int g_galosh_pass1_mode = 0;

/* Phase 1 unified_sigma override (CLI --unified-sigma=X).
 *
 * Phase 1 normally measures unified_sigma from in_gat via per-CFA halfres
 * median MAD (= estimate_gat_sigma_halfres), then divides in_gat by it
 * to enforce effective σ = 1.0 in the normalized space.  Phase 10 multi-
 * plies back by the same unified_sigma before inverse GAT.
 *
 * When X > 0 is provided, Phase 1 SKIPS the measurement and uses X as
 * unified_sigma.  X = 1.0 means "trust Phase 0 (α, σ²) completely; no
 * additional calibration" — useful when an external blind estimator
 * (= Python EM_iter etc.) has already calibrated (α, σ²) and we want
 * Phase 1's safety net to step out of the way.
 *
 * Default 0 = use measured unified_sigma (= production behavior).      */
float g_galosh_unified_sigma_override = 0.0f;

/* Super-clean luma-denoise gate (CLI --super-clean-threshold=X).
 *
 * Catastrophic super-clean destruction (-8 to -12 dB on RawNIND
 * 2pilesofplates_ISO125/160, Blombukett ISO200, Elplint ISO200) was
 * diagnosed as per-block hard threshold killing subtle texture AC
 * coefficients in WHT space.  The destruction is NOT fixable by
 * threshold-magnitude tuning (cliff observation: L=0 → noisy verbatim,
 * L=0.1 → catastrophic loss; any positive threshold kills subtle texture
 * AC < 1 = σ_normalized).  The clean fix is to GATE the entire luma
 * denoise (= Phase 5 Pass 1 + Pass 2) when predicted noise is below
 * visual-perceptual significance.
 *
 * Detection signal: pred_noise_std(s=0.5) = √(α·0.5 + σ²) from Phase 0.
 *   Threshold default 0.0 = disabled (= baseline behavior).
 *   When threshold > 0 and pred_noise_std < threshold, Phase 5 is
 *   bypassed (L_cs_den = L_cs) for that image only — chroma denoise
 *   (Phase 7-9) still runs.
 *
 * Initial test value 0.01 corresponds to ~ ISO 200 sensor noise floor;
 * below this denoising is perceptually unnecessary AND destructive. */
float g_galosh_super_clean_threshold = 0.0f;

/* ============================================================
 * [PREVIOUS: GALOSH_RAW_G] Two structural features adopted unconditionally
 * (commit a48e716).  These cannot be disabled at runtime — they are the
 * defining traits of GALOSH_RAW_G vs pre-G.
 *
 *   (i)  K16 chroma full-res reconstruction (was: "v1 chromaup")
 *        c1/c2/c3 upsampled to full-res via EWA Jinc-Lanczos-3, then
 *        per-pixel inverse 2x2 WHT with Bayer-aware sign tables.
 *        Replaces pre-G block-replicated chroma inverse (visible 2x2
 *        stair-step on diagonal edges).  Var=1 noise invariance preserved.
 *
 *   (ii) Pass1 BayesShrink σ_Y via MAD partial-selection-sort
 *        (was: "v5 robust-MAD").  σ_Y = median(|AC|) / 0.6745.  Robust
 *        to ~25% outlier coefficients (Donoho-Johnstone 1995); kills
 *        spatial noise clusters that pre-G L2 sum_sq mistook for
 *        signal.  Improved pilot propagates to Pass2.
 *
 * Implemented inside galosh_pass12_multiorient_blocked (pass1+pass2
 * wrapper) and the K16 step (d) block of gat_galosh_denoise_rawlc.
 * ============================================================ */

/* ============================================================
 * [ARCHIVED] CFA-protected WHT bin indices (案A "CFA frequency protect").
 * Used only by galosh_pass1_cfa / galosh_pass2_cfa (= legacy stride=4
 * Phase 4 with -DGALOSH_CFA_PROTECT).  GALOSH_RAW_G does NOT protect
 * any WHT bins; the L/C decompose handles CFA structure structurally.
 * ============================================================ */
#define CFA_IDX_HORIZ  1   /* row=0, col=1 → 0*8+1 */
#define CFA_IDX_VERT   8   /* row=1, col=0 → 1*8+0 */
#define CFA_IDX_DIAG   9   /* row=1, col=1 → 1*8+1 */

/* [ARCHIVED] is_cfa_protected — used only by *_cfa archived variants. */
static inline int is_cfa_protected(int i)
{
    return (i == 0 || i == CFA_IDX_HORIZ || i == CFA_IDX_VERT || i == CFA_IDX_DIAG);
}

/* ============================================================
 * [ARCHIVED] SSE 2x2 WHT decompose / reconstruct primitives.
 * Original-design helpers for inline 8x8 patch processing; never used
 * by the current GALOSH_RAW_G pipeline (which decomposes/reconstructs
 * via plane-level loops in gat_galosh_denoise_rawlc Phase 3 and Phase
 * 4(d) K16 chromaup).  Kept as reference for SSE WHT optimisation.
 *
 * Guarded for non-x86 builds: these are unused by the canonical path and
 * would otherwise break Apple Silicon / other ARM targets at compile time.
 * ============================================================ */
#if defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
static inline void wht_decompose_8x8(const float patch[GALOSH_BLOCK_PIXELS],
                                       float lc[4][GALOSH_HALF_PIXELS])
{
  const __m128 half = _mm_set1_ps(0.5f);
  for(int i = 0; i < GALOSH_HALF_BLOCK; i++)
  {
    /* Load 8 floats from even row: [a0,b0,a1,b1,a2,b2,a3,b3] */
    const float *row0 = patch + (2 * i) * GALOSH_BLOCK_SIZE;
    const float *row1 = patch + (2 * i + 1) * GALOSH_BLOCK_SIZE;
    const __m128 r0lo = _mm_loadu_ps(row0);      /* a0,b0,a1,b1 */
    const __m128 r0hi = _mm_loadu_ps(row0 + 4);  /* a2,b2,a3,b3 */
    const __m128 r1lo = _mm_loadu_ps(row1);      /* c0,d0,c1,d1 */
    const __m128 r1hi = _mm_loadu_ps(row1 + 4);  /* c2,d2,c3,d3 */
    /* Deinterleave: a = even cols, b = odd cols */
    const __m128 a = _mm_shuffle_ps(r0lo, r0hi, _MM_SHUFFLE(2, 0, 2, 0)); /* a0,a1,a2,a3 */
    const __m128 b = _mm_shuffle_ps(r0lo, r0hi, _MM_SHUFFLE(3, 1, 3, 1)); /* b0,b1,b2,b3 */
    const __m128 c = _mm_shuffle_ps(r1lo, r1hi, _MM_SHUFFLE(2, 0, 2, 0)); /* c0,c1,c2,c3 */
    const __m128 d = _mm_shuffle_ps(r1lo, r1hi, _MM_SHUFFLE(3, 1, 3, 1)); /* d0,d1,d2,d3 */
    const __m128 ab_sum = _mm_add_ps(a, b);
    const __m128 ab_dif = _mm_sub_ps(a, b);
    const __m128 cd_sum = _mm_add_ps(c, d);
    const __m128 cd_dif = _mm_sub_ps(c, d);
    _mm_storeu_ps(lc[0] + i * GALOSH_HALF_BLOCK, _mm_mul_ps(_mm_add_ps(ab_sum, cd_sum), half));
    _mm_storeu_ps(lc[1] + i * GALOSH_HALF_BLOCK, _mm_mul_ps(_mm_add_ps(ab_dif, cd_dif), half));
    _mm_storeu_ps(lc[2] + i * GALOSH_HALF_BLOCK, _mm_mul_ps(_mm_sub_ps(ab_sum, cd_sum), half));
    _mm_storeu_ps(lc[3] + i * GALOSH_HALF_BLOCK, _mm_mul_ps(_mm_sub_ps(ab_dif, cd_dif), half));
  }
}

/* SSE inverse WHT: 4x4 x 4ch -> 8x8 Bayer patch.
 * Processes all 4 columns simultaneously, interleaving back to 8-wide rows. */
static inline void wht_reconstruct_8x8(const float lc[4][GALOSH_HALF_PIXELS],
                                         float patch[GALOSH_BLOCK_PIXELS])
{
  const __m128 half = _mm_set1_ps(0.5f);
  for(int i = 0; i < GALOSH_HALF_BLOCK; i++)
  {
    const __m128 L  = _mm_loadu_ps(lc[0] + i * GALOSH_HALF_BLOCK);
    const __m128 C1 = _mm_loadu_ps(lc[1] + i * GALOSH_HALF_BLOCK);
    const __m128 C2 = _mm_loadu_ps(lc[2] + i * GALOSH_HALF_BLOCK);
    const __m128 C3 = _mm_loadu_ps(lc[3] + i * GALOSH_HALF_BLOCK);
    const __m128 LC1_sum = _mm_add_ps(L, C1);
    const __m128 LC1_dif = _mm_sub_ps(L, C1);
    const __m128 C2C3_sum = _mm_add_ps(C2, C3);
    const __m128 C2C3_dif = _mm_sub_ps(C2, C3);
    /* even-col (a), odd-col (b) for even row */
    const __m128 a = _mm_mul_ps(_mm_add_ps(LC1_sum, C2C3_sum), half);  /* L+C1+C2+C3 */
    const __m128 b = _mm_mul_ps(_mm_add_ps(LC1_dif, C2C3_dif), half);  /* L-C1+C2-C3 */
    /* even-col (c), odd-col (d) for odd row */
    const __m128 cc = _mm_mul_ps(_mm_sub_ps(LC1_sum, C2C3_sum), half); /* L+C1-C2-C3 */
    const __m128 dd = _mm_mul_ps(_mm_sub_ps(LC1_dif, C2C3_dif), half); /* L-C1-C2+C3 */
    /* Interleave even/odd columns back to 8-wide rows */
    float *out0 = patch + (2 * i) * GALOSH_BLOCK_SIZE;
    float *out1 = patch + (2 * i + 1) * GALOSH_BLOCK_SIZE;
    _mm_storeu_ps(out0,     _mm_unpacklo_ps(a, b));  /* a0,b0,a1,b1 */
    _mm_storeu_ps(out0 + 4, _mm_unpackhi_ps(a, b));  /* a2,b2,a3,b3 */
    _mm_storeu_ps(out1,     _mm_unpacklo_ps(cc, dd)); /* c0,d0,c1,d1 */
    _mm_storeu_ps(out1 + 4, _mm_unpackhi_ps(cc, dd)); /* c2,d2,c3,d3 */
  }
}
#endif /* x86 SSE */


/* ============================================================
 * [ARCHIVED] galosh_pass1_cfa — 案A "CFA frequency protect" Pass1.
 *
 * 8x8 WHT-LOSH on full-res Bayer mosaic with bins {0, 1, 8, 9} held
 * fixed (DC + 3 CFA frequency bins) to "protect" CFA structure from
 * shrinkage.  Theory: hard-threshold all bins except the CFA-locked
 * frequencies.  Bench result: visible 8x8 grid artefact, no measurable
 * colour-shift improvement; archived 2026-04 (案A 棄却済み).
 *
 * GALOSH_RAW_G does NOT use this — it decomposes 4 ch RGGB → L/C1/C2/C3
 * structurally before WHT, removing the need for any frequency masking.
 *
 * Called from gat_galosh_denoise_rawlc legacy stride=4 #else branch when
 * GALOSH_CFA_PROTECT is defined; not reached by default GALOSH_RAW_G build.
 * ============================================================ */
static void galosh_pass1_cfa(const float *restrict input,
                             float *restrict output,
                             const int width, const int height,
                             const float sigma_strength,
                             const int stride)
{
    const int rmax = height - GALOSH_BLOCK_SIZE;
    const int cmax = width  - GALOSH_BLOCK_SIZE;
    const int npix = width * height;

    float *numer = (float *)dt_alloc_align(64, sizeof(float) * npix);
    float *denom = (float *)dt_alloc_align(64, sizeof(float) * npix);
    if(!numer || !denom)
    {
        if(numer) dt_free_align(numer);
        if(denom) dt_free_align(denom);
        memcpy(output, input, sizeof(float) * npix);
        return;
    }
    memset(numer, 0, sizeof(float) * npix);
    memset(denom, 0, sizeof(float) * npix);

    const float sigma_sq = sigma_strength * sigma_strength;
    const float lambda_max = sigma_strength * sqrtf(2.0f * logf((float)GALOSH_BLOCK_PIXELS));

    #pragma omp parallel
    {
        float *my_numer = (float *)dt_alloc_align(64, sizeof(float) * npix);
        float *my_denom = (float *)dt_alloc_align(64, sizeof(float) * npix);
        if(my_numer && my_denom)
        {
        memset(my_numer, 0, sizeof(float) * npix);
        memset(my_denom, 0, sizeof(float) * npix);

        #pragma omp for schedule(dynamic, 4)
        for(int ref_r = 0; ref_r <= rmax; ref_r += stride)
        {
            for(int ref_c = 0; ref_c <= cmax; ref_c += stride)
            {
                float block[GALOSH_BLOCK_PIXELS];
                for(int dy = 0; dy < GALOSH_BLOCK_SIZE; dy++)
                    memcpy(block + dy * GALOSH_BLOCK_SIZE,
                           input + (ref_r + dy) * width + ref_c,
                           GALOSH_BLOCK_SIZE * sizeof(float));

                wht2d_8x8(block, 0);

                /* Estimate sigma_Y^2 from non-CFA AC coefficients */
                float sum_sq = 0.0f;
                int n_ac = 0;
                for(int i = 0; i < GALOSH_BLOCK_PIXELS; i++)
                {
                    if(!is_cfa_protected(i))
                    {
                        sum_sq += block[i] * block[i];
                        n_ac++;
                    }
                }
                const float sigma_y_sq = (n_ac > 0) ? sum_sq / ((float)n_ac * (float)GALOSH_BLOCK_PIXELS) : 0.0f;
                const float sigma_x_sq = fmaxf(sigma_y_sq - sigma_sq, 0.0f);

                float lambda;
                if(sigma_x_sq < 1e-10f)
                    lambda = 1e30f;
                else
                {
                    lambda = (sigma_sq / sqrtf(sigma_x_sq)) * sqrtf((float)GALOSH_BLOCK_PIXELS);
                    const float lambda_max_unorm = lambda_max * sqrtf((float)GALOSH_BLOCK_PIXELS);
                    if(lambda > lambda_max_unorm) lambda = lambda_max_unorm;
                }

                /* Hard threshold: preserve DC + CFA frequency bins */
                int n_nonzero = 4; /* DC + 3 CFA bins always kept */
                for(int i = 0; i < GALOSH_BLOCK_PIXELS; i++)
                {
                    if(is_cfa_protected(i)) continue; /* skip protected bins */
                    if(fabsf(block[i]) < lambda)
                        block[i] = 0.0f;
                    else
                        n_nonzero++;
                }

                wht2d_8x8(block, 1);

                const float weight = 1.0f / (float)n_nonzero;
                for(int dy = 0; dy < GALOSH_BLOCK_SIZE; dy++)
                    for(int dx = 0; dx < GALOSH_BLOCK_SIZE; dx++)
                    {
                        const int pos = (ref_r + dy) * width + (ref_c + dx);
                        const float kw = galosh_kaiser_2d[dy * GALOSH_BLOCK_SIZE + dx];
                        my_numer[pos] += weight * kw * block[dy * GALOSH_BLOCK_SIZE + dx];
                        my_denom[pos] += weight * kw;
                    }
            }
        }

        #pragma omp critical
        {
            for(int i = 0; i < npix; i++)
            {
                numer[i] += my_numer[i];
                denom[i] += my_denom[i];
            }
        }
        } /* end if alloc ok */
        dt_free_align(my_numer);
        dt_free_align(my_denom);
    }

    for(int i = 0; i < npix; i++)
        output[i] = (denom[i] > 1e-10f) ? numer[i] / denom[i] : input[i];

    dt_free_align(numer);
    dt_free_align(denom);
}

/* ============================================================
 * [ARCHIVED] galosh_pass2_cfa — 案A Pass2 paired with galosh_pass1_cfa.
 * Same archival rationale: CFA-protect approach yields 8x8 grid artefact,
 * archived in favour of GALOSH_RAW_G structural L/C decompose.
 * Called from gat_galosh_denoise_rawlc legacy stride=4 #else branch only
 * (GALOSH_CFA_PROTECT compile flag).  Not used by default build.
 * ============================================================ */
static void galosh_pass2_cfa(const float *restrict noisy,
                             const float *restrict pilot,
                             float *restrict output,
                             const int width, const int height,
                             const float sigma_strength,
                             const int stride)
{
    const int rmax = height - GALOSH_BLOCK_SIZE;
    const int cmax = width  - GALOSH_BLOCK_SIZE;
    const int npix = width * height;

    float *numer = (float *)dt_alloc_align(64, sizeof(float) * npix);
    float *denom = (float *)dt_alloc_align(64, sizeof(float) * npix);
    if(!numer || !denom)
    {
        if(numer) dt_free_align(numer);
        if(denom) dt_free_align(denom);
        memcpy(output, noisy, sizeof(float) * npix);
        return;
    }
    memset(numer, 0, sizeof(float) * npix);
    memset(denom, 0, sizeof(float) * npix);

    const float sigma_sq_unorm = sigma_strength * sigma_strength
                                * (float)GALOSH_BLOCK_PIXELS;

    #pragma omp parallel
    {
        float *my_numer = (float *)dt_alloc_align(64, sizeof(float) * npix);
        float *my_denom = (float *)dt_alloc_align(64, sizeof(float) * npix);
        if(my_numer && my_denom)
        {
        memset(my_numer, 0, sizeof(float) * npix);
        memset(my_denom, 0, sizeof(float) * npix);

        #pragma omp for schedule(dynamic, 4)
        for(int ref_r = 0; ref_r <= rmax; ref_r += stride)
        {
            for(int ref_c = 0; ref_c <= cmax; ref_c += stride)
            {
                float blk_noisy[GALOSH_BLOCK_PIXELS];
                float blk_pilot[GALOSH_BLOCK_PIXELS];
                for(int dy = 0; dy < GALOSH_BLOCK_SIZE; dy++)
                {
                    memcpy(blk_noisy + dy * GALOSH_BLOCK_SIZE,
                           noisy + (ref_r + dy) * width + ref_c,
                           GALOSH_BLOCK_SIZE * sizeof(float));
                    memcpy(blk_pilot + dy * GALOSH_BLOCK_SIZE,
                           pilot + (ref_r + dy) * width + ref_c,
                           GALOSH_BLOCK_SIZE * sizeof(float));
                }

                wht2d_8x8(blk_noisy, 0);
                wht2d_8x8(blk_pilot, 0);

                float wiener_energy = 0.0f;
                for(int i = 0; i < GALOSH_BLOCK_PIXELS; i++)
                {
                    float w;
                    if(is_cfa_protected(i))
                    {
                        w = 1.0f; /* DC + CFA bins protected */
                    }
                    else
                    {
                        const float s2 = blk_pilot[i] * blk_pilot[i];
                        w = s2 / (s2 + sigma_sq_unorm);
                        if(w < GALOSH_WIENER_FLOOR) w = GALOSH_WIENER_FLOOR;
                    }
                    blk_noisy[i] *= w;
                    wiener_energy += w * w;
                }

                wht2d_8x8(blk_noisy, 1);

                const float weight = 1.0f / fmaxf(wiener_energy, 1e-6f);
                for(int dy = 0; dy < GALOSH_BLOCK_SIZE; dy++)
                    for(int dx = 0; dx < GALOSH_BLOCK_SIZE; dx++)
                    {
                        const int pos = (ref_r + dy) * width + (ref_c + dx);
                        const float kw = galosh_kaiser_2d[dy * GALOSH_BLOCK_SIZE + dx];
                        my_numer[pos] += weight * kw * blk_noisy[dy * GALOSH_BLOCK_SIZE + dx];
                        my_denom[pos] += weight * kw;
                    }
            }
        }

        #pragma omp critical
        {
            for(int i = 0; i < npix; i++)
            {
                numer[i] += my_numer[i];
                denom[i] += my_denom[i];
            }
        }
        } /* end if alloc ok */
        dt_free_align(my_numer);
        dt_free_align(my_denom);
    }

    for(int i = 0; i < npix; i++)
        output[i] = (denom[i] > 1e-10f) ? numer[i] / denom[i] : noisy[i];

    dt_free_align(numer);
    dt_free_align(denom);
}


/* ================================================================
 * [PREVIOUS: GALOSH_RAW_G] compute_L_fullres — Phase 4(b) K14 (box variant).
 *
 * Used by gat_galosh_denoise_rawlc Phase 4(b) to build full-res L from
 * half-res L+C1/C2/C3, in TWO calls (noisy + pilot) which feed the
 * full-res Pass2 Wiener (Phase 4(c) K15).  This is the canonical box
 * 4-tap variant; the EWA-JL3 alternative is at galosh_compute_L_fullres_ewajl3
 * (= [ARCHIVED] variant C).
 *
 * Compute L_fullres from half-res L/C planes.
 *
 * Half-res L/C produces one L value per 2x2 Bayer block.
 * Under strong shrinkage this creates a visible 2x2 plateau artifact.
 * To eliminate it we compute L at every raw pixel position using
 * a sliding 2x2 WHT-DC.
 *
 * Derivation -- inv-WHT of sliding 2x2 blocks yields:
 *   L_fullres(2hy,   2hx)   = L(hy,hx)
 *   L_fullres(2hy,   2hx+1) = [(L-C2)@(hy,hx) + (L+C2)@(hy,hx+1)] / 2
 *   L_fullres(2hy+1, 2hx)   = [(L-C1)@(hy,hx) + (L+C1)@(hy+1,hx)] / 2
 *   L_fullres(2hy+1, 2hx+1) = sum [(L +/- C1 +/- C2 +/- C3) at 4 blocks] / 4
 *
 * Output: L_out of size (2*halfwidth) x (2*halfheight) = full-res.
 * ================================================================ */
static void compute_L_fullres(const float *restrict L,
                               const float *restrict C1,
                               const float *restrict C2,
                               const float *restrict C3,
                               const int halfwidth, const int halfheight,
                               float *restrict L_out)
{
  const int fw = 2 * halfwidth;

  DT_OMP_FOR()
  for(int hy = 0; hy < halfheight; hy++)
  {
    for(int hx = 0; hx < halfwidth; hx++)
    {
      const size_t hi = (size_t)hy * halfwidth + hx;
      /* Neighbor indices (clamped at boundaries) */
      const int hx1 = MIN(hx + 1, halfwidth - 1);
      const int hy1 = MIN(hy + 1, halfheight - 1);
      const size_t hi_r  = (size_t)hy  * halfwidth + hx1;
      const size_t hi_d  = (size_t)hy1 * halfwidth + hx;
      const size_t hi_dr = (size_t)hy1 * halfwidth + hx1;

      const int fr = 2 * hy, fc = 2 * hx;

      /* (2hy, 2hx): block-aligned = L itself */
      L_out[(size_t)fr * fw + fc] = L[hi];

      /* (2hy, 2hx+1): horizontal sliding */
      L_out[(size_t)fr * fw + fc + 1]
        = ((L[hi] - C2[hi]) + (L[hi_r] + C2[hi_r])) * 0.5f;

      /* (2hy+1, 2hx): vertical sliding */
      L_out[(size_t)(fr + 1) * fw + fc]
        = ((L[hi] - C1[hi]) + (L[hi_d] + C1[hi_d])) * 0.5f;

      /* (2hy+1, 2hx+1): diagonal sliding */
      {
        const float Ls = L[hi] + L[hi_r] + L[hi_d] + L[hi_dr];
        const float C1s = -C1[hi] - C1[hi_r] + C1[hi_d] + C1[hi_dr];
        const float C2s = -C2[hi] + C2[hi_r] - C2[hi_d] + C2[hi_dr];
        const float C3s =  C3[hi] - C3[hi_r] - C3[hi_d] + C3[hi_dr];
        L_out[(size_t)(fr + 1) * fw + fc + 1]
          = (Ls + C1s + C2s + C3s) * 0.25f;
      }
    }
  }
}


/* ================================================================
 * [ARCHIVED] galosh_compute_L_fullres_ewajl3 — variant C (jaggy-fix)
 *
 * Drop-in alternative to compute_L_fullres above (= Phase 4(b) K14).
 * Only used when g_galosh_lfr_kernel == 1 (--lfr-kernel=ewajl3 CLI flag).
 * GALOSH_RAW_G default uses the box variant; bench archive needs this for
 * variant-C reproducibility (PSNR -0.5 dB vs box, archived 2026-04).
 *
 * EWA jinc-windowed-jinc 3-tap (Lanczos-Jinc-3) compute_L_fullres
 * variant — RAW-specific archived experiment (variant C).
 *
 * Replaces the legacy box-like 4-tap reconstruction (compute_L_fullres
 * above) with a circularly symmetric, band-limited reconstruction
 * kernel.  Active only when --lfr-kernel=ewajl3 is passed; otherwise
 * the legacy compute_L_fullres is used.  Archived because pure EWA-JL3
 * reconstruction at the 4 sub-pixel positions used by the inverse 2x2
 * WHT introduces ~1 px shift relative to the legacy box convention
 * (PSNR -0.5 dB in bench).  Kept here for reproducibility of the
 * variant-C numbers in the bench archive.
 *
 * Lives in galosh_raw_cpu.c rather than galosh_core.h because the
 * subpixel offsets and the 4-channel API are RAW-Bayer-specific; the
 * generic 2x EWA-JL3 upsample for v1 chromaup chroma upsampling stays
 * in galosh_core.h as galosh_upsample_2x_ewajl3().
 *
 * Sub-pixel offsets (half-res units, relative to half-res sample
 * centre): TL(-0.25,-0.25), TR(-0.25,+0.25), BL(+0.25,-0.25),
 * BR(+0.25,+0.25).  We sample a 5x5 half-res window and apply a
 * precomputed normalized weight table per sub-pixel.
 * ================================================================ */
static void galosh_compute_L_fullres_ewajl3(const float *restrict L,
                                             const float *restrict C1,
                                             const float *restrict C2,
                                             const float *restrict C3,
                                             const int halfwidth, const int halfheight,
                                             float *restrict L_out)
{
    /* C1/C2/C3 unused: pure spatial reconstruction from the L plane.
     * The chroma-derived L refinement of the legacy formulas is sacrificed
     * for bandlimited diagonal accuracy -- this trade-off is intentional
     * for variant C and is part of what the bench is measuring. */
    (void)C1; (void)C2; (void)C3;

    const int fw = 2 * halfwidth;
    const int fh = 2 * halfheight;

    /* NOTE on alignment.
     *
     * Pure EWA-JL3 reconstruction at the geometric pixel centre offset
     * (TL at (-0.25,-0.25) half-res, etc.) produces a clean L plane that
     * is band-limited but is HALF A PIXEL DOWN-RIGHT of legacy's
     * block-centroid-stored-at-TL convention.  The downstream inverse
     * 2x2 WHT was tuned around legacy's built-in shift, so plain EWA-
     * JL3 ends up displaying RGGB output shifted ~1 px DOWN-RIGHT
     * relative to the base / A / B variants, as flagged in the dartboard
     * bench.
     *
     * Trying to match legacy by sampling at offsets {0, +0.5, +0.5, +0.5}
     * collides with jinc's negative ring at ~r_full=1.41 and produces
     * numerically unstable EWA weights for the BR position (only four
     * samples land inside support, all with negative inner-jinc and
     * positive outer-jinc contributions, leaving wsum tiny and the
     * normalised kernel magnifying small noise into huge values).
     *
     * Conclusion: C-1 (pure EWA-JL3 on L) is structurally incompatible
     * with the legacy WHT inverse pipeline.  The right fix is C-2 --
     * keep legacy compute_L_fullres formulas (which have the correct
     * built-in shift convention) and add a band-limited refinement on
     * top.  Until C-2 is implemented we stay with the original geometric
     * offsets here, which the user already saw shifts by ~1 px but at
     * least produces stable values to feed into the bench. */
    const float subpix[4][2] = {
        { -0.25f, -0.25f },  /* TL */
        { -0.25f, +0.25f },  /* TR */
        { +0.25f, -0.25f },  /* BL */
        { +0.25f, +0.25f },  /* BR */
    };

    /* Precompute 4 normalized 5x5 weight tables. */
    const int W = 2;          /* half-window radius in half-res samples */
    const int kw = 2 * W + 1; /* 5 */
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
                const float r_full = r_half * 2.0f;  /* output pixel units */
                float w_val = 0.0f;
                if(r_full < 3.0f)
                    w_val = galosh_jinc(r_full) * galosh_jinc(r_full / 3.0f);
                weights[si][dy + W][dx + W] = w_val;
                wsum += w_val;
            }
        }
        const float inv_wsum = 1.0f / fmaxf(wsum, 1e-20f);
        for(int dy = 0; dy < kw; dy++)
            for(int dx = 0; dx < kw; dx++)
                weights[si][dy][dx] *= inv_wsum;
    }

    /* Apply weights to produce 4 full-res samples per half-res block. */
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
                        sum += L[(size_t)hyi * halfwidth + hxi]
                             * weights[si][dy + W][dx + W];
                    }
                }
                L_out[(size_t)fr * fw + fc] = sum;
            }
        }
    }
}


/* ================================================================
 *  GALOSH -- Full pipeline (RAW L/C decomposed)
 *
 *  Pre-demosaic denoiser operating on RGGB before WB and demosaic.
 *  Fully blind: no white-balance coefficients, no per-channel QE prior.
 *
 *  Pipeline:
 *   1. Half-res RGGB extraction -> piecewise C1 GAT -> RMS unified sigma-normalize
 *   2. Self-consistent dark anchor (per-ch DC subtract) -> WHT -> L/C1/C2/C3
 *   3. Pass 1: BayesShrink hard-threshold pilot on each half-res L/C plane
 *   4. Pass 2: Wiener shrinkage on each half-res L/C plane (independent sigma_L / sigma_C)
 *   5. Full-res L: sliding WHT-DC -> GALOSH Pass 2 on L plane
 *   6. Inverse WHT with full-res L + half-res C (anchor restore)
 *   7. sigma-denormalize -> exact inverse GAT -> write back
 *
 *  Key difference from BM3D: GALOSH operates on ONE plane at a time
 *  (L, C1, C2, C3 independently). No block matching, no non-local processing.
 *  GAT normalization ensures noise is i.i.d. Gaussian (sigma=1), so local
 *  adaptive shrinkage in the WHT domain is sufficient.
 *
 *  Theoretical highlights:
 *  (a) Piecewise C1 VST (extends Foi GAT to all reals)
 *  (b) Unified sigma normalization (RMS, makes Var[L]=Var[Ck]=1 exactly)
 *  (c) Self-consistent dark anchor (per-channel DC protection)
 *  See detailed derivations in comments below.
 *
 *  Ref: Danielyan et al. "Cross-color BM3D" (LNLA 2009) -- L/C decomposition
 *       Foi et al. (Sig.Proc. 2008) -- Poisson-Gaussian noise model
 *       Makitalo & Foi (TIP 2013) -- exact unbiased inverse GAT
 *       Chang, Yu & Vetterli (IEEE TIP 2000) -- BayesShrink
 *       Cleveland (J. Am. Stat. Assoc., 1979) -- LOESS (see galosh_core.h)
 * ================================================================ */
/* ================================================================
 * [PREVIOUS: GALOSH_RAW_G] gat_galosh_denoise_rawlc — main entry point.
 *
 * Orchestrates the full GALOSH_RAW_G pipeline.  Called by main() (CLI)
 * and by darktable's process() (via the bayer.h port).  Branches at
 * the bottom on `#ifndef GALOSH_LEGACY`:
 *   #ifndef GALOSH_LEGACY (default)  → [LATEST] half-res LOSH +
 *                                       K14/K15/K16 chromaup
 *   #else                            → [ARCHIVED] full-res raw WHT-LOSH
 *                                       + block-replicated chroma
 * ================================================================ */
static void galosh_raw_denoise_smallinput(const float *const restrict in,
                                       float *const restrict out,
                                       const dt_iop_roi_t *const roi,
                                       const float luma_strength,
                                       const float chroma_strength,
                                       const uint32_t filters)
{
  const int width = roi->width, height = roi->height;
  const size_t npixels = (size_t)width * height;
  memcpy(out, in, sizeof(float) * npixels);

  if(luma_strength <= 0.0f) return;
  if(width < GALOSH_BLOCK_SIZE * 2 || height < GALOSH_BLOCK_SIZE * 2) return;

  const int co_row = 0, co_col = 0;
  (void)filters;

  const int halfwidth  = (width  + 1) / 2;
  const int halfheight = (height + 1) / 2;
  const size_t chsize  = (size_t)halfwidth * halfheight;

  /* ================================================================
   * [LATEST: GALOSH_RAW_L] Phase 0 — Foi-Alenius blind α / σ²
   * estimation.  Identical to K/J/I/H/G Phase 0.
   * ================================================================ */
  const galosh_noise_params_t np = galosh_estimate_noise(in, width, height);
  gat_build_inverse_table(np.alpha, np.sigma_sq);

  /* Pre-declare buffers for cleanup-on-error. */
  float *in_gat = NULL;
  float *L_cs = NULL;
  float *L_cs_den = NULL;
  float *L_h_den = NULL;
  float *C1_h = NULL, *C2_h = NULL, *C3_h = NULL;
  float *C1_h_den = NULL, *C2_h_den = NULL, *C3_h_den = NULL;
  float *C1_aligned = NULL, *C2_aligned = NULL, *C3_aligned = NULL;
  float *L_pixel = NULL;

  in_gat = dt_alloc_align_float(npixels);
  if(!in_gat) goto cleanup_rawlc_l;

  /* ================================================================
   * [LATEST: GALOSH_RAW_L] Phase 1 — GAT forward (full-res) + per-CFA
   * σ_GAT MAD + RMS unified_sigma + scalar normalize.  Identical to K.
   * ================================================================ */
  DT_OMP_FOR()
  for(size_t i = 0; i < npixels; i++)
    in_gat[i] = gat_forward(in[i], np.alpha, np.sigma_sq);

  float sigma_gat_ch[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  for(int s = 0; s < 4; s++)
  {
    const int ro = ((s & 1)        + co_row) & 1;
    const int co = (((s >> 1) & 1) + co_col) & 1;
    const int hw = (width  - co + 1) / 2;
    const int hh = (height - ro + 1) / 2;
    if(hw < 4 || hh < 4) continue;

    float *tmp = dt_alloc_align_float((size_t)hw * hh);
    if(!tmp) continue;
    DT_OMP_FOR()
    for(int rr = 0; rr < hh; rr++)
      for(int cc = 0; cc < hw; cc++)
        tmp[(size_t)rr * hw + cc] = in_gat[(size_t)(ro + 2*rr) * width + (co + 2*cc)];
    sigma_gat_ch[s] = estimate_gat_sigma_halfres(tmp, hw, hh);
    dt_free_align(tmp);
  }

  const float mean_var = 0.25f * (sigma_gat_ch[0]*sigma_gat_ch[0]
                                + sigma_gat_ch[1]*sigma_gat_ch[1]
                                + sigma_gat_ch[2]*sigma_gat_ch[2]
                                + sigma_gat_ch[3]*sigma_gat_ch[3]);
  const float unified_sigma = sqrtf(fmaxf(mean_var, 1e-12f));
  const float inv_sg = 1.0f / unified_sigma;

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_L] alpha=%.8f sigma_sq=%.10f | "
                  "unified_sigma=%.4f [RMS] (per-ch: %.4f %.4f %.4f %.4f) | "
                  "size=%dx%d (half=%dx%d) | sigma_L=%.3f sigma_C=%.3f\n",
                  np.alpha, np.sigma_sq, unified_sigma,
                  sigma_gat_ch[0], sigma_gat_ch[1], sigma_gat_ch[2], sigma_gat_ch[3],
                  width, height, halfwidth, halfheight, luma_strength, chroma_strength);

  DT_OMP_FOR()
  for(size_t i = 0; i < npixels; i++) in_gat[i] *= inv_sg;

  /* ================================================================
   * [LATEST: GALOSH_RAW_L] Phase 2 — dark_ref IRLS + per-pixel subtract.
   * Identical to K/J Phase 2.
   * ================================================================ */
  float ch_dark_ref[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  {
    const double s_init = (double)np.sigma_sq / fmax((double)np.alpha, 1e-12);
    double s_scale = s_init;
    const double s_min = 0.05 * s_init;
    const double s_max = 50.0 * s_init;
    const int n_iter = 2;

    for(int iter = 0; iter <= n_iter; iter++)
    {
      const double inv_s = 1.0 / fmax(s_scale, 1e-20);
      double sum_w = 0.0;
      double sum_w0 = 0.0, sum_w1 = 0.0, sum_w2 = 0.0, sum_w3 = 0.0;

      #pragma omp parallel for collapse(2) schedule(static) \
              reduction(+:sum_w,sum_w0,sum_w1,sum_w2,sum_w3)
      for(int br = 0; br < height - 1; br += 2)
        for(int bc = 0; bc < width - 1; bc += 2)
        {
          const float g0 = in_gat[(size_t)br     * width + bc    ];
          const float g1 = in_gat[(size_t)(br+1) * width + bc    ];
          const float g2 = in_gat[(size_t)br     * width + bc + 1];
          const float g3 = in_gat[(size_t)(br+1) * width + bc + 1];
          const float ch_max = fmaxf(fmaxf(g0, g1), fmaxf(g2, g3));
          const float ch_min = fminf(fminf(g0, g1), fminf(g2, g3));
          if(ch_max - ch_min > GALOSH_ACHROMATIC_RANGE) continue;

          const float iv0 = in[(size_t)br     * width + bc    ];
          const float iv1 = in[(size_t)(br+1) * width + bc    ];
          const float iv2 = in[(size_t)br     * width + bc + 1];
          const float iv3 = in[(size_t)(br+1) * width + bc + 1];
          const double L_raw = (iv0 + iv1 + iv2 + iv3) * 0.25;
          const double r = L_raw * inv_s;
          const double r2 = r * r;
          const double w = 1.0 / (1.0 + r2 * r2);
          sum_w  += w;
          sum_w0 += w * g0;
          sum_w1 += w * g1;
          sum_w2 += w * g2;
          sum_w3 += w * g3;
        }

      const double inv_sw = 1.0 / fmax(sum_w, 1e-20);
      ch_dark_ref[0] = (float)(sum_w0 * inv_sw);
      ch_dark_ref[1] = (float)(sum_w1 * inv_sw);
      ch_dark_ref[2] = (float)(sum_w2 * inv_sw);
      ch_dark_ref[3] = (float)(sum_w3 * inv_sw);

      if(iter == n_iter) break;

      double sum_wresid2 = 0.0;
      double sum_wW = 0.0;
      const float dr0 = ch_dark_ref[0], dr1 = ch_dark_ref[1];
      const float dr2 = ch_dark_ref[2], dr3 = ch_dark_ref[3];
      #pragma omp parallel for collapse(2) schedule(static) \
              reduction(+:sum_wresid2,sum_wW)
      for(int br = 0; br < height - 1; br += 2)
        for(int bc = 0; bc < width - 1; bc += 2)
        {
          const float g0 = in_gat[(size_t)br     * width + bc    ];
          const float g1 = in_gat[(size_t)(br+1) * width + bc    ];
          const float g2 = in_gat[(size_t)br     * width + bc + 1];
          const float g3 = in_gat[(size_t)(br+1) * width + bc + 1];
          const float ch_max = fmaxf(fmaxf(g0, g1), fmaxf(g2, g3));
          const float ch_min = fminf(fminf(g0, g1), fminf(g2, g3));
          if(ch_max - ch_min > GALOSH_ACHROMATIC_RANGE) continue;

          const float iv0 = in[(size_t)br     * width + bc    ];
          const float iv1 = in[(size_t)(br+1) * width + bc    ];
          const float iv2 = in[(size_t)br     * width + bc + 1];
          const float iv3 = in[(size_t)(br+1) * width + bc + 1];
          const double L_raw = (iv0 + iv1 + iv2 + iv3) * 0.25;
          const double r = L_raw * inv_s;
          const double r2 = r * r;
          const double w = 1.0 / (1.0 + r2 * r2);
          const double d0 = (double)g0 - dr0;
          const double d1 = (double)g1 - dr1;
          const double d2 = (double)g2 - dr2;
          const double d3 = (double)g3 - dr3;
          const double resid2 = d0*d0 + d1*d1 + d2*d2 + d3*d3;
          sum_wW      += w;
          sum_wresid2 += w * resid2 * 0.25;
        }
      const double inv_sw2 = 1.0 / fmax(sum_wW, 1e-20);
      const double measured_std = sqrt(fmax(sum_wresid2 * inv_sw2, 1e-20));
      const double ratio = 1.0 / measured_std;
      s_scale *= sqrt(ratio);
      if(s_scale < s_min) s_scale = s_min;
      if(s_scale > s_max) s_scale = s_max;
    }

    if(g_verbose) fprintf(stderr, "[GALOSH_RAW_L] dark anchor: s_init=%.6e s_final=%.6e | "
                    "ch_dark_ref: [0]=%.4f [1]=%.4f [2]=%.4f [3]=%.4f\n",
            s_init, s_scale,
            ch_dark_ref[0], ch_dark_ref[1], ch_dark_ref[2], ch_dark_ref[3]);

    DT_OMP_FOR()
    for(int r = 0; r < height; r++)
    {
      const int r_off = (r - co_row) & 1;
      for(int c = 0; c < width; c++)
      {
        const int c_off = (c - co_col) & 1;
        const int slot  = r_off | (c_off << 1);
        in_gat[(size_t)r * width + c] -= ch_dark_ref[slot];
      }
    }
  }

  /* ================================================================
   * [LATEST: GALOSH_RAW_L] Phase 3 — stride=1 forward 2x2 WHT (L-only).
   * Identical to K/J Phase 3.
   * ================================================================ */
  L_cs = dt_alloc_align_float(npixels);
  if(!L_cs) goto cleanup_rawlc_l;
  gat_h_forward_l_only_stride1(in_gat, L_cs, width, height);

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_L] Phase 3 forward WHT (stride=1, L-only) done\n");

  /* ================================================================
   * [LATEST: GALOSH_RAW_L] Phase 4 — half-res chroma extract.
   * Identical to K/J Phase 4.
   * ================================================================ */
  C1_h = dt_alloc_align_float(chsize);
  C2_h = dt_alloc_align_float(chsize);
  C3_h = dt_alloc_align_float(chsize);
  if(!C1_h || !C2_h || !C3_h) goto cleanup_rawlc_l;
  gat_j_forward_c_halfres(in_gat, C1_h, C2_h, C3_h,
                           width, height, halfwidth, halfheight);

  dt_free_align(in_gat); in_gat = NULL;

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_L] Phase 4 half-res chroma extract done\n");

  /* ================================================================
   * [LATEST: GALOSH_RAW_L] Phase 5 — denoise.
   *   5(L) full-res LOSH on L_cs (= K Phase 5(L))
   *   5(C) half-res 3-channel LOESS on C with sub-sampled L_cs_den
   *        as Y guide (= K Phase 5(C))
   * ================================================================ */
  L_cs_den = dt_alloc_align_float(npixels);
  if(!L_cs_den) goto cleanup_rawlc_l;
  galosh_pass12_multiorient_blocked(L_cs, L_cs_den, width, height,
                                     luma_strength, /*block=*/8,
                                     /*stride=*/2, /*n_orient=*/1,
                                     /*use_robust_shrink=*/1);
  dt_free_align(L_cs); L_cs = NULL;
  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_L] Phase 5(L) full-res cycle-spun L Pass1+2 done\n");

  L_h_den  = dt_alloc_align_float(chsize);
  C1_h_den = dt_alloc_align_float(chsize);
  C2_h_den = dt_alloc_align_float(chsize);
  C3_h_den = dt_alloc_align_float(chsize);
  if(!L_h_den || !C1_h_den || !C2_h_den || !C3_h_den) goto cleanup_rawlc_l;

  DT_OMP_FOR()
  for(int hr = 0; hr < halfheight; hr++)
  {
    const int fr = 2 * hr;
    if(fr >= height) continue;
    for(int hc = 0; hc < halfwidth; hc++)
    {
      const int fc = 2 * hc;
      if(fc >= width) continue;
      L_h_den[(size_t)hr * halfwidth + hc] = L_cs_den[(size_t)fr * width + fc];
    }
  }

  galosh_loess_chroma_3ch_r(L_h_den, C1_h, C2_h, C3_h,
                             C1_h_den, C2_h_den, C3_h_den,
                             halfwidth, halfheight, chroma_strength,
                             /*R=*/GALOSH_LOESS_RADIUS,
                             /*BW=*/GALOSH_LOESS_BW);
  dt_free_align(L_h_den); L_h_den = NULL;
  dt_free_align(C1_h); C1_h = NULL;
  dt_free_align(C2_h); C2_h = NULL;
  dt_free_align(C3_h); C3_h = NULL;

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_L] Phase 5(C) half-res chroma LOESS done "
                   "(sigma_C=%.3f, R=%d, BW=%.1f, 3ch fused)\n",
          chroma_strength, GALOSH_LOESS_RADIUS, GALOSH_LOESS_BW);

  /* ================================================================
   * [LATEST: GALOSH_RAW_L] Phase 6 — L_pixel = 2x2 overlap average of
   * L_cs_den.  Computed BEFORE Phase 7 (vs K where it was AFTER K16
   * upsample) because the joint bilateral upsample needs L_pixel as
   * its bilateral guide.
   * ================================================================ */
  L_pixel = dt_alloc_align_float(npixels);
  if(!L_pixel) goto cleanup_rawlc_l;
  gat_i_lpixel_overlap_avg(L_cs_den, L_pixel, width, height);
  dt_free_align(L_cs_den); L_cs_den = NULL;

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_L] Phase 6 L_pixel overlap-avg done\n");

  /* ================================================================
   * [LATEST: GALOSH_RAW_L] Phase 7 ⭐ NEW vs K ⭐ — Joint bilateral
   * K16 EWA-JL3 upsample.  Replaces K's Phase 6 (K16 standard upsample)
   * AND K's Phase 8 (LOESS edge-alignment refinement) with a single
   * fused stage:
   *
   *   w_combined[i] = w_jinc(d_i) × exp(-(L_pixel - L_at_h[i])²/(2BW²))
   *   C_full[fr,fc] = Σ w_combined[i] · C_h[i] / Σ w_combined[i]
   *
   * BW = 1.5 (= K Phase 8 BW, = "tight" bilateral for moderate-edge
   * separation; safe at this stage because input L_pixel residual σ
   * ≈ 0.1 << 1.5 in GAT-norm space).
   *
   * In flat L regions: bilateral ≈ uniform → effective kernel = pure
   * jinc → bandlimit-faithful (matches K16 standard exactly).
   * At L edges: bilateral kills cross-edge half-res samples → effective
   * kernel = one-sided jinc → C edges snap to L edges via cross-channel
   * structural prior.  Information advantage over K's chained approach:
   * the bilateral operates on the ORIGINAL half-res C samples (not the
   * pre-smoothed K16 output), preserving sub-pixel edge information.
   * ================================================================ */
  C1_aligned = dt_alloc_align_float(npixels);
  C2_aligned = dt_alloc_align_float(npixels);
  C3_aligned = dt_alloc_align_float(npixels);
  if(!C1_aligned || !C2_aligned || !C3_aligned) goto cleanup_rawlc_l;

  gat_k16_joint_bilateral_upsample(C1_h_den, C2_h_den, C3_h_den, L_pixel,
                                    C1_aligned, C2_aligned, C3_aligned,
                                    halfwidth, halfheight, /*BW=*/1.5f);
  dt_free_align(C1_h_den); C1_h_den = NULL;
  dt_free_align(C2_h_den); C2_h_den = NULL;
  dt_free_align(C3_h_den); C3_h_den = NULL;

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_L] Phase 7 joint bilateral K16 upsample done "
                   "(BW=1.5, 3ch fused; replaces K Phase 6+8)\n");

  /* ================================================================
   * [LATEST: GALOSH_RAW_L] Phase 8+9 — fused per-pixel WHT inverse
   * + dark_ref restore + ×unified_sigma denormalize + inverse GAT (LUT).
   * Identical to K Phase 9+10.
   * ================================================================ */
  {
    static const float SIGNS[4][3] = {
      { +1.0f, +1.0f, +1.0f },  /* R  */
      { -1.0f, +1.0f, -1.0f },  /* Gb */
      { +1.0f, -1.0f, -1.0f },  /* Gr */
      { -1.0f, -1.0f, +1.0f },  /* B  */
    };

    DT_OMP_FOR()
    for(int fr = 0; fr < height; fr++)
    {
      const int r_off = (fr - co_row) & 1;
      for(int fc = 0; fc < width; fc++)
      {
        const int c_off = (fc - co_col) & 1;
        const int slot  = r_off | (c_off << 1);
        const size_t pos = (size_t)fr * width + fc;
        const float val = 0.5f * (L_pixel[pos]
                                + SIGNS[slot][0] * C1_aligned[pos]
                                + SIGNS[slot][1] * C2_aligned[pos]
                                + SIGNS[slot][2] * C3_aligned[pos])
                        + ch_dark_ref[slot];
        out[pos] = gat_inverse_exact(val * unified_sigma);
      }
    }
  }

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_L] Phase 8+9 per-pixel inverse WHT + denorm + inv-GAT done\n");

cleanup_rawlc_l:
  dt_free_align(in_gat);
  dt_free_align(L_cs);
  dt_free_align(L_cs_den);
  dt_free_align(L_h_den);
  dt_free_align(C1_h); dt_free_align(C2_h); dt_free_align(C3_h);
  dt_free_align(C1_h_den); dt_free_align(C2_h_den); dt_free_align(C3_h_den);
  dt_free_align(C1_aligned); dt_free_align(C2_aligned); dt_free_align(C3_aligned);
  dt_free_align(L_pixel);
}


/* ================================================================
 * [LATEST: GALOSH_RAW_M] gat_galosh_denoise_rawlc_m — M pipeline entry.
 *
 * M = H + hierarchical Bayesian chroma denoising in Phase 5(C).
 *
 * Returns to H's full-res cycle-spinning architecture (no I/J/K/L
 * half-res chroma path) but replaces Phase 5(C) plain LOESS with a
 * principled two-scale Bayesian estimator that allows chroma_strength
 * slider to truly act as σ_n scaling — affecting BOTH flat-region
 * noise floor AND edge behavior, with FIXED computational cost.
 *
 * Theoretical motivation (no magic numbers):
 *   Generative model:
 *     C_true ~ N(C_global, σ_local²)        (= chroma local-vs-global prior)
 *     C_obs = C_true + n,  n ~ N(0, σ_n²)    (= GAT noise model)
 *     σ_n² = 1                                (= structural property of
 *                                                 GAT normalization, NOT
 *                                                 a magic constant)
 *
 *   Local LOESS estimator: C_local_hat with std σ_n/√N_local
 *   Global LOESS estimator: C_global_hat (= prior mean)
 *
 *   MAP posterior:
 *     w_data = (N_local · σ_local²) / (N_local · σ_local² + σ_n_eff²)
 *     C_post = w_data · C_local_hat + (1 - w_data) · C_global_hat
 *
 *   User slider: σ_n_eff² = chroma_strength² · σ_n²
 *     k = 0:  w_data = 1 → C_post = C_local (= H equivalent, max detail)
 *     k = 1:  balanced (σ_local²(x)-adaptive: flat→C_global, detail→C_local)
 *     k → ∞: w_data = 0 → C_post = C_global (= max smoothing)
 *
 *   σ_local²(x) is DATA-DRIVEN: estimated from local sample variance of
 *   C_in within R_local window, bias-corrected by σ_n².  No empirical
 *   tuning constant — the variance estimator is computed inside the
 *   LOESS pass (sumC²_per_channel) at +~15% LOESS cost.
 *
 * Constants (all derived, no magic):
 *   R_local  = 7   (CFA chroma half-Nyquist period × 3-period oversampling)
 *   R_global = 15  (= 2 · R_local; scale-space doubling, Lindeberg 1994)
 *   σ_n²     = 1   (GAT normalization structural property)
 *   N_local  = (2·R_local + 1)² = 225  (window sample count)
 *
 * Pipeline (= H Phases 0..4, 6..9; M's only structural change is Phase 5(C)):
 *   Phase 0..4    identical to H Phases 0..4 (= forward stride=1 cycle-
 *                  spinning WHT + CFA demod)
 *   Phase 5(L)    = H Phase 5(L) (full-res LOSH on cycle-spun L plane)
 *   Phase 5(C) ⭐ NEW vs H ⭐ — hierarchical Bayesian:
 *     5(C-i)  Local LOESS at R=7 with σ²_local(x) emission
 *               galosh_loess_chroma_3ch_r_with_var → C_local, var_C
 *     5(C-ii) Global LOESS at R=15 (= prior mean estimator)
 *               galosh_loess_chroma_3ch_r → C_global
 *     5(C-iii) Per-pixel Bayesian inverse-variance fusion
 *               gat_m_bayesian_fusion_3ch (chroma_strength as σ_n scaling)
 *   Phase 6..9    identical to H Phases 6..9 (= remod, 4-block overlap-add
 *                  inverse, per-pixel inverse + GAT)
 *
 * Cost: M Phase 5(C) is heavier than H Phase 5(C) due to R_global=15
 * LOESS (= 4.27× window vs R=7).  Net cost vs H depends on (A) multi-
 * channel fusion savings vs R_global expansion.  Speed measurement
 * pending — if too slow, R_global can be reduced to 11 (= 1.5×) or 9
 * (= scale-doubling principle relaxed for cost).
 *
 * (日) M = H + 階層 Bayesian chroma denoise.  chroma_strength を真の
 *   σ_n scaling として動作させる (= user 期待通り「強い denoise」)。
 *   R_local=7 (CFA Nyquist), R_global=15 (scale-space ×2)、σ_local²(x)
 *   data-driven、全 constant 構造的・教科書的・データ駆動 のいずれか
 *   (= magic-free)。
 * ================================================================ */
static void galosh_raw_denoise(const float *const restrict in,
                                       float *const restrict out,
                                       const dt_iop_roi_t *const roi,
                                       const float luma_strength,
                                       const float chroma_strength,
                                       const uint32_t filters,
                                       const int cfa_row,
                                       const int cfa_col)
{
  const int width = roi->width, height = roi->height;
  const size_t npixels = (size_t)width * height;
  memcpy(out, in, sizeof(float) * npixels);

  if(luma_strength <= 0.0f) return;
  if(width < GALOSH_BLOCK_SIZE * 2 || height < GALOSH_BLOCK_SIZE * 2) return;

  const int co_row = cfa_row & 1, co_col = cfa_col & 1;
  (void)filters;

  const int halfwidth  = (width  + 1) / 2;
  const int halfheight = (height + 1) / 2;
  const size_t chsize  = (size_t)halfwidth * halfheight;

  /* Pre-declare buffers for cleanup-on-error. */
  float *in_gat = NULL;
  float *L_cs = NULL;
  float *L_cs_den = NULL;
  float *L_pixel = NULL;
  float *L_h_den = NULL;
  float *C1_h = NULL, *C2_h = NULL, *C3_h = NULL;
  /* Phase 7 chroma pyramid buffers. */
  float *L_q = NULL, *L_e = NULL;
  float *C1_q = NULL, *C2_q = NULL, *C3_q = NULL;
  float *C1_e = NULL, *C2_e = NULL, *C3_e = NULL;
  float *C1_loess_h = NULL, *C2_loess_h = NULL, *C3_loess_h = NULL;
  float *C1_loess_q = NULL, *C2_loess_q = NULL, *C3_loess_q = NULL;
  float *C1_loess_e = NULL, *C2_loess_e = NULL, *C3_loess_e = NULL;
  float *C1_q_up = NULL, *C2_q_up = NULL, *C3_q_up = NULL;
  float *C1_e_to_q = NULL, *C2_e_to_q = NULL, *C3_e_to_q = NULL;
  float *C1_e_up = NULL, *C2_e_up = NULL, *C3_e_up = NULL;
  /* Phase 8 output (= half-res blended chroma). */
  float *C1_h_den = NULL, *C2_h_den = NULL, *C3_h_den = NULL;
  /* Phase 9 output (= full-res aligned chroma). */
  float *C1_aligned = NULL, *C2_aligned = NULL, *C3_aligned = NULL;

  in_gat = dt_alloc_align_float(npixels);
  if(!in_gat) goto cleanup_rawlc_o;

  /* ============== [LATEST: GALOSH_RAW_O] Phase 0 — blind α / σ² ============== */
  const galosh_noise_params_t np = galosh_estimate_noise(in, width, height);
  gat_build_inverse_table(np.alpha, np.sigma_sq);

  /* Super-clean image-level gate.  When Phase 0 reports predicted noise std
   * below the threshold (default 0 = disabled), bypass the entire denoise
   * pipeline (Phase 1-10) and return the noisy input verbatim.  Background:
   * for super-clean images (= mid-tone pred noise std < 0.01) the per-block
   * BayesShrink + Wiener catastrophically destroys subtle texture (cliff
   * observation: ANY positive luma_strength produces -8 to -12 dB loss vs
   * noisy; only luma_strength=0 preserves).  Chroma path also contributes
   * to the destruction (= LOESS chroma denoising attenuates subtle color
   * structure).  Cleanest fix: skip the entire pipeline (= what the L=0
   * early return already does, but triggered automatically). */
  if(g_galosh_super_clean_threshold > 0.0f)
  {
    const float pred_noise_std_05 =
      sqrtf(fmaxf(np.alpha * 0.5f + np.sigma_sq, 0.0f));
    if(pred_noise_std_05 < g_galosh_super_clean_threshold)
    {
      if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] SUPER-CLEAN GATE: pred_noise_std(0.5)=%.6f "
              "< threshold=%.6f → bypass entire pipeline (out = noisy)\n",
              pred_noise_std_05, g_galosh_super_clean_threshold);
      /* out has already been initialized via memcpy(out, in) at top of
       * function.  Skip Phase 1-10 entirely. */
      return;
    }
  }

  /* ============== [LATEST: GALOSH_RAW_O] Phase 1 — GAT forward + per-CFA σ
   * + RMS unified_sigma + scalar normalize.  Identical to L Phase 1. ============== */
  DT_OMP_FOR()
  for(size_t i = 0; i < npixels; i++)
    in_gat[i] = gat_forward(in[i], np.alpha, np.sigma_sq);

  float sigma_gat_ch[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  for(int s = 0; s < 4; s++)
  {
    const int ro = ((s & 1)        + co_row) & 1;
    const int co = (((s >> 1) & 1) + co_col) & 1;
    const int hw = (width  - co + 1) / 2;
    const int hh = (height - ro + 1) / 2;
    if(hw < 4 || hh < 4) continue;

    float *tmp = dt_alloc_align_float((size_t)hw * hh);
    if(!tmp) continue;
    DT_OMP_FOR()
    for(int rr = 0; rr < hh; rr++)
      for(int cc = 0; cc < hw; cc++)
        tmp[(size_t)rr * hw + cc] = in_gat[(size_t)(ro + 2*rr) * width + (co + 2*cc)];
    sigma_gat_ch[s] = estimate_gat_sigma_halfres(tmp, hw, hh);
    dt_free_align(tmp);
  }

  const float mean_var = 0.25f * (sigma_gat_ch[0]*sigma_gat_ch[0]
                                + sigma_gat_ch[1]*sigma_gat_ch[1]
                                + sigma_gat_ch[2]*sigma_gat_ch[2]
                                + sigma_gat_ch[3]*sigma_gat_ch[3]);
  float unified_sigma = sqrtf(fmaxf(mean_var, 1e-12f));

  /* Phase 1 override (CLI --unified-sigma=X): bypass the in_gat-based
   * measurement and force unified_sigma = X.  Used when Phase 0 (α, σ²)
   * is supplied externally (= EM_iter, oracle, etc.) and the caller wants
   * to disable Phase 1's safety-net re-estimation.  X = 1.0 corresponds to
   * "trust the supplied (α, σ²) completely; no further calibration". */
  if(g_galosh_unified_sigma_override > 0.0f)
  {
    if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] unified_sigma override: measured=%.4f -> %.4f\n",
            unified_sigma, g_galosh_unified_sigma_override);
    unified_sigma = g_galosh_unified_sigma_override;
  }
  const float inv_sg = 1.0f / unified_sigma;

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] alpha=%.8f sigma_sq=%.10f | "
                  "unified_sigma=%.4f [RMS] (per-ch: %.4f %.4f %.4f %.4f) | "
                  "size=%dx%d (half=%dx%d) | sigma_L=%.3f sigma_C=%.3f\n",
                  np.alpha, np.sigma_sq, unified_sigma,
                  sigma_gat_ch[0], sigma_gat_ch[1], sigma_gat_ch[2], sigma_gat_ch[3],
                  width, height, halfwidth, halfheight, luma_strength, chroma_strength);

  DT_OMP_FOR()
  for(size_t i = 0; i < npixels; i++) in_gat[i] *= inv_sg;

  /* ============== [LATEST: GALOSH_RAW_O] Phase 2 — dark_ref IRLS + per-pixel
   * CFA-aware subtract.  Identical to L Phase 2. ============== */
  float ch_dark_ref[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  {
    const double s_init = (double)np.sigma_sq / fmax((double)np.alpha, 1e-12);
    double s_scale = s_init;
    const double s_min = 0.05 * s_init;
    const double s_max = 50.0 * s_init;
    const int n_iter = 2;

    for(int iter = 0; iter <= n_iter; iter++)
    {
      const double inv_s = 1.0 / fmax(s_scale, 1e-20);
      double sum_w = 0.0;
      double sum_w0 = 0.0, sum_w1 = 0.0, sum_w2 = 0.0, sum_w3 = 0.0;

      #pragma omp parallel for collapse(2) schedule(static) \
              reduction(+:sum_w,sum_w0,sum_w1,sum_w2,sum_w3)
      for(int br = 0; br < height - 1; br += 2)
        for(int bc = 0; bc < width - 1; bc += 2)
        {
          const float g0 = in_gat[(size_t)br     * width + bc    ];
          const float g1 = in_gat[(size_t)(br+1) * width + bc    ];
          const float g2 = in_gat[(size_t)br     * width + bc + 1];
          const float g3 = in_gat[(size_t)(br+1) * width + bc + 1];
          const float ch_max = fmaxf(fmaxf(g0, g1), fmaxf(g2, g3));
          const float ch_min = fminf(fminf(g0, g1), fminf(g2, g3));
          if(ch_max - ch_min > GALOSH_ACHROMATIC_RANGE) continue;

          const float iv0 = in[(size_t)br     * width + bc    ];
          const float iv1 = in[(size_t)(br+1) * width + bc    ];
          const float iv2 = in[(size_t)br     * width + bc + 1];
          const float iv3 = in[(size_t)(br+1) * width + bc + 1];
          const double L_raw = (iv0 + iv1 + iv2 + iv3) * 0.25;
          const double r = L_raw * inv_s;
          const double r2 = r * r;
          const double w = 1.0 / (1.0 + r2 * r2);
          sum_w  += w;
          sum_w0 += w * g0;
          sum_w1 += w * g1;
          sum_w2 += w * g2;
          sum_w3 += w * g3;
        }

      const double inv_sw = 1.0 / fmax(sum_w, 1e-20);
      ch_dark_ref[0] = (float)(sum_w0 * inv_sw);
      ch_dark_ref[1] = (float)(sum_w1 * inv_sw);
      ch_dark_ref[2] = (float)(sum_w2 * inv_sw);
      ch_dark_ref[3] = (float)(sum_w3 * inv_sw);

      if(iter == n_iter) break;

      double sum_wresid2 = 0.0;
      double sum_wW = 0.0;
      const float dr0 = ch_dark_ref[0], dr1 = ch_dark_ref[1];
      const float dr2 = ch_dark_ref[2], dr3 = ch_dark_ref[3];
      #pragma omp parallel for collapse(2) schedule(static) \
              reduction(+:sum_wresid2,sum_wW)
      for(int br = 0; br < height - 1; br += 2)
        for(int bc = 0; bc < width - 1; bc += 2)
        {
          const float g0 = in_gat[(size_t)br     * width + bc    ];
          const float g1 = in_gat[(size_t)(br+1) * width + bc    ];
          const float g2 = in_gat[(size_t)br     * width + bc + 1];
          const float g3 = in_gat[(size_t)(br+1) * width + bc + 1];
          const float ch_max = fmaxf(fmaxf(g0, g1), fmaxf(g2, g3));
          const float ch_min = fminf(fminf(g0, g1), fminf(g2, g3));
          if(ch_max - ch_min > GALOSH_ACHROMATIC_RANGE) continue;

          const float iv0 = in[(size_t)br     * width + bc    ];
          const float iv1 = in[(size_t)(br+1) * width + bc    ];
          const float iv2 = in[(size_t)br     * width + bc + 1];
          const float iv3 = in[(size_t)(br+1) * width + bc + 1];
          const double L_raw = (iv0 + iv1 + iv2 + iv3) * 0.25;
          const double r = L_raw * inv_s;
          const double r2 = r * r;
          const double w = 1.0 / (1.0 + r2 * r2);
          const double d0 = (double)g0 - dr0;
          const double d1 = (double)g1 - dr1;
          const double d2 = (double)g2 - dr2;
          const double d3 = (double)g3 - dr3;
          const double resid2 = d0*d0 + d1*d1 + d2*d2 + d3*d3;
          sum_wW      += w;
          sum_wresid2 += w * resid2 * 0.25;
        }
      const double inv_sw2 = 1.0 / fmax(sum_wW, 1e-20);
      const double measured_std = sqrt(fmax(sum_wresid2 * inv_sw2, 1e-20));
      const double ratio = 1.0 / measured_std;
      s_scale *= sqrt(ratio);
      if(s_scale < s_min) s_scale = s_min;
      if(s_scale > s_max) s_scale = s_max;
    }

    if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] dark anchor: s_init=%.6e s_final=%.6e | "
                    "ch_dark_ref: [0]=%.4f [1]=%.4f [2]=%.4f [3]=%.4f\n",
            s_init, s_scale,
            ch_dark_ref[0], ch_dark_ref[1], ch_dark_ref[2], ch_dark_ref[3]);

    DT_OMP_FOR()
    for(int r = 0; r < height; r++)
    {
      const int r_off = (r - co_row) & 1;
      for(int c = 0; c < width; c++)
      {
        const int c_off = (c - co_col) & 1;
        const int slot  = r_off | (c_off << 1);
        in_gat[(size_t)r * width + c] -= ch_dark_ref[slot];
      }
    }
  }

  /* Verification harness: dump intermediates if GALOSH_DUMP_DIR set. */
#define O32_CPU_DUMP(name, ptr, n_floats) do { \
  const char *_dd = getenv("GALOSH_DUMP_DIR"); \
  if(_dd) { \
    char _path[1024]; \
    snprintf(_path, sizeof(_path), "%s/%s.bin", _dd, name); \
    FILE *_df = fopen(_path, "wb"); \
    if(_df) { fwrite((ptr), sizeof(float), (n_floats), _df); fclose(_df); } \
  } \
} while(0)
  O32_CPU_DUMP("p2_in_gat", in_gat, npixels);

  /* ============== [LATEST: GALOSH_RAW_O] Phase 3 — stride=1 forward 2x2 WHT
   * (L-only).  Identical to L Phase 3. ============== */
  L_cs = dt_alloc_align_float(npixels);
  if(!L_cs) goto cleanup_rawlc_o;
  gat_h_forward_l_only_stride1(in_gat, L_cs, width, height);
  O32_CPU_DUMP("p3_L_cs", L_cs, npixels);

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Phase 3 forward WHT (stride=1, L-only) done\n");

  /* ============== [LATEST: GALOSH_RAW_O] Phase 4 — half-res chroma extract.
   * Identical to L Phase 4. ============== */
  C1_h = dt_alloc_align_float(chsize);
  C2_h = dt_alloc_align_float(chsize);
  C3_h = dt_alloc_align_float(chsize);
  if(!C1_h || !C2_h || !C3_h) goto cleanup_rawlc_o;
  gat_j_forward_c_halfres(in_gat, C1_h, C2_h, C3_h,
                           width, height, halfwidth, halfheight);
  O32_CPU_DUMP("p4_C1_h", C1_h, chsize);
  O32_CPU_DUMP("p4_C2_h", C2_h, chsize);
  O32_CPU_DUMP("p4_C3_h", C3_h, chsize);
  dt_free_align(in_gat); in_gat = NULL;

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Phase 4 half-res chroma extract done\n");

  /* ============== [LATEST: GALOSH_RAW_O] Phase 5 — single-scale WHT-LOSH on
   * L_cs.  Identical to L Phase 5(L).  N's multi-scale luma was diagnosed
   * to regress -0.6 dB (= cycle-spinning correlation breaks σ scaling),
   * reverted. ============== */
  L_cs_den = dt_alloc_align_float(npixels);
  if(!L_cs_den) goto cleanup_rawlc_o;
  /* Phase 5 expanded inline (= equivalent to galosh_pass12_multiorient_blocked
   * with n_orient=1) so pilot is exposed for cross-CPU/GPU verification
   * dumping.  Bit-identical to the wrapper for n_orient=1 path. */
  {
    float *L_cs_pilot = dt_alloc_align_float(npixels);
    if(!L_cs_pilot) goto cleanup_rawlc_o;
    /* [V2.0 A2] block size routed through --wht (default 8 = canonical O;
     * 4 = fast mode).  wiener_floor stays 1/B by construction. */
    GALOSH_F16_STAGE(L_cs, npixels);          /* [A4] P5 input storage */
    galosh_pass1_blocked(L_cs, L_cs_pilot, width, height,
                          luma_strength, /*block=*/g_galosh_wht_block,
                          /*stride=*/2, /*use_robust_shrink=*/1);
    GALOSH_F16_STAGE(L_cs_pilot, npixels);    /* [A4] pass1->pass2 buffer */
    O32_CPU_DUMP("p5_pilot", L_cs_pilot, npixels);
    galosh_pass2_blocked(L_cs, L_cs_pilot, L_cs_den, width, height,
                          luma_strength,
                          /*wiener_floor=*/(1.0f / (float)g_galosh_wht_block),
                          /*block=*/g_galosh_wht_block, /*stride=*/2);
    GALOSH_F16_STAGE(L_cs_den, npixels);      /* [A4] P5 output storage */
    dt_free_align(L_cs_pilot);
  }
  O32_CPU_DUMP("p5_L_cs_den", L_cs_den, npixels);
  dt_free_align(L_cs); L_cs = NULL;
  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Phase 5 single-scale L WHT-LOSH done\n");

  /* ============== [LATEST: GALOSH_RAW_O] Phase 6 — L_pixel = 2x2 overlap-avg
   * of L_cs_den (= full-res chroma guide for Phase 9);
   * L_h_den = subsample of L_cs_den at every-other position (= half-res
   * chroma guide for Phase 7).  Identical to L Phase 6. ============== */
  L_pixel = dt_alloc_align_float(npixels);
  L_h_den = dt_alloc_align_float(chsize);
  if(!L_pixel || !L_h_den) goto cleanup_rawlc_o;

  gat_i_lpixel_overlap_avg(L_cs_den, L_pixel, width, height);

  DT_OMP_FOR()
  for(int hr = 0; hr < halfheight; hr++)
  {
    const int fr = 2 * hr;
    if(fr >= height) continue;
    for(int hc = 0; hc < halfwidth; hc++)
    {
      const int fc = 2 * hc;
      if(fc >= width) continue;
      L_h_den[(size_t)hr * halfwidth + hc] = L_cs_den[(size_t)fr * width + fc];
    }
  }
  GALOSH_F16_STAGE(L_pixel, npixels);         /* [A4] P6 full-res guide */
  GALOSH_F16_STAGE(L_h_den, chsize);          /* [A4] P6 half-res guide */
  O32_CPU_DUMP("p6_L_pixel", L_pixel, npixels);
  O32_CPU_DUMP("p6_L_h_den", L_h_den, chsize);
  dt_free_align(L_cs_den); L_cs_den = NULL;

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Phase 6 L_pixel + L_h_den done\n");

  /* ============== [LATEST: GALOSH_RAW_O] Phase 7 ⭐ NEW vs L ⭐ — Multi-scale
   * LOESS chroma pyramid + L-guided K16 upsample at each scale transition.
   *
   * Constructs 3 LOESS-denoised chroma estimates at progressively wider
   * effective receptive fields:
   *   Lv0 (half-res,    ~30 raw px) = LOESS(C_h,             L_h_den, R=7)
   *   Lv1 (quarter-res, ~60 raw px) = LOESS(box_down(C_h),   L_q,     R=7)
   *   Lv2 (eighth-res, ~120 raw px) = LOESS(box_down²(C_h),  L_e,     R=7)
   * Each LOESS is the same fused 3ch Y-bilateral local linear regression
   * used in L pipeline Phase 5(C) — only the input scale changes.
   *
   * Coarse scales are upsampled to half-res with the existing
   * gat_k16_joint_bilateral_upsample (= K16 EWA-JL3 jinc + L bilateral
   * weight) using L at the destination scale as guide:
   *   C_q_up   = K16(C_loess_q,   L_h_den)         [quarter → half]
   *   C_e_to_q = K16(C_loess_e,   L_q)             [eighth  → quarter]
   *   C_e_up   = K16(C_e_to_q,    L_h_den)         [quarter → half]
   * The K16 bilateral guide ensures L coupling is preserved at every
   * scale transition (= no plain unguided upsample anywhere in the
   * pyramid). ============== */
  const int cq_w = halfwidth / 2;
  const int cq_h = halfheight / 2;
  const int ce_w = cq_w / 2;
  const int ce_h = cq_h / 2;
  const size_t cqsize = (size_t)cq_w * cq_h;
  const size_t cesize = (size_t)ce_w * ce_h;

  if(cq_w < 4 || cq_h < 4 || ce_w < 4 || ce_h < 4)
  {
    /* Image too small for 3-level pyramid — fall back to L pipeline. */
    if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] image too small for 3-level pyramid "
                    "(cq=%dx%d, ce=%dx%d); falling back to L pipeline\n",
            cq_w, cq_h, ce_w, ce_h);
    goto cleanup_rawlc_o;  /* TODO: graceful fallback to L variant */
  }

  /* ─── Build L pyramid (= box_down twice). ─── */
  L_q = dt_alloc_align_float(cqsize);
  L_e = dt_alloc_align_float(cesize);
  if(!L_q || !L_e) goto cleanup_rawlc_o;
  gat_box_downsample_2x(L_h_den, L_q, halfwidth, halfheight);
  gat_box_downsample_2x(L_q,     L_e, cq_w,      cq_h);

  /* ─── Build chroma pyramid (= box_down twice, per channel). ─── */
  C1_q = dt_alloc_align_float(cqsize);
  C2_q = dt_alloc_align_float(cqsize);
  C3_q = dt_alloc_align_float(cqsize);
  C1_e = dt_alloc_align_float(cesize);
  C2_e = dt_alloc_align_float(cesize);
  C3_e = dt_alloc_align_float(cesize);
  if(!C1_q || !C2_q || !C3_q || !C1_e || !C2_e || !C3_e) goto cleanup_rawlc_o;
  gat_box_downsample_2x(C1_h, C1_q, halfwidth, halfheight);
  gat_box_downsample_2x(C2_h, C2_q, halfwidth, halfheight);
  gat_box_downsample_2x(C3_h, C3_q, halfwidth, halfheight);
  gat_box_downsample_2x(C1_q, C1_e, cq_w, cq_h);
  gat_box_downsample_2x(C2_q, C2_e, cq_w, cq_h);
  gat_box_downsample_2x(C3_q, C3_e, cq_w, cq_h);

  /* ─── LOESS at each scale (3ch fused per call). ─── */
  C1_loess_h = dt_alloc_align_float(chsize);
  C2_loess_h = dt_alloc_align_float(chsize);
  C3_loess_h = dt_alloc_align_float(chsize);
  C1_loess_q = dt_alloc_align_float(cqsize);
  C2_loess_q = dt_alloc_align_float(cqsize);
  C3_loess_q = dt_alloc_align_float(cqsize);
  C1_loess_e = dt_alloc_align_float(cesize);
  C2_loess_e = dt_alloc_align_float(cesize);
  C3_loess_e = dt_alloc_align_float(cesize);
  if(!C1_loess_h || !C2_loess_h || !C3_loess_h ||
     !C1_loess_q || !C2_loess_q || !C3_loess_q ||
     !C1_loess_e || !C2_loess_e || !C3_loess_e)
    goto cleanup_rawlc_o;

  /* LOESS strength is fixed at the L-baseline tuning (= chroma_strength
   * controls the slider WALK across pyramid scales, not the LOESS ε).
   * Each LOESS invocation runs at "noise-matched" ε via strength_c=1.0. */
  const float loess_strength = 1.0f;
  galosh_loess_chroma_3ch_r(L_h_den, C1_h, C2_h, C3_h,
                             C1_loess_h, C2_loess_h, C3_loess_h,
                             halfwidth, halfheight, loess_strength,
                             GALOSH_LOESS_RADIUS, GALOSH_LOESS_BW);
  O32_CPU_DUMP("p7_C1_loess_h", C1_loess_h, chsize);
  galosh_loess_chroma_3ch_r(L_q, C1_q, C2_q, C3_q,
                             C1_loess_q, C2_loess_q, C3_loess_q,
                             cq_w, cq_h, loess_strength,
                             GALOSH_LOESS_RADIUS, GALOSH_LOESS_BW);
  galosh_loess_chroma_3ch_r(L_e, C1_e, C2_e, C3_e,
                             C1_loess_e, C2_loess_e, C3_loess_e,
                             ce_w, ce_h, loess_strength,
                             GALOSH_LOESS_RADIUS, GALOSH_LOESS_BW);

  /* C{1,2,3}_q and C{1,2,3}_e are pyramid INPUTS; LOESS outputs replace
   * them.  Free input pyramid buffers (= no longer needed). */
  dt_free_align(C1_q); dt_free_align(C2_q); dt_free_align(C3_q);
  C1_q = C2_q = C3_q = NULL;
  dt_free_align(C1_e); dt_free_align(C2_e); dt_free_align(C3_e);
  C1_e = C2_e = C3_e = NULL;

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Phase 7 LOESS at 3 scales done "
                  "(half: %dx%d, quarter: %dx%d, eighth: %dx%d)\n",
          halfwidth, halfheight, cq_w, cq_h, ce_w, ce_h);

  /* ─── Upsample coarse levels to half-res via K16 joint bilateral. ───
   * Using L at the destination scale as bilateral guide preserves L
   * coupling at every scale transition (= no plain unguided upsample).
   *
   * STRIDE FIX: K16 hardcodes fw = 2*halfwidth_param, fh = 2*halfheight
   * for the destination buffer's stride.  When pyramid downsampling
   * gives odd dims (= e.g. SIDD halfwidth=2663 → cq_w=1331 → 2*cq_w=
   * 2662 ≠ halfwidth), the K16-internal stride does NOT match
   * chsize-allocated buffers' stride, producing garbage output.
   * Workaround: allocate K16 input/output buffers at exact (2*input_w
   * × 2*input_h) dim, then edge-replicate-pad to chsize for smoothstep
   * blend compatibility.
   */
  C1_q_up = dt_alloc_align_float(chsize);
  C2_q_up = dt_alloc_align_float(chsize);
  C3_q_up = dt_alloc_align_float(chsize);
  C1_e_up = dt_alloc_align_float(chsize);
  C2_e_up = dt_alloc_align_float(chsize);
  C3_e_up = dt_alloc_align_float(chsize);
  if(!C1_q_up || !C2_q_up || !C3_q_up ||
     !C1_e_up || !C2_e_up || !C3_e_up)
    goto cleanup_rawlc_o;

  /* quarter → half (L_h_den guide) — stride-corrected */
  {
    const int fw = 2 * cq_w;          /* output width K16 uses (= 2*input_w) */
    const int fh = 2 * cq_h;          /* output height K16 uses */
    const size_t fsize = (size_t)fw * fh;

    float *L_for_q = dt_alloc_align_float(fsize);
    float *C1_q_up_raw = dt_alloc_align_float(fsize);
    float *C2_q_up_raw = dt_alloc_align_float(fsize);
    float *C3_q_up_raw = dt_alloc_align_float(fsize);
    if(!L_for_q || !C1_q_up_raw || !C2_q_up_raw || !C3_q_up_raw)
    {
      dt_free_align(L_for_q);
      dt_free_align(C1_q_up_raw); dt_free_align(C2_q_up_raw); dt_free_align(C3_q_up_raw);
      goto cleanup_rawlc_o;
    }

    /* Crop L_h_den (chsize stride) to (fw × fh) for stride-matching K16. */
    gat_crop_2d_topleft(L_h_den, halfwidth, halfheight, L_for_q, fw, fh);

    gat_chroma_upsample(C1_loess_q, C2_loess_q, C3_loess_q, L_for_q,
                        C1_q_up_raw, C2_q_up_raw, C3_q_up_raw,
                        cq_w, cq_h, /*BW=*/1.5f);

    /* Pad-to-chsize via edge replication for the smoothstep blend. */
    gat_pad_2d_edge(C1_q_up_raw, fw, fh, C1_q_up, halfwidth, halfheight);
    gat_pad_2d_edge(C2_q_up_raw, fw, fh, C2_q_up, halfwidth, halfheight);
    gat_pad_2d_edge(C3_q_up_raw, fw, fh, C3_q_up, halfwidth, halfheight);
    O32_CPU_DUMP("p7_C1_q_up", C1_q_up, chsize);

    dt_free_align(L_for_q);
    dt_free_align(C1_q_up_raw);
    dt_free_align(C2_q_up_raw);
    dt_free_align(C3_q_up_raw);
  }

  /* eighth → quarter → half (chained, with stride-correction at each step) */
  {
    /* Step 1: eighth → quarter.  K16 expects fw = 2*ce_w, fh = 2*ce_h. */
    const int fw_eq = 2 * ce_w;       /* K16 output width at quarter scale */
    const int fh_eq = 2 * ce_h;
    const size_t fsize_eq = (size_t)fw_eq * fh_eq;

    float *L_for_e = dt_alloc_align_float(fsize_eq);
    float *C1_e_to_q_raw = dt_alloc_align_float(fsize_eq);
    float *C2_e_to_q_raw = dt_alloc_align_float(fsize_eq);
    float *C3_e_to_q_raw = dt_alloc_align_float(fsize_eq);
    if(!L_for_e || !C1_e_to_q_raw || !C2_e_to_q_raw || !C3_e_to_q_raw)
    {
      dt_free_align(L_for_e);
      dt_free_align(C1_e_to_q_raw); dt_free_align(C2_e_to_q_raw); dt_free_align(C3_e_to_q_raw);
      goto cleanup_rawlc_o;
    }

    /* L_q is at (cq_w × cq_h); crop to (fw_eq × fh_eq) for K16 stride match. */
    gat_crop_2d_topleft(L_q, cq_w, cq_h, L_for_e, fw_eq, fh_eq);

    gat_chroma_upsample(C1_loess_e, C2_loess_e, C3_loess_e, L_for_e,
                        C1_e_to_q_raw, C2_e_to_q_raw, C3_e_to_q_raw,
                        ce_w, ce_h, /*BW=*/1.5f);

    /* Pad raw output (fw_eq × fh_eq) up to (cq_w × cq_h) for next K16 step. */
    C1_e_to_q = dt_alloc_align_float(cqsize);
    C2_e_to_q = dt_alloc_align_float(cqsize);
    C3_e_to_q = dt_alloc_align_float(cqsize);
    if(!C1_e_to_q || !C2_e_to_q || !C3_e_to_q)
    {
      dt_free_align(L_for_e);
      dt_free_align(C1_e_to_q_raw); dt_free_align(C2_e_to_q_raw); dt_free_align(C3_e_to_q_raw);
      goto cleanup_rawlc_o;
    }
    gat_pad_2d_edge(C1_e_to_q_raw, fw_eq, fh_eq, C1_e_to_q, cq_w, cq_h);
    gat_pad_2d_edge(C2_e_to_q_raw, fw_eq, fh_eq, C2_e_to_q, cq_w, cq_h);
    gat_pad_2d_edge(C3_e_to_q_raw, fw_eq, fh_eq, C3_e_to_q, cq_w, cq_h);
    dt_free_align(L_for_e);
    dt_free_align(C1_e_to_q_raw);
    dt_free_align(C2_e_to_q_raw);
    dt_free_align(C3_e_to_q_raw);

    /* Step 2: quarter → half (= same as q→h above, but input is C_e_to_q). */
    const int fw_qh = 2 * cq_w;
    const int fh_qh = 2 * cq_h;
    const size_t fsize_qh = (size_t)fw_qh * fh_qh;

    float *L_for_q2 = dt_alloc_align_float(fsize_qh);
    float *C1_e_up_raw = dt_alloc_align_float(fsize_qh);
    float *C2_e_up_raw = dt_alloc_align_float(fsize_qh);
    float *C3_e_up_raw = dt_alloc_align_float(fsize_qh);
    if(!L_for_q2 || !C1_e_up_raw || !C2_e_up_raw || !C3_e_up_raw)
    {
      dt_free_align(L_for_q2);
      dt_free_align(C1_e_up_raw); dt_free_align(C2_e_up_raw); dt_free_align(C3_e_up_raw);
      goto cleanup_rawlc_o;
    }
    gat_crop_2d_topleft(L_h_den, halfwidth, halfheight, L_for_q2, fw_qh, fh_qh);

    gat_chroma_upsample(C1_e_to_q, C2_e_to_q, C3_e_to_q, L_for_q2,
                        C1_e_up_raw, C2_e_up_raw, C3_e_up_raw,
                        cq_w, cq_h, /*BW=*/1.5f);

    gat_pad_2d_edge(C1_e_up_raw, fw_qh, fh_qh, C1_e_up, halfwidth, halfheight);
    gat_pad_2d_edge(C2_e_up_raw, fw_qh, fh_qh, C2_e_up, halfwidth, halfheight);
    gat_pad_2d_edge(C3_e_up_raw, fw_qh, fh_qh, C3_e_up, halfwidth, halfheight);
    O32_CPU_DUMP("p7_C1_e_up", C1_e_up, chsize);

    dt_free_align(L_for_q2);
    dt_free_align(C1_e_up_raw);
    dt_free_align(C2_e_up_raw);
    dt_free_align(C3_e_up_raw);
  }

  /* Free intermediates. */
  dt_free_align(C1_loess_q); dt_free_align(C2_loess_q); dt_free_align(C3_loess_q);
  C1_loess_q = C2_loess_q = C3_loess_q = NULL;
  dt_free_align(C1_loess_e); dt_free_align(C2_loess_e); dt_free_align(C3_loess_e);
  C1_loess_e = C2_loess_e = C3_loess_e = NULL;
  dt_free_align(C1_e_to_q); dt_free_align(C2_e_to_q); dt_free_align(C3_e_to_q);
  C1_e_to_q = C2_e_to_q = C3_e_to_q = NULL;
  dt_free_align(L_q); dt_free_align(L_e);
  L_q = L_e = NULL;

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Phase 7 K16 joint-bilateral upsample × 3 done "
                  "(L coupling preserved at every scale transition)\n");

  /* ============== [LATEST: GALOSH_RAW_O] Phase 8 ⭐ NEW vs L ⭐ — smoothstep
   * slider walk over 4 anchors {C_h, C_loess_h, C_q_up, C_e_up}.
   *
   *   slider ∈ [0, 1]: t = smoothstep(s    );  C = (1-t)*C_h        + t*C_loess_h
   *   slider ∈ [1, 2]: t = smoothstep(s - 1); C = (1-t)*C_loess_h + t*C_q_up
   *   slider ∈ [2, 3]: t = smoothstep(s - 2); C = (1-t)*C_q_up    + t*C_e_up
   *   slider ≥ 3:                              C = C_e_up   (saturate)
   *
   * Cubic smoothstep (= 3t² - 2t³, derivative 0 at t=0,1) gives C¹
   * continuity at integer slider values — no "click" feel at scale
   * boundaries. ============== */
  C1_h_den = dt_alloc_align_float(chsize);
  C2_h_den = dt_alloc_align_float(chsize);
  C3_h_den = dt_alloc_align_float(chsize);
  if(!C1_h_den || !C2_h_den || !C3_h_den) goto cleanup_rawlc_o;

  {
    /* Determine segment + smoothstep parameter once (= same for all 3 ch). */
    const float s = chroma_strength;
    int segment;
    float t_raw;
    const float *A1, *A2, *A3, *B1, *B2, *B3;
    if(s <= 0.0f)
    {
      segment = -1;
      t_raw = 0.0f;
      A1 = A2 = A3 = B1 = B2 = B3 = NULL;
    }
    else if(s <= 1.0f)
    {
      segment = 0;
      t_raw = s;
      A1 = C1_h;       A2 = C2_h;       A3 = C3_h;
      B1 = C1_loess_h; B2 = C2_loess_h; B3 = C3_loess_h;
    }
    else if(s <= 2.0f)
    {
      segment = 1;
      t_raw = s - 1.0f;
      A1 = C1_loess_h; A2 = C2_loess_h; A3 = C3_loess_h;
      B1 = C1_q_up;    B2 = C2_q_up;    B3 = C3_q_up;
    }
    else if(s <= 3.0f)
    {
      segment = 2;
      t_raw = s - 2.0f;
      A1 = C1_q_up;    A2 = C2_q_up;    A3 = C3_q_up;
      B1 = C1_e_up;    B2 = C2_e_up;    B3 = C3_e_up;
    }
    else
    {
      segment = 3;
      t_raw = 1.0f;
      A1 = C1_e_up;    A2 = C2_e_up;    A3 = C3_e_up;
      B1 = NULL;       B2 = NULL;       B3 = NULL;
    }

    if(segment < 0)
    {
      /* slider ≤ 0: pure noisy, no denoise. */
      memcpy(C1_h_den, C1_h, sizeof(float) * chsize);
      memcpy(C2_h_den, C2_h, sizeof(float) * chsize);
      memcpy(C3_h_den, C3_h, sizeof(float) * chsize);
    }
    else if(segment >= 3 || B1 == NULL)
    {
      /* slider ≥ 3: saturate at C_e_up. */
      memcpy(C1_h_den, A1, sizeof(float) * chsize);
      memcpy(C2_h_den, A2, sizeof(float) * chsize);
      memcpy(C3_h_den, A3, sizeof(float) * chsize);
    }
    else
    {
      const float t = t_raw * t_raw * (3.0f - 2.0f * t_raw);   /* smoothstep */
      const float oneMt = 1.0f - t;
      DT_OMP_FOR()
      for(size_t i = 0; i < chsize; i++)
      {
        C1_h_den[i] = oneMt * A1[i] + t * B1[i];
        C2_h_den[i] = oneMt * A2[i] + t * B2[i];
        C3_h_den[i] = oneMt * A3[i] + t * B3[i];
      }
    }

    if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Phase 8 smoothstep slider walk done "
                    "(slider=%.3f, segment=%d, t_raw=%.3f)\n",
            chroma_strength, segment, t_raw);
    O32_CPU_DUMP("p8_C1_h_den", C1_h_den, chsize);
  }

  /* Free anchor buffers (= already consumed). */
  dt_free_align(C1_h); dt_free_align(C2_h); dt_free_align(C3_h);
  C1_h = C2_h = C3_h = NULL;
  dt_free_align(C1_loess_h); dt_free_align(C2_loess_h); dt_free_align(C3_loess_h);
  C1_loess_h = C2_loess_h = C3_loess_h = NULL;
  dt_free_align(C1_q_up); dt_free_align(C2_q_up); dt_free_align(C3_q_up);
  C1_q_up = C2_q_up = C3_q_up = NULL;
  dt_free_align(C1_e_up); dt_free_align(C2_e_up); dt_free_align(C3_e_up);
  C1_e_up = C2_e_up = C3_e_up = NULL;
  dt_free_align(L_h_den); L_h_den = NULL;

  GALOSH_F16_STAGE(C1_h_den, chsize);         /* [A4] P8 blended chroma */
  GALOSH_F16_STAGE(C2_h_den, chsize);
  GALOSH_F16_STAGE(C3_h_den, chsize);

  /* ============== [LATEST: GALOSH_RAW_O] Phase 9 — Joint bilateral K16
   * EWA-JL3 upsample to full-res with L_pixel guide.  Identical to L
   * pipeline. ============== */
  C1_aligned = dt_alloc_align_float(npixels);
  C2_aligned = dt_alloc_align_float(npixels);
  C3_aligned = dt_alloc_align_float(npixels);
  if(!C1_aligned || !C2_aligned || !C3_aligned) goto cleanup_rawlc_o;

  gat_chroma_upsample(C1_h_den, C2_h_den, C3_h_den, L_pixel,
                      C1_aligned, C2_aligned, C3_aligned,
                      halfwidth, halfheight, /*BW=*/1.5f);
  GALOSH_F16_STAGE(C1_aligned, npixels);      /* [A4] P9 full-res chroma */
  GALOSH_F16_STAGE(C2_aligned, npixels);
  GALOSH_F16_STAGE(C3_aligned, npixels);
  O32_CPU_DUMP("p9_C1_aligned", C1_aligned, npixels);
  dt_free_align(C1_h_den); dt_free_align(C2_h_den); dt_free_align(C3_h_den);
  C1_h_den = C2_h_den = C3_h_den = NULL;

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Phase 9 final K16 joint-bilateral upsample "
                  "(L_pixel guide, full-res reconstruction) done\n");

  /* ============== [LATEST: GALOSH_RAW_O] Phase 10 — fused per-pixel inverse
   * 2x2 WHT + dark_ref restore + ×unified_sigma + inverse GAT (LUT).
   * Identical to L Phase 8+9. ============== */
  {
    static const float SIGNS[4][3] = {
      { +1.0f, +1.0f, +1.0f },  /* R  */
      { -1.0f, +1.0f, -1.0f },  /* Gb */
      { +1.0f, -1.0f, -1.0f },  /* Gr */
      { -1.0f, -1.0f, +1.0f },  /* B  */
    };

    DT_OMP_FOR()
    for(int fr = 0; fr < height; fr++)
    {
      const int r_off = (fr - co_row) & 1;
      for(int fc = 0; fc < width; fc++)
      {
        const int c_off = (fc - co_col) & 1;
        const int slot  = r_off | (c_off << 1);
        const size_t pos = (size_t)fr * width + fc;
        const float val = 0.5f * (L_pixel[pos]
                                + SIGNS[slot][0] * C1_aligned[pos]
                                + SIGNS[slot][1] * C2_aligned[pos]
                                + SIGNS[slot][2] * C3_aligned[pos])
                        + ch_dark_ref[slot];
        out[pos] = gat_inverse_exact(val * unified_sigma);
      }
    }
  }

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Phase 10 per-pixel inverse WHT + denorm + inv-GAT done\n");
  O32_CPU_DUMP("p10_output", out, npixels);
#undef O32_CPU_DUMP

cleanup_rawlc_o:
  dt_free_align(in_gat);
  dt_free_align(L_cs);
  dt_free_align(L_cs_den);
  dt_free_align(L_pixel);
  dt_free_align(L_h_den);
  dt_free_align(L_q); dt_free_align(L_e);
  dt_free_align(C1_h); dt_free_align(C2_h); dt_free_align(C3_h);
  dt_free_align(C1_q); dt_free_align(C2_q); dt_free_align(C3_q);
  dt_free_align(C1_e); dt_free_align(C2_e); dt_free_align(C3_e);
  dt_free_align(C1_loess_h); dt_free_align(C2_loess_h); dt_free_align(C3_loess_h);
  dt_free_align(C1_loess_q); dt_free_align(C2_loess_q); dt_free_align(C3_loess_q);
  dt_free_align(C1_loess_e); dt_free_align(C2_loess_e); dt_free_align(C3_loess_e);
  dt_free_align(C1_q_up); dt_free_align(C2_q_up); dt_free_align(C3_q_up);
  dt_free_align(C1_e_to_q); dt_free_align(C2_e_to_q); dt_free_align(C3_e_to_q);
  dt_free_align(C1_e_up); dt_free_align(C2_e_up); dt_free_align(C3_e_up);
  dt_free_align(C1_h_den); dt_free_align(C2_h_den); dt_free_align(C3_h_den);
  dt_free_align(C1_aligned); dt_free_align(C2_aligned); dt_free_align(C3_aligned);
}


/* ================================================================
 * [DEPRECATED: GALOSH_RAW_N] galosh_n_chroma_pyramid_lcoupled — 2-level
 * Laplacian pyramid + L-coupled WHT-LOSH on a single half-res chroma
 * plane.  Used by Phase 8 of the (deprecated) N pipeline.
 *
 * WHY DEPRECATED: replacing LOESS with L-coupled WHT-LOSH on chroma
 *   regressed sRGB PSNR by −3.4 dB on SIDD `0001_S6_GRBG_010` (= 38.77
 *   vs L's 42.14).  Diagnostic isolated the regression to the chroma
 *   side: LOESS's per-pixel Y-bilateral weighted local linear regression
 *   is structurally better-suited for half-Nyquist chroma than 8x8 WHT
 *   block transform, regardless of multi-scale or L-coupling.  Block-
 *   transform per-coefficient BayesShrink loses the cross-channel
 *   coherence that LOESS captures pixel-by-pixel.
 *
 * REPLACEMENT: GALOSH_RAW_O uses multi-scale LOESS pyramid (3 levels:
 *   half / quarter / eighth) + smoothstep slider walk.  Slider semantics
 *   match user spec (= 0 noisy, 1 standard, ≥1 stronger toward 120 raw
 *   px receptive field) without the WHT block-transform regression.
 *
 * Kept in source as reference for the negative result.
 *
 * Decompose:
 *   C_quarter = box_down(C_h)                 (quarter-res low-pass)
 *   C_detail  = C_h - box_up(C_quarter)       (half-res high-freq detail)
 *
 * Per-level denoise (= cross-channel BayesShrink threshold derived from
 * pooled C+L AC variance; see galosh_pass12_lcoupled_multiorient_blocked):
 *   C_quarter_den = lcoupled_WHT-LOSH(C_quarter, L_guide_quarter,
 *                                     σ = chroma_strength × 0.5)
 *   C_detail_den  = lcoupled_WHT-LOSH(C_detail,  L_guide_detail,
 *                                     σ = chroma_strength × 1.0)
 * σ scales 0.5× per coarser level (= 2x2 box-avg variance reduction
 * sqrt(1/4)).  Threshold uses pooled (C+L) AC sample variance so that
 * blocks with strong L structure get relaxed C threshold (= preserve
 * cross-channel-aligned chroma edges) while flat-L blocks get aggressive
 * threshold (= strong chroma smoothing where signal is weak).
 *
 * Reconstruct (= exact inverse of decompose if no denoising applied):
 *   C_h_den = box_up(C_quarter_den) + C_detail_den
 *
 * (日) Chroma 1ch を 2-level Laplacian pyramid に分解し、各 level で
 *   L-coupled WHT-LOSH (= 各 block で C+L AC pooled BayesShrink)。
 *   Coarse level の σ は 0.5× にスケール (= 2x2 box-down で var/4)。
 *   L 構造あり block では C threshold 緩和 (= cross-channel edge 保持)、
 *   L 平坦 block では aggressive threshold (= 弱信号で強 smoothing)。
 * ================================================================ */
static void galosh_n_chroma_pyramid_lcoupled(
    const float *restrict C_h,
    const float *restrict L_guide_quarter,
    const float *restrict L_guide_detail,
    float *restrict C_h_den,
    const int halfwidth, const int halfheight,
    const float chroma_strength)
{
  const int cq_w = halfwidth / 2;
  const int cq_h = halfheight / 2;
  const size_t chsize = (size_t)halfwidth * halfheight;
  const size_t cqsize = (size_t)cq_w * cq_h;

  float *C_quarter     = dt_alloc_align_float(cqsize);
  float *C_quarter_up  = dt_alloc_align_float(chsize);
  float *C_detail      = dt_alloc_align_float(chsize);
  float *C_quarter_den = dt_alloc_align_float(cqsize);
  float *C_detail_den  = dt_alloc_align_float(chsize);

  if(!C_quarter || !C_quarter_up || !C_detail || !C_quarter_den || !C_detail_den)
  {
    memcpy(C_h_den, C_h, sizeof(float) * chsize);
    goto cleanup_n_chroma;
  }

  /* Decompose */
  gat_box_downsample_2x(C_h, C_quarter, halfwidth, halfheight);
  gat_box_replicate_upsample_2x(C_quarter, C_quarter_up,
                                 cq_w, cq_h, halfwidth, halfheight);
  DT_OMP_FOR()
  for(size_t i = 0; i < chsize; i++)
    C_detail[i] = C_h[i] - C_quarter_up[i];

  /* Per-level σ measured via MAD-of-Laplacian (= sqrt(0.75)/0.5 textbook
   * for uncorrelated half-res C input + 2x2 box-avg, but measured values
   * track the actual marginal noise level robustly under signal). */
  const float sigma_C_q_meas = estimate_gat_sigma_halfres(C_quarter, cq_w, cq_h);
  const float sigma_C_d_meas = estimate_gat_sigma_halfres(C_detail, halfwidth, halfheight);

  /* Per-level L-coupled WHT-LOSH */
  galosh_pass12_lcoupled_multiorient_blocked(
      C_quarter, L_guide_quarter, C_quarter_den,
      cq_w, cq_h, chroma_strength * sigma_C_q_meas,
      /*block=*/8, /*stride=*/2, /*n_orient=*/1, /*use_robust_shrink=*/1);
  galosh_pass12_lcoupled_multiorient_blocked(
      C_detail, L_guide_detail, C_detail_den,
      halfwidth, halfheight, chroma_strength * sigma_C_d_meas,
      /*block=*/8, /*stride=*/2, /*n_orient=*/1, /*use_robust_shrink=*/1);

  /* Reconstruct */
  gat_box_replicate_upsample_2x(C_quarter_den, C_quarter_up,
                                 cq_w, cq_h, halfwidth, halfheight);
  DT_OMP_FOR()
  for(size_t i = 0; i < chsize; i++)
    C_h_den[i] = C_quarter_up[i] + C_detail_den[i];

cleanup_n_chroma:
  dt_free_align(C_quarter);
  dt_free_align(C_quarter_up);
  dt_free_align(C_detail);
  dt_free_align(C_quarter_den);
  dt_free_align(C_detail_den);
}


/* ================================================================
 * [DEPRECATED: GALOSH_RAW_N] gat_galosh_denoise_rawlc_n — N pipeline entry.
 *
 * STATUS: superseded by GALOSH_RAW_O (multi-scale LOESS pyramid +
 *   smoothstep slider walk).  N regressed −3.4 dB sRGB on SIDD
 *   `0001_S6_GRBG_010` because L-coupled WHT-LOSH on chroma is
 *   structurally inferior to LOESS regardless of multi-scale or L
 *   coupling.  Kept in source as the negative-result reference.
 *
 * ORIGINAL DESIGN (= what we tried):
 * N = L's GAT × WHT-LOSH × L/C decomposition pipeline EXTENDED with
 *   multi-scale (Laplacian pyramid) WHT-LOSH on luma + cross-channel
 *   L-coupled WHT-LOSH on chroma.  Replaces L's Phase 5(L) single-scale
 *   8x8 WHT-LOSH and Phase 5(C) LOESS chroma with multi-scale variants
 *   that maximise the L (full-res) / C (half-res) asymmetry GALOSH
 *   builds on.
 *
 * Design intent — "L coupling at every chroma stage":
 *   Phase 5(L) multi-scale = L's own multi-resolution structure used as
 *              its own prior; flat-region smoothing slider becomes
 *              truly functional via low-frequency band shrinkage that
 *              single-scale 8x8 WHT cannot reach.
 *   Phase 8    L-coupled  = L_guide_half (= 2x2 subsample of L_cs_den)
 *              guides chroma per-block BayesShrink threshold via pooled
 *              (C+L) AC variance.  chroma_strength slider works in flat
 *              regions while preserving L-aligned chroma edges.
 *   Phase 9    L-guided   = L_pixel (= overlap-avg of L_cs_den) guides
 *              K16 EWA-JL3 jinc kernel via bilateral weight, super-
 *              resolving chroma beyond half-Nyquist by borrowing L
 *              high-freq structure (= L variant unchanged).
 *
 * Pipeline (10 phases):
 *   Phase 0..4  identical to L Phase 0..4.
 *   Phase 5  ⭐ Multi-scale WHT-LOSH on luma (3-level Laplacian pyramid):
 *              decompose L_cs into {detail_0 (full), detail_1 (half),
 *              coarse_2 (quarter)}; WHT-LOSH per level with σ scaled
 *              by 0.5^k (= 2x2 box-avg variance reduction); reconstruct
 *              L_cs_den exactly inverse of decompose.
 *   Phase 6     L_pixel = 2x2 overlap-avg of L_cs_den (= L variant
 *              unchanged); L_guide_half = subsample L_cs_den at every
 *              other position (= L variant L_h_den convention).
 *   Phase 7     L_guide_half pyramid decompose (2 levels) →
 *              {L_guide_quarter, L_guide_detail_0}.
 *   Phase 8  ⭐ L-coupled multi-scale WHT-LOSH on chroma (3ch × 2-level):
 *              per channel decompose into {C_quarter, C_detail}; per
 *              level lcoupled WHT-LOSH (= pooled C+L AC BayesShrink);
 *              reconstruct C_h_den.
 *   Phase 9     Joint bilateral K16 EWA-JL3 upsample (= L variant
 *              unchanged): C_h_den + L_pixel guide → C_full_aligned.
 *   Phase 10    fused per-pixel WHT inverse + dark_ref restore + ×
 *              unified_sigma + inverse GAT (LUT) (= L Phase 8+9
 *              unchanged).
 *
 * Theoretical motivation:
 *   Single-scale 8x8 WHT-LOSH cannot denoise low-frequency noise below
 *   the 1/8-pixel Nyquist of its smallest detectable signal — flat
 *   regions accumulate noise floor that the slider cannot reach.
 *   Multi-scale extension (Burt-Adelson / Mallat) shrinks low-freq
 *   subbands at coarser resolutions where the noise floor is more
 *   visible.  L-coupled BayesShrink threshold (Chang-Yu-Vetterli 2000
 *   extended cross-channel) replaces LOESS bandwidth tuning with a
 *   per-block threshold modulated by cross-channel L AC variance,
 *   giving theoretically clean separation of edge/flat regions and
 *   full slider control over flat-region chroma smoothing.
 *
 * (日) GALOSH_RAW_N: L パイプラインに multi-scale + L-coupled 拡張。
 *   L (フル解像度) と C (半解像度) の解像度非対称性を最大限活用し、
 *   chroma processing 全 stage で L 情報を guide として使用。
 *   Phase 5(L) で 3-level Laplacian pyramid WHT-LOSH (= flat 域で
 *   slider 効く)、 Phase 8 で L-coupled BayesShrink (= cross-channel
 *   pooled variance、 LOESS 廃止)、 Phase 9 は joint bilateral upsample
 *   不変 (= L から full-res chroma 高周波借用)。
 * ================================================================ */
#ifndef GALOSH_RAW_NOMAIN
int main(int argc, char **argv)
{
  /* Strip --stride / --orient / --lfr-kernel flags out of argv before
   * parsing positional args.  Order-independent so the bench harness
   * can append them anywhere on the CLI. */
  int new_argc = 0;
  char *positional[32];
  /* [V2.0 A1] noise-model injection mode (parsed below, applied after input
   * read).  デフォルト "fit" = 従来経路そのまま（byte-identical 保証）。 */
  const char *noise_mode = "fit";
  const char *noise_state_path = NULL;
  for(int i = 0; i < argc; i++)
  {
    const char *a = argv[i];
    if(strncmp(a, "--wht=", 6) == 0)
    {
      const int b = atoi(a + 6);
      g_galosh_wht_block = (b == 4) ? 4 : 8;
      fprintf(stderr, "  --wht=%d (Phase 5 luma LOSH block size)\n",
              g_galosh_wht_block);
    }
    else if(strncmp(a, "--noise=", 8) == 0)
    {
      noise_mode = a + 8;
    }
    else if(strncmp(a, "--noise-state=", 14) == 0)
    {
      noise_state_path = a + 14;
    }
    else if(strncmp(a, "--upsample=", 11) == 0)
    {
      g_galosh_upsample_fast = (strcmp(a + 11, "fast") == 0);
      fprintf(stderr, "  --upsample=%s\n",
              g_galosh_upsample_fast ? "fast (guided bilinear)" : "jinc (K16)");
    }
    else if(strcmp(a, "--f16-storage") == 0)
    {
      g_galosh_f16_storage = 1;
      fprintf(stderr, "  --f16-storage (FP16 inter-phase storage oracle)\n");
    }
    else if(strncmp(a, "--stride=", 9) == 0)
    {
      g_galosh_stride = atoi(a + 9);
      if(g_galosh_stride < 1 || g_galosh_stride > 8) g_galosh_stride = 2;
    }
    else if(strncmp(a, "--orient=", 9) == 0)
    {
      g_galosh_n_orient = atoi(a + 9);
      if(g_galosh_n_orient != 1 && g_galosh_n_orient != 4) g_galosh_n_orient = 1;
    }
    else if(strncmp(a, "--lfr-kernel=", 13) == 0)
    {
      const char *k = a + 13;
      if(strcmp(k, "ewajl3") == 0 || strcmp(k, "ewa-jl3") == 0)
        g_galosh_lfr_kernel = 1;
      else
        g_galosh_lfr_kernel = 0;
    }
    else if(strncmp(a, "--unified=", 10) == 0)
    {
      g_galosh_unified = (atoi(a + 10) != 0);
    }
    else if(strncmp(a, "--k13-block=", 12) == 0)
    {
      const int b = atoi(a + 12);
      if(b == 4 || b == 8) g_galosh_k13_block = b;
      else                 g_galosh_k13_block = 8;
    }
    else if(strncmp(a, "--pass1=", 8) == 0)
    {
      const char *v = a + 8;
      if(strcmp(v, "a1") == 0 || strcmp(v, "A1") == 0) g_galosh_pass1_mode = 1;
      else                                              g_galosh_pass1_mode = 0;
      fprintf(stderr, "  --pass1=%s (mode=%d)\n", v, g_galosh_pass1_mode);
    }
    else if(strncmp(a, "--super-clean-threshold=", 24) == 0)
    {
      g_galosh_super_clean_threshold = (float)atof(a + 24);
      if(g_galosh_super_clean_threshold < 0.0f) g_galosh_super_clean_threshold = 0.0f;
      fprintf(stderr, "  --super-clean-threshold=%.6f\n",
              g_galosh_super_clean_threshold);
    }
    else if(strncmp(a, "--unified-sigma=", 16) == 0)
    {
      g_galosh_unified_sigma_override = (float)atof(a + 16);
      if(g_galosh_unified_sigma_override < 0.0f) g_galosh_unified_sigma_override = 0.0f;
      fprintf(stderr, "  --unified-sigma=%.6f (Phase 1 measurement bypassed)\n",
              g_galosh_unified_sigma_override);
    }
    else if(strncmp(a, "--chroma-up=",     12) == 0 ||
            strncmp(a, "--robust-shrink=", 16) == 0 ||
            strncmp(a, "--chroma-wiener=", 16) == 0 ||
            strncmp(a, "--chroma-method=", 16) == 0)
    {
      /* Deprecated flags from the v0..v6 development variants -- these
       * features are now baked into GALOSH_RAW_G's definition (chroma-up
       * + robust-shrink) or removed as archived experiments (chroma-
       * wiener / chroma-method).  Silently accepted for bench-script
       * backward compatibility. */
    }
    else
    {
      if(new_argc < 32) positional[new_argc++] = (char *)a;
    }
  }
  argv = positional;
  argc = new_argc;

  if(argc < 5)
  {
    fprintf(stderr,
      "Usage: %s input.bin output.bin width height\n"
      "       [method] [strength] [luma_str] [chroma_str]\n"
      "       [alpha] [sigma_sq]\n"
      "       [--variant=V]     (o | n | m | l | k | j | i | h | g, default o;\n"
      "                          O = LATEST (L + multi-scale LOESS chroma pyramid\n"
      "                              + smoothstep slider walk + L-guided upsample),\n"
      "                          N = DEPRECATED (multi-scale luma + L-coupled WHT-LOSH\n"
      "                              chroma; -3.4 dB regression, superseded by O),\n"
      "                          M = PREVIOUS (H + hierarchical Bayesian Phase 5(C)),\n"
      "                          L = PREVIOUS (joint bilateral K16, K Phase 6+8 fused),\n"
      "                          K = PREVIOUS (J + Bayesian-correct Phase 8 ε/BW),\n"
      "                          J = PREVIOUS (I + L-guided chroma refinement),\n"
      "                          I = PREVIOUS hybrid (L from H + C from G),\n"
      "                          H = full-res cycle-spinning + overlap-add inverse,\n"
      "                          G = half-res LOSH + K14/K15/K16 chromaup)\n"
      "       [--stride=N]      (G-only: 1 or 2, default 2; A enables stride=1)\n"
      "       [--orient=N]      (G-only: 1 or 4, default 1; B enables orient=4)\n"
      "       [--lfr-kernel=K]  (G-only: box | ewajl3, default box; C: ewajl3)\n"
      "       [--noise=M]       (fit | hold | every:N | ema:B; video-loop noise-model\n"
      "                          injection -- stateful modes need --noise-state=FILE)\n"
      "       [--noise-state=F] (one-line state file: alpha sigma_sq frames_since_fit)\n"
      "       [--wht=B]         (8 = canonical | 4 = fast luma LOSH, coarser grain)\n"
      "       [--upsample=K]    (jinc = canonical K16 | fast = guided bilinear,\n"
      "                          ring-free by construction, slightly softer chroma)\n"
      "       [--f16-storage]   (FP16 inter-phase storage oracle: rounds major\n"
      "                          phase buffers through binary16; GPU FP16 parity ref)\n"
      "\n"
      "  method:  'galosh' or 'ours' (GALOSH local WHT shrinkage, default)\n"
      "  luma_str:   sigma_L for luma shrinkage (default 0.5, user-tunable)\n"
      "  chroma_str: sigma_C for chroma shrinkage (default 1.0, user-tunable)\n"
      "  alpha:      P-G shot noise gain (auto if <= 0)\n"
      "  sigma_sq:   read noise variance (auto if <= 0)\n",
      argv[0]);
    return 1;
  }

  if(g_verbose) fprintf(stderr, "[GALOSH] RAW denoise: overlapping-WHT luma + 3-level LOESS chroma pyramid\n");

  const char *input_file = argv[1];
  const char *output_file = argv[2];
  const int width = atoi(argv[3]);
  const int height = atoi(argv[4]);

  const char *method = (argc > 5) ? argv[5] : "galosh";
  const float strength = (argc > 6) ? (float)atof(argv[6]) : 1.0f;

  /* sigma_L (luma shrinkage strength):
   *   In GAT-normalized space, true noise sigma = 1.0.
   *   sigma_L < 1.0 means conservative denoising (preserve detail).
   *   Default 1.0: full-strength denoising matching the noise model.
   *   Applied to full-res raw GALOSH (Phase 4) where sub-pixel
   *   detail matters; also used for half-res luma pilot (Phase 3).
   *
   * sigma_C (chroma shrinkage strength):
   *   Applied in Phase 3 luma-guided chroma Wiener.
   *   Default 1.0: matches GAT-normalized noise sigma exactly.
   *   Since chroma Wiener uses luma pilot (not chroma pilot),
   *   sigma_C directly controls the denoising/preservation tradeoff
   *   without affecting the pilot quality. */
  const float luma_str = (argc > 7) ? (float)atof(argv[7]) : 0.5f;
  const float chroma_str = (argc > 8) ? (float)atof(argv[8]) : 1.0f;

  /* CLI override for Phase 0 (α, σ²) — argv[9] = α, argv[10] = σ²
   *   0 = use binary's internal Foi-Alenius blind estimation (= default)
   *   > 0 = bypass Phase 0, force these values throughout the pipeline
   * Mechanism: positive override sets globals consumed by galosh_estimate_noise
   * top-of-function short-circuit (see galosh_cpu.h).  Used by:
   *   - run_oracle_parallel.py    (= GT-derived oracle (α, σ²))
   *   - EM_iter Python prototype  (= test alternative blind estimators without
   *                                 rebuilding the binary)
   *   - any external (α, σ²) source for ablation studies                       */
  const float alpha_in    = (argc > 9)  ? (float)atof(argv[9])  : 0.0f;
  const float sigma_sq_in = (argc > 10) ? (float)atof(argv[10]) : 0.0f;
  if(alpha_in > 0.0f && sigma_sq_in >= 0.0f)
  {
    g_galosh_alpha_override    = alpha_in;
    g_galosh_sigma_sq_override = sigma_sq_in;
    fprintf(stderr, "  noise_est override: alpha=%.8f sigma_sq=%.10f\n",
            alpha_in, sigma_sq_in);
  }
  (void)strength; /* strength is absorbed into luma_str/chroma_str */

  if(width <= 0 || height <= 0 || (width & 1) || (height & 1) ||
     (size_t)width > SIZE_MAX / sizeof(float) / (size_t)height)
  {
    fprintf(stderr, "Invalid dimensions: %dx%d (need positive, even W/H)\n", width, height);
    return 1;
  }

  fprintf(stderr, "Raw Denoiser Standalone v6 (GALOSH)\n");
  fprintf(stderr, "  Input:  %s (%dx%d)\n", input_file, width, height);
  fprintf(stderr, "  Method: %s\n", method);
  fprintf(stderr, "  Params: luma=%.2f chroma=%.2f\n", luma_str, chroma_str);

  /* Initialize Kaiser window */
  init_galosh_kaiser();

  /* Read input */
  const size_t npixels = (size_t)width * height;
  float *in = dt_alloc_align_float(npixels);
  float *out = dt_alloc_align_float(npixels);
  if(!in || !out)
  {
    fprintf(stderr, "Memory allocation failed\n");
    dt_free_align(in); dt_free_align(out);
    return 1;
  }

  FILE *fin = fopen(input_file, "rb");
  if(!fin)
  {
    fprintf(stderr, "Cannot open %s\n", input_file);
    dt_free_align(in); dt_free_align(out);
    return 1;
  }
  size_t nread = fread(in, sizeof(float), npixels, fin);
  fclose(fin);
  if(nread != npixels)
  {
    fprintf(stderr, "Read %zu floats, expected %zu\n", nread, npixels);
    fprintf(stderr, "hint: the CLI reads a raw float32 plane (exactly W*H*4 bytes,\n"
                    "      [0,1] Bayer) — image containers (DNG/TIFF/PNG) cannot be\n"
                    "      passed directly. Convert first: tools/dng_to_bin.py prints\n"
                    "      the exact W H to use.\n");
    dt_free_align(in); dt_free_align(out);
    return 1;
  }

  /* ================= [V2.0 A1] noise-model injection modes =================
   * EN: Reference semantics for the video API (GPU: galosh_set_noise_update).
   *   --noise=fit      per-frame blind fit (default; legacy path, untouched)
   *   --noise=hold     use (alpha, sigma_sq) from --noise-state, no fit
   *   --noise=every:N  refit when frames_since_fit >= N, else hold stored
   *   --noise=ema:B    fit every frame, blend new = B*fit + (1-B)*stored
   * The state file (--noise-state) is one line: alpha sigma_sq frames_since_fit,
   * so a scripted video loop shares the model across frame-by-frame
   * invocations.  The pipeline itself is untouched: stateful modes compute
   * the model HERE and feed the existing override short-circuit (the
   * argv[9]/argv[10] mechanism), which is why --noise=fit stays
   * byte-identical to the legacy CLI.
   * JP: 動画 API の参照意味論。fit は従来経路そのまま（byte-identical）。
   * hold/every/ema は main 側でモデルを確定して既存 override 機構へ注入する
   * だけで、パイプライン本体は無変更。state ファイルが フレーム間の共有点。 */
  if(strcmp(noise_mode, "fit") != 0)
  {
    float st_a = 0.0f, st_s = 0.0f;
    int st_n = -1;                      /* stored frames_since_fit; -1 = none */
    if(noise_state_path)
    {
      FILE *sf = fopen(noise_state_path, "r");
      if(sf)
      {
        if(fscanf(sf, "%f %f %d", &st_a, &st_s, &st_n) != 3) st_n = -1;
        fclose(sf);
      }
    }
    const int have_state = (st_n >= 0 && st_a > 0.0f);
    float use_a = 0.0f, use_s = 0.0f;
    int new_n = 0;

    if(strcmp(noise_mode, "hold") == 0)
    {
      if(!have_state)
      {
        fprintf(stderr, "--noise=hold requires a valid --noise-state file\n");
        dt_free_align(in); dt_free_align(out);
        return 1;
      }
      use_a = st_a; use_s = st_s; new_n = st_n + 1;
    }
    else if(strncmp(noise_mode, "every:", 6) == 0)
    {
      const int N = atoi(noise_mode + 6);
      if(have_state && st_n + 1 < (N > 0 ? N : 1))
      {
        use_a = st_a; use_s = st_s; new_n = st_n + 1;
      }
      else
      {
        const galosh_noise_params_t est = galosh_estimate_noise(in, width, height);
        use_a = est.alpha; use_s = est.sigma_sq; new_n = 0;
      }
    }
    else if(strncmp(noise_mode, "ema:", 4) == 0)
    {
      const float beta_in = (float)atof(noise_mode + 4);
      const float b = (beta_in > 0.0f && beta_in <= 1.0f) ? beta_in : 1.0f;
      const galosh_noise_params_t est = galosh_estimate_noise(in, width, height);
      if(have_state)
      {
        use_a = b * est.alpha    + (1.0f - b) * st_a;
        use_s = b * est.sigma_sq + (1.0f - b) * st_s;
      }
      else
      {
        use_a = est.alpha; use_s = est.sigma_sq;
      }
      new_n = 0;
    }
    else
    {
      fprintf(stderr, "Unknown --noise mode '%s' (fit|hold|every:N|ema:B)\n",
              noise_mode);
      dt_free_align(in); dt_free_align(out);
      return 1;
    }

    g_galosh_alpha_override    = use_a;
    g_galosh_sigma_sq_override = use_s;
    fprintf(stderr, "  noise mode=%s: alpha=%.8f sigma_sq=%.10f (frames_since_fit=%d)\n",
            noise_mode, use_a, use_s, new_n);
    if(noise_state_path)
    {
      FILE *sf = fopen(noise_state_path, "w");
      if(sf)
      {
        fprintf(sf, "%.9g %.9g %d\n", use_a, use_s, new_n);
        fclose(sf);
      }
      else
        fprintf(stderr, "WARNING: cannot write --noise-state %s\n", noise_state_path);
    }
  }

  /* Process */
  dt_iop_roi_t roi = { .width = width, .height = height };
  double t_start = omp_get_wtime();

  if(strcmp(method, "galosh") == 0 || strcmp(method, "ours") == 0)
  {
    galosh_raw_denoise(in, out, &roi, luma_str, chroma_str, 0, 0, 0);
  }
  else
  {
    fprintf(stderr, "Unknown method '%s'. Use 'galosh' or 'ours'.\n", method);
    dt_free_align(in);
    dt_free_align(out);
    return 1;
  }

  double elapsed = omp_get_wtime() - t_start;
  fprintf(stderr, "  Elapsed: %.2f seconds\n", elapsed);

  /* Write output */
  FILE *fout = fopen(output_file, "wb");
  if(!fout) { fprintf(stderr, "Cannot open %s for writing\n", output_file); return 1; }
  size_t nwrote = fwrite(out, sizeof(float), npixels, fout);
  fclose(fout);
  if(nwrote != npixels) { fprintf(stderr, "Short write to %s\n", output_file); return 1; }

  fprintf(stderr, "  Output: %s\n", output_file);

  dt_free_align(in);
  dt_free_align(out);
  return 0;
}
#endif /* GALOSH_RAW_NOMAIN */
