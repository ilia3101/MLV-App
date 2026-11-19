/* galosh_raw_cpu.c — GALOSH RAW: fully-blind RAW Bayer denoiser (CPU FP32 reference).
 *
 * GALOSH = Generalized Anscombe LOcal SHrinkage.  Full-resolution chroma
 * pipeline:
 *   P0  blind noise estimation -> alpha, sigma^2 (or a supplied clip model)
 *   P1  GAT forward (variance-stabilize) + per-CFA sigma + unit-variance normalize
 *   P2  dark-reference IRLS (achromatic) + CFA-aware subtract
 *   P3  stride-1 cycle-spun 2x2 WHT -> L (luma) / C1,C2,C3 (chroma), full-res,
 *       followed by CFA sign-flip demodulation
 *   P5  luma: overlapping-block BayesShrink-MAD + empirical Wiener (cycle-spun);
 *       chroma: fixed 3-anchor full-res ladder
 *         A1 = full-res LOESS R=11
 *         A2 = half-res LOESS R=11 -> K16
 *         A3 = half-res LOESS R=22 -> K16
 *       with smoothstep blends over c = 0..3
 *   P6  CFA remodulation + exact 4-block overlap-add inverse WHT
 *   P7  dark restore + denormalize + inverse GAT -> output
 *
 * There is no half-res chroma path, no K16 final upsample and no L_pixel tent.
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

/* [fullres-chroma] Full-resolution chroma path (CPU + Vulkan).
 *
 * Stride-1 4-plane WHT (L, C1, C2, C3 all W x H), CFA-demodulated full-res
 * chroma, a fixed 3-anchor scale ladder, and the exact 4-block overlap-add
 * inverse.  No half-res chroma, no K16 final upsample, no L_pixel tent.
 *
 * Fixed ladder (strength c):
 *   c < 1 : raw <-> A1          (smoothstep)
 *   c = 1 : A1 = full-res LOESS R=11
 *   1 < c < 2 : A1 <-> A2
 *   c = 2 : A2 = half-res LOESS R=11 -> K16
 *   2 < c < 3 : A2 <-> A3
 *   c = 3 : A3 = half-res LOESS R=22 -> K16
 *   c > 3 : saturate at A3
 *
 * BW stays user-controllable (--chroma-bw in the CLI; mapped from the
 * "Chroma preserve" slider in the app). */
#define GALOSH_CHROMA_R_BASE 11
#define GALOSH_CHROMA_R_HI   22
static float g_galosh_chroma_full_bw  = 1.0f;

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

/* [fullres-chroma] One 2x guided upsample step inside the full-res pyramid.
 *
 * K16 hardcodes its output stride as 2*src_w x 2*src_h.  When the destination
 * level has an odd dimension, 2*src_w is one pixel short; crop the destination
 * luma guide to the K16 footprint and edge-pad the result back to the real
 * destination size (same stride fix as the half-res O pyramid). */
static int galosh_chroma_upsample_level(
    const float *c1_src, const float *c2_src, const float *c3_src,
    const float *L_dst,
    const int src_w, const int src_h,
    const int dst_w, const int dst_h,
    float *c1_dst, float *c2_dst, float *c3_dst)
{
  const int fw = 2 * src_w;
  const int fh = 2 * src_h;
  if(fw == dst_w && fh == dst_h)
  {
    gat_chroma_upsample(c1_src, c2_src, c3_src, L_dst,
                        c1_dst, c2_dst, c3_dst,
                        src_w, src_h, /*BW=*/1.5f);
    return 0;
  }

  const size_t fsize = (size_t)fw * fh;
  float *L_fit = dt_alloc_align_float(fsize);
  float *o1 = dt_alloc_align_float(fsize);
  float *o2 = dt_alloc_align_float(fsize);
  float *o3 = dt_alloc_align_float(fsize);
  if(!L_fit || !o1 || !o2 || !o3)
  {
    dt_free_align(L_fit);
    dt_free_align(o1); dt_free_align(o2); dt_free_align(o3);
    return -1;
  }
  gat_crop_2d_topleft(L_dst, dst_w, dst_h, L_fit, fw, fh);
  gat_chroma_upsample(c1_src, c2_src, c3_src, L_fit,
                      o1, o2, o3, src_w, src_h, /*BW=*/1.5f);
  gat_pad_2d_edge(o1, fw, fh, c1_dst, dst_w, dst_h);
  gat_pad_2d_edge(o2, fw, fh, c2_dst, dst_w, dst_h);
  gat_pad_2d_edge(o3, fw, fh, c3_dst, dst_w, dst_h);
  dt_free_align(L_fit);
  dt_free_align(o1); dt_free_align(o2); dt_free_align(o3);
  return 0;
}

/* [fullres-chroma] Build one full-res pyramid anchor A_{level+1}.
 *
 *   level 0 : LOESS on the full-res C0 -> A1
 *   level k : LOESS at level k, then cascade K16 back up to full res using
 *             the L guides at each level.  Only the LOESS at level k is
 *             needed; the intermediate chroma anchors are not.
 *
 * Output is always full-res (lw[0] x lh[0]). */
static int galosh_chroma_anchor_from_level(
    const int level, const int R, const float BW,
    float *L_lvl[4],
    float *C1_lvl[4], float *C2_lvl[4], float *C3_lvl[4],
    const int lw[4], const int lh[4],
    float *out1, float *out2, float *out3)
{
  if(level <= 0)
  {
    galosh_loess_chroma_3ch_r(L_lvl[0], C1_lvl[0], C2_lvl[0], C3_lvl[0],
                              out1, out2, out3,
                              lw[0], lh[0], /*strength=*/1.0f, R, BW);
    return 0;
  }

  const size_t nsrc = (size_t)lw[level] * lh[level];
  float *s1 = dt_alloc_align_float(nsrc);
  float *s2 = dt_alloc_align_float(nsrc);
  float *s3 = dt_alloc_align_float(nsrc);
  if(!s1 || !s2 || !s3)
  {
    dt_free_align(s1); dt_free_align(s2); dt_free_align(s3);
    return -1;
  }
  galosh_loess_chroma_3ch_r(L_lvl[level],
                            C1_lvl[level], C2_lvl[level], C3_lvl[level],
                            s1, s2, s3,
                            lw[level], lh[level], /*strength=*/1.0f, R, BW);

  for(int j = level - 1; j >= 0; j--)
  {
    float *d1, *d2, *d3;
    if(j == 0)
    {
      d1 = out1; d2 = out2; d3 = out3;
    }
    else
    {
      const size_t nd = (size_t)lw[j] * lh[j];
      d1 = dt_alloc_align_float(nd);
      d2 = dt_alloc_align_float(nd);
      d3 = dt_alloc_align_float(nd);
      if(!d1 || !d2 || !d3)
      {
        dt_free_align(d1); dt_free_align(d2); dt_free_align(d3);
        dt_free_align(s1); dt_free_align(s2); dt_free_align(s3);
        return -1;
      }
    }
    if(galosh_chroma_upsample_level(s1, s2, s3, L_lvl[j],
                                    lw[j + 1], lh[j + 1],
                                    lw[j], lh[j],
                                    d1, d2, d3) != 0)
    {
      if(j > 0) { dt_free_align(d1); dt_free_align(d2); dt_free_align(d3); }
      dt_free_align(s1); dt_free_align(s2); dt_free_align(s3);
      return -1;
    }
    dt_free_align(s1); dt_free_align(s2); dt_free_align(s3);
    s1 = d1; s2 = d2; s3 = d3;
  }
  if(s1 != out1) { dt_free_align(s1); }
  if(s2 != out2) { dt_free_align(s2); }
  if(s3 != out3) { dt_free_align(s3); }
  return 0;
}

/* [fullres-chroma] Fixed 3-anchor full-res chroma ladder.
 *
 * A1 = full-res LOESS R=11
 * A2 = half-res LOESS R=11 upsampled to full
 * A3 = half-res LOESS R=22 upsampled to full
 *
 * c < 1      raw <-> A1
 * 1 < c < 2  A1 <-> A2
 * 2 < c < 3  A2 <-> A3
 * c >= 3     A3
 *
 * Only the anchors needed for the current c are built.  Returns 0 on
 * success, -1 on allocation failure. */
static int galosh_chroma_pyramid_full(
    const float *L0,
    const float *C1_0, const float *C2_0, const float *C3_0,
    const int width, const int height,
    const float chroma_strength, const float BW,
    float *out1, float *out2, float *out3)
{
  const size_t npixels = (size_t)width * height;
  int rc = 0;

  if(chroma_strength <= 0.0f)
  {
    memcpy(out1, C1_0, sizeof(float) * npixels);
    memcpy(out2, C2_0, sizeof(float) * npixels);
    memcpy(out3, C3_0, sizeof(float) * npixels);
    return 0;
  }

  const int hw = width / 2;
  const int hh = height / 2;
  const int have_half = (hw >= 8 && hh >= 8);

  float *L_lvl[4]  = { (float *)L0, NULL, NULL, NULL };
  float *C1_lvl[4] = { (float *)C1_0, NULL, NULL, NULL };
  float *C2_lvl[4] = { (float *)C2_0, NULL, NULL, NULL };
  float *C3_lvl[4] = { (float *)C3_0, NULL, NULL, NULL };
  int lw[4] = { width, hw, 0, 0 };
  int lh[4] = { height, hh, 0, 0 };

  /* Ladder segment.  Anchor ids: 0=A1, 1=A2, 2=A3; lower=-1 is raw C0. */
  float t;
  int lower, upper;
  const float s = chroma_strength;
  if(s <= 1.0f)
  {
    t = s;
    t = t * t * (3.0f - 2.0f * t);   /* smoothstep */
    lower = -1;
    upper = 0;
  }
  else if(!have_half)
  {
    t = 1.0f;
    lower = -1;
    upper = 0;
  }
  else if(s < 2.0f)
  {
    t = s - 1.0f;
    t = t * t * (3.0f - 2.0f * t);
    lower = 0;
    upper = 1;
  }
  else if(s < 3.0f)
  {
    t = s - 2.0f;
    t = t * t * (3.0f - 2.0f * t);
    lower = 1;
    upper = 2;
  }
  else
  {
    t = 1.0f;
    lower = 2;
    upper = 2;
  }

  const int need_lo = (t < 1.0f);
  const int need_hi = (t > 0.0f);
  const int need_level1 =
      (need_lo && (lower == 1 || lower == 2)) ||
      (need_hi && (upper == 1 || upper == 2));

  if(need_level1)
  {
    const size_t nk = (size_t)hw * hh;
    L_lvl[1]  = dt_alloc_align_float(nk);
    C1_lvl[1] = dt_alloc_align_float(nk);
    C2_lvl[1] = dt_alloc_align_float(nk);
    C3_lvl[1] = dt_alloc_align_float(nk);
    if(!L_lvl[1] || !C1_lvl[1] || !C2_lvl[1] || !C3_lvl[1])
    {
      rc = -1;
      goto cleanup;
    }
    gat_box_downsample_2x(L_lvl[0],  L_lvl[1],  width, height);
    gat_box_downsample_2x(C1_lvl[0], C1_lvl[1], width, height);
    gat_box_downsample_2x(C2_lvl[0], C2_lvl[1], width, height);
    gat_box_downsample_2x(C3_lvl[0], C3_lvl[1], width, height);
  }

  float *lo1 = NULL, *lo2 = NULL, *lo3 = NULL;
  float *hi1 = NULL, *hi2 = NULL, *hi3 = NULL;

  if(need_lo && lower >= 0)
  {
    lo1 = dt_alloc_align_float(npixels);
    lo2 = dt_alloc_align_float(npixels);
    lo3 = dt_alloc_align_float(npixels);
    if(!lo1 || !lo2 || !lo3) { rc = -1; goto cleanup; }
    const int lvl = (lower == 0) ? 0 : 1;
    const int R   = (lower == 2) ? GALOSH_CHROMA_R_HI : GALOSH_CHROMA_R_BASE;
    if(galosh_chroma_anchor_from_level(lvl, R, BW,
                                       L_lvl, C1_lvl, C2_lvl, C3_lvl,
                                       lw, lh, lo1, lo2, lo3) != 0)
    { rc = -1; goto cleanup; }
  }
  if(need_hi)
  {
    hi1 = dt_alloc_align_float(npixels);
    hi2 = dt_alloc_align_float(npixels);
    hi3 = dt_alloc_align_float(npixels);
    if(!hi1 || !hi2 || !hi3) { rc = -1; goto cleanup; }
    const int lvl = (upper == 0) ? 0 : 1;
    const int R   = (upper == 2) ? GALOSH_CHROMA_R_HI : GALOSH_CHROMA_R_BASE;
    if(galosh_chroma_anchor_from_level(lvl, R, BW,
                                       L_lvl, C1_lvl, C2_lvl, C3_lvl,
                                       lw, lh, hi1, hi2, hi3) != 0)
    { rc = -1; goto cleanup; }
  }

  if(need_lo && need_hi)
  {
    if(lower < 0)
    {
      DT_OMP_FOR()
      for(size_t i = 0; i < npixels; i++)
      {
        out1[i] = (1.0f - t) * C1_0[i] + t * hi1[i];
        out2[i] = (1.0f - t) * C2_0[i] + t * hi2[i];
        out3[i] = (1.0f - t) * C3_0[i] + t * hi3[i];
      }
    }
    else
    {
      DT_OMP_FOR()
      for(size_t i = 0; i < npixels; i++)
      {
        out1[i] = (1.0f - t) * lo1[i] + t * hi1[i];
        out2[i] = (1.0f - t) * lo2[i] + t * hi2[i];
        out3[i] = (1.0f - t) * lo3[i] + t * hi3[i];
      }
    }
  }
  else if(need_lo)
  {
    if(lower < 0)
    {
      memcpy(out1, C1_0, sizeof(float) * npixels);
      memcpy(out2, C2_0, sizeof(float) * npixels);
      memcpy(out3, C3_0, sizeof(float) * npixels);
    }
    else
    {
      memcpy(out1, lo1, sizeof(float) * npixels);
      memcpy(out2, lo2, sizeof(float) * npixels);
      memcpy(out3, lo3, sizeof(float) * npixels);
    }
  }
  else
  {
    memcpy(out1, hi1, sizeof(float) * npixels);
    memcpy(out2, hi2, sizeof(float) * npixels);
    memcpy(out3, hi3, sizeof(float) * npixels);
  }

cleanup:
  dt_free_align(lo1); dt_free_align(lo2); dt_free_align(lo3);
  dt_free_align(hi1); dt_free_align(hi2); dt_free_align(hi3);
  dt_free_align(L_lvl[1]);
  dt_free_align(C1_lvl[1]); dt_free_align(C2_lvl[1]); dt_free_align(C3_lvl[1]);
  return rc;
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

  /* Pre-declare buffers for cleanup-on-error. */
  float *in_gat = NULL;
  float *L_cs = NULL;
  float *L_cs_den = NULL;
  /* Full-res chroma WHT planes and the ladder output. */
  float *C1_full = NULL, *C2_full = NULL, *C3_full = NULL;
  float *C1_full_den = NULL, *C2_full_den = NULL, *C3_full_den = NULL;
  float *recon_full = NULL;

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
                  "size=%dx%d | sigma_L=%.3f sigma_C=%.3f\n",
                  np.alpha, np.sigma_sq, unified_sigma,
                  sigma_gat_ch[0], sigma_gat_ch[1], sigma_gat_ch[2], sigma_gat_ch[3],
                  width, height, luma_strength, chroma_strength);

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

    /* ch_dark_ref[] is collected over the 2x2 blocks of the forward grid,
     * which is anchored at (0,0): slot 0 = TL, 1 = BL, 2 = TR, 3 = BR.
     * Index by absolute parity so the per-position anchor matches the grid
     * for every CFA phase (the grid is not shifted to put red at (0,0)). */
    DT_OMP_FOR()
    for(int r = 0; r < height; r++)
    {
      const int r_off = r & 1;
      for(int c = 0; c < width; c++)
      {
        const int c_off = c & 1;
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

  /* ============== [LATEST: GALOSH_RAW_O] Phase 3 — stride=1 forward 2x2 WHT.
   * half mode: L-only (chroma is extracted on the aligned quads in P4).
   * full mode: L + C1/C2/C3 at every pixel, then CFA demodulate the C planes.
   * ============== */
  L_cs = dt_alloc_align_float(npixels);
  if(!L_cs) goto cleanup_rawlc_o;
  C1_full = dt_alloc_align_float(npixels);
  C2_full = dt_alloc_align_float(npixels);
  C3_full = dt_alloc_align_float(npixels);
  if(!C1_full || !C2_full || !C3_full) goto cleanup_rawlc_o;
  gat_h_forward_wht_stride1(in_gat, L_cs, C1_full, C2_full, C3_full,
                            width, height);
  /* CFA sign-flip demodulation: C1 *= (-1)^r, C2 *= (-1)^c,
   * C3 *= (-1)^(r+c).  Self-inverse; remodulated before the inverse WHT. */
  gat_h_demodulate_chroma(C1_full, C2_full, C3_full, width, height);
  O32_CPU_DUMP("p3_C1_full", C1_full, npixels);
  O32_CPU_DUMP("p3_C2_full", C2_full, npixels);
  O32_CPU_DUMP("p3_C3_full", C3_full, npixels);
  O32_CPU_DUMP("p3_L_cs", L_cs, npixels);

  if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Phase 3 forward WHT (stride=1, L+C) done\n");

  dt_free_align(in_gat); in_gat = NULL;

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

  /* ================================================================
   * [fullres-chroma] Full-res branch — H/M chroma architecture with a
   * 3-level scale ladder.
   *
   * C1/C2/C3 were produced at every pixel by the stride-1 forward WHT
   * (Phase 3) and CFA-demodulated.  The pyramid helper denoises the
   * chroma at full / half / quarter / eighth resolution and blends the
   * anchors according to chroma_strength:
   *   c<1  : raw <-> full-res LOESS
   *   c 1-2: full <-> half
   *   c 2-3: half <-> quarter
   *   c 3-4: quarter <-> eighth
   * There is no K16 final upsample and no L_pixel tent: the reconstruction
   * uses the block-anchored full-res L and C planes directly.
   * ================================================================ */
  {
    C1_full_den = dt_alloc_align_float(npixels);
    C2_full_den = dt_alloc_align_float(npixels);
    C3_full_den = dt_alloc_align_float(npixels);
    if(!C1_full_den || !C2_full_den || !C3_full_den) goto cleanup_rawlc_o;

    GALOSH_F16_STAGE(C1_full, npixels);
    GALOSH_F16_STAGE(C2_full, npixels);
    GALOSH_F16_STAGE(C3_full, npixels);

    if(galosh_chroma_pyramid_full(L_cs_den,
                                  C1_full, C2_full, C3_full,
                                  width, height, chroma_strength,
                                  g_galosh_chroma_full_bw,
                                  C1_full_den, C2_full_den, C3_full_den) != 0)
      goto cleanup_rawlc_o;

    GALOSH_F16_STAGE(C1_full_den, npixels);
    GALOSH_F16_STAGE(C2_full_den, npixels);
    GALOSH_F16_STAGE(C3_full_den, npixels);
    O32_CPU_DUMP("p5c_C1_full_den", C1_full_den, npixels);
    O32_CPU_DUMP("p5c_C2_full_den", C2_full_den, npixels);
    O32_CPU_DUMP("p5c_C3_full_den", C3_full_den, npixels);

    dt_free_align(C1_full); dt_free_align(C2_full); dt_free_align(C3_full);
    C1_full = C2_full = C3_full = NULL;

    /* Remodulate: re-apply the CFA sign flips before the inverse WHT. */
    gat_h_demodulate_chroma(C1_full_den, C2_full_den, C3_full_den,
                            width, height);

    recon_full = dt_alloc_align_float(npixels);
    if(!recon_full) goto cleanup_rawlc_o;
    gat_h_inverse_overlap_add(L_cs_den, C1_full_den, C2_full_den, C3_full_den,
                              recon_full, width, height);
    GALOSH_F16_STAGE(recon_full, npixels);
    O32_CPU_DUMP("p7_recon_full", recon_full, npixels);

    if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Full-res chroma ladder done "
                          "(R=11/22 BW=%.2f), 4-block overlap-add inverse done\n",
                          g_galosh_chroma_full_bw);

    dt_free_align(L_cs_den); L_cs_den = NULL;

    /* Phase 10 — per-pixel dark_ref restore + x unified_sigma + inverse GAT. */
    DT_OMP_FOR()
    for(int fr = 0; fr < height; fr++)
    {
      const int r_off = fr & 1;
      for(int fc = 0; fc < width; fc++)
      {
        const int c_off = fc & 1;
        const int slot  = r_off | (c_off << 1);
        const size_t pos = (size_t)fr * width + fc;
        const float val = (recon_full[pos] + ch_dark_ref[slot]) * unified_sigma;
        out[pos] = gat_inverse_exact(val);
      }
    }
    O32_CPU_DUMP("p10_output", out, npixels);
    if(g_verbose) fprintf(stderr, "[GALOSH_RAW_O] Phase 10 full-res inverse WHT "
                          "+ denorm + inv-GAT done\n");
    goto cleanup_rawlc_o;
  }

cleanup_rawlc_o:
  dt_free_align(in_gat);
  dt_free_align(L_cs);
  dt_free_align(L_cs_den);
  dt_free_align(C1_full); dt_free_align(C2_full); dt_free_align(C3_full);
  dt_free_align(C1_full_den); dt_free_align(C2_full_den); dt_free_align(C3_full_den);
  dt_free_align(recon_full);
}


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
    else if(strncmp(a, "--chroma-r=", 11) == 0)
    {
      /* Fixed ladder: R=11 for A1/A2, R=22 for the high anchor. */
      fprintf(stderr, "  note: --chroma-r is fixed at 11/22; ignoring %s\n",
              a + 11);
    }
    else if(strncmp(a, "--chroma-bw=", 12) == 0)
    {
      const float bw = (float)atof(a + 12);
      if(!(bw > 0.0f))
      {
        fprintf(stderr, "Invalid --chroma-bw=%s (must be > 0)\n", a + 12);
        return 1;
      }
      g_galosh_chroma_full_bw = bw;
      fprintf(stderr, "  --chroma-bw=%.3f (full-res LOESS bilateral sigma)\n",
              g_galosh_chroma_full_bw);
    }
    else if(strcmp(a, "--f16-storage") == 0)
    {
      g_galosh_f16_storage = 1;
      fprintf(stderr, "  --f16-storage (FP16 inter-phase storage oracle)\n");
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
      "       [--noise=M]       (fit | hold | every:N | ema:B; video-loop noise-model\n"
      "                          injection -- stateful modes need --noise-state=FILE)\n"
      "       [--noise-state=F] (one-line state file: alpha sigma_sq frames_since_fit)\n"
      "       [--wht=B]         (8 = canonical | 4 = fast luma LOSH, coarser grain)\n"
      "       [--upsample=K]    (jinc = canonical K16 | fast = guided bilinear,\n"
      "                          ring-free by construction, slightly softer chroma)\n"
      "       [--chroma-bw=X]   (full-res LOESS bilateral sigma, default 1.0)\n"
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
  fprintf(stderr, "  Params: luma=%.2f chroma=%.2f full-res chroma "
                  "(R=11/22 BW=%.2f)\n",
          luma_str, chroma_str, g_galosh_chroma_full_bw);

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
