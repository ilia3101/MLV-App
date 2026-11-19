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

## Noise model

GALOSH is blind, but MLV-App uses one model per clip instead of one per frame.
The model is the two-scalar relation `Var(x) = alpha*x + sigma_sq`:

- `alpha`: shot-noise gain
- `sigma_sq`: Gaussian read-noise variance

The GAT transform and its inverse LUT are derived from these two values, so
exposing and editing them is enough to tune the model.

The model is never estimated on import, clip open, preview, thumbnail or
export.  It is estimated from exactly one frame:

- when the user presses **Set current frame for noise estimation** (the frame
  currently shown in the viewer becomes the clip's stored noise frame), or
- when GALOSH luma > 0 and no valid model exists, including a recipe paste
  with luma > 0 but no **GALOSH Noise Model** payload.

`alpha` and `sigma_sq` are exposed as editable fields after the first estimate
or a recipe paste.  Editing them re-renders but keeps the model valid.

A valid model is invalidated and re-estimated from the stored noise frame when
raw black/white levels, raw fixes, dual-ISO settings or the 1x3 anamorphic
factor change.  If no model existed, those changes leave it unset.  The stored
noise frame is per clip and session-only; `alpha`/`sigma_sq` are stored per clip
in the session and copied by recipes only with the separate **GALOSH Noise
Model** checkbox.

Estimation runs on the CPU even when the Vulkan backend is selected, so both
backends consume exactly the same model.  The Vulkan host skips its own
alpha/sigma fit when the bridge supplies one.

## Full-resolution chroma

The RAW path now uses full-resolution chroma only.  Phase 3 runs the stride-1
4-plane WHT (`L, C1, C2, C3` at W x H), demodulates the CFA sign flips, and the
chroma ladder operates on the full-res lattice:

- A1 = full-res LOESS R=11
- A2 = half-res LOESS R=11 -> K16
- A3 = half-res LOESS R=22 -> K16
- `c < 1`: raw <-> A1; `1 < c < 2`: A1 <-> A2; `2 < c < 3`: A2 <-> A3;
  `c >= 3`: saturate at A3.
- Reconstruction uses the exact 4-block overlap-add inverse; there is no K16
  final upsample and no `L_pixel` tent.

The UI maps:

- Luma `0..800` -> `luma = value/100`; 0 disables GALOSH, 100 = reference l=1.
- Chroma `0..300` -> `chroma = value/100`; 100 = c=1, 300 = c=3.
- Chroma preserve `0..100` -> `bw = 1.0 - 0.99*(value/100)`; 1.0 gives maximum
  detail (`chroma-bw=0.01`), 0.0 gives minimum (`chroma-bw=1.0`).

The CPU and Vulkan implementations use the same ladder and parameters.  The
half-res chroma path and its shaders have been removed.

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

The noise model is estimated **after** the 3x horizontal Bayer upsample, so it
matches the grid the denoiser actually processes.  Changing the 1x3 factor
therefore invalidates and re-estimates the model.

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
- `galosh_raw_cpu.c`: full-resolution chroma only (stride-1 WHT + fixed
  R=11/R=22 ladder); the half-res chroma branch and archived variants were
  removed.
- `galosh_bridge.c` / `video_mlv.c`: add the single-frame per-clip noise model
  estimator used by the UI button and by invalidation re-estimation; the bridge
  and render paths never estimate the model per frame.  For 1x3 clips the model
  is estimated after the horizontal Bayer upsample.
- `galosh_bridge.c`: expose the per-clip `chroma_bw` from the UI's inverse
  "Chroma preserve" slider.
- `galosh_vk_host.c` (Vulkan branch): skip the duplicate alpha/sigma fit and
  dark-Laplacian pass when the bridge supplies the clip model; keep the
  per-frame dark-threshold pass and upload the supplied model before GAT/LUT.
- `galosh_cpu.h`: allocator stubs use `posix_memalign` / `_aligned_malloc`
  (MLV-App is built as C99, where `aligned_alloc` is not declared).
- `galosh_cpu.h`: `_Float16` is optional; MLV-App does not use the FP16-storage
  oracle.
- `galosh_cpu.h`: SSE includes are guarded for non-x86 builds.
- `galosh_cpu.h`: per-call diagnostic prints were removed.
- `galosh_vk_host.c`: the adaptive pass12 banding enforces at least eight
  queue submissions, so a probe underestimate cannot submit one
  watchdog-tripping dispatch on slow integrated GPUs.
- `galosh_vk_host.c`: portability enumeration/subset support and MoltenVK ICD
  probing were added for the optional macOS Vulkan backend.

## Vulkan backend

The Vulkan host is in `galosh_vk_host.c`, with shader sources and precompiled
SPIR-V in `vulkan/shaders/`.  The backend is off by default.  Enable it with:

```sh
qmake CONFIG+=galosh_vulkan path/to/MLVApp.pro
```

Requirements: Vulkan headers and a loader (`-lvulkan` on Linux, `vulkan-1` on
Windows, MoltenVK on macOS).  Vulkan headers are not vendored; use the system
headers or the LunarG SDK (`VULKAN_SDK`).  On macOS, Homebrew provides:

```sh
brew install molten-vk vulkan-headers vulkan-loader
export VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
```

The build copies the `.spv` files to `<build>/galosh_shaders`; on macOS they
are copied into `MLV App.app/Contents/MacOS/galosh_shaders`.  The host uses the
subgroup-32 `pass12_sg` kernel when the device advertises
`VK_EXT_subgroup_size_control`; otherwise it uses the classic pass12 kernel.
Set `GALOSH_VK_FORCE_CLASSIC=1` to force the classic path for troubleshooting.
Timestamp profiling runs only with `GALOSH_VERBOSE=1`.

Regenerate SPIR-V with:

```sh
cd src/processing/galosh/vulkan
for k in shaders/*.comp; do
  glslc -O --target-env=vulkan1.2 "$k" -o "${k%.comp}.spv"
done
```
