# GALOSH integration

Vendored from <https://github.com/luxgrain/GALOSH> (Apache-2.0).  The upstream
CPU FP32 RAW reference (`galosh_cpu.h`, `galosh_raw_cpu.c`) is the canonical
implementation; the Vulkan host is an optional backend.

The denoiser runs on linear Bayer data in `[0,1]` after MLV raw corrections
and before white balance/debayer.  `getMlvRawFrameFloat()` has already called
`applyLLRawProcObject()`, so dual-ISO deinterleaving, pattern-noise and stripe
corrections are applied before GALOSH; the bridge always receives a corrected
Bayer mosaic.

Normalization matches the reference `galosh-dng` tool: the bridge uses the
native black/white levels of the DNG export (not MLV-App's 0.993 highlight
bias) and clips the normalized mosaic to `[0,1]` before denoising.  This keeps
the blind noise estimate, the GAT transform and the luma/chroma response
aligned with the reference implementation.  On the 3x3 test frames the
normalized input matches the DNG input to float precision and the denoised
output differs from the reference DNG by roughly 1e-4 (DNG quantization).

## 1x3 anamorphic clips

For 1x3 MLV footage one raw pixel is three display pixels wide.  Running GALOSH
on the raw grid would make its square WHT blocks and chroma windows three times
wider after the display stretch, producing heavy horizontal smearing and
coloured stepping on high-contrast edges.

The bridge therefore denoises 1x3 footage on a horizontally upsampled Bayer
grid:

- the mosaic is upsampled by 3 along x with a CFA-aware 1D interpolation;
- only odd factors are accepted, because `parity(3*x) == parity(x)` preserves
  the 2x2 CFA pattern and its phase;
- the existing CPU/Vulkan backend runs unchanged on the larger grid;
- the result is sampled back at the original pixel positions.

The noise model is always estimated on the original grid.  Re-estimating it on
the upsampled grid lowers `alpha` by roughly an order of magnitude, which makes
the inverse-GAT table build dramatically slower.

Some Magic Lantern builds occasionally do not write the crop/skipping metadata needed to
auto-detect 1x3.  In the Qt UI the factor is therefore driven by the manual
`0.33x` height-stretch selection (which makes the viewer stretch horizontally
by 3x); `getMlvAspectRatio()` remains a fallback for files that do carry the
metadata.  The 3x path is slower and is not used for filmstrip thumbnails.

The Vulkan host splits the long luma pass into at least eight queue
submissions.  The adaptive probe can underestimate the per-row rate on slow
integrated GPUs, and a single oversized submission trips the GPU watchdog; the
minimum band count keeps every submission bounded without a resolution-based
backend switch.  CPU is used only when the Vulkan backend is not compiled in or
no usable Vulkan device is available; a GPU call that reports an error leaves
the frame untouched rather than silently switching backends.

## Local changes to upstream sources

- `galosh_raw_cpu.c`: `main()` is wrapped in `#ifndef GALOSH_RAW_NOMAIN`.
- `galosh_raw_cpu.c`: archived x86 SSE helpers are guarded for non-x86 builds.
- `galosh_raw_cpu.c`: `galosh_raw_denoise()` accepts a CFA phase so RGGB,
  GRBG, GBRG and BGGR MLV files are handled without resampling.
- `galosh_cpu.h`: allocator stubs use `posix_memalign` / `_aligned_malloc`
  (MLV-App is built as C99, where `aligned_alloc` is not declared).
- `galosh_cpu.h`: `_Float16` is optional; MLV-App does not use the FP16-storage
  oracle.
- `galosh_cpu.h`: SSE includes are guarded for non-x86 builds.
- `galosh_cpu.h`: per-call diagnostic prints were removed.

## Backend API

This change builds the CPU FP32 reference only.  The Vulkan host is maintained
as a separate change; the bridge keeps the small `galosh_vulkan_*` API and a
no-op stub so the GPU implementation can be added without changing the UI or
the cache integration.
