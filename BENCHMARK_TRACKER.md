# MLV-App Apple Silicon Benchmark Tracker

Timings on an **Apple Silicon M1 Mac (8-core CPU, 8 GB RAM)** for three builds:

1. **Original**: unmodified MLV App v1.16.
2. **PR #334**: ARM NEON for AMaZE demosaic, AVFoundation rate-limiting, memory pool persistence.
3. **Today** (`dualiso-chroma-opt`): faster 5×5 chroma smooth (SIMD + OpenMP), histogram-matching bypass, diagnostic print removal. These were timed together, not separately.

How to read the tables:

- Export and playback times are stopwatch results. Run-to-run variation is about ±5 s on long exports (the same FFmpeg export took 252 s and 257 s), so small differences are not real.
- "not measured" means no timing exists for that build. Speed-ups are only given where both times exist.
- Speed-up is Original ÷ Today.

---

## 1. Export times

### Clip A: 4.4K Dual ISO (`M13-1815.MLV`)

- Frame size: 1472×1846 anamorphic (4416×1846 after de-squeeze)
- Frames: 736 (about 30.7 s at 23.974 fps)
- File size: 1.74 GB (format to confirm)
- Settings on in every run: Force AMaZE, Fix Bad Pixels (Force, Norm), Fix Focus Pixels, Dual ISO Force ON (AMaZE, Histogram, Exposure -2.06, Black Delta 4)

| Preset | Original | PR #334 | Today | Speed-up |
| :--- | :--- | :--- | :--- | :--- |
| CinemaDNG Uncompressed, chroma off | 3 min 28 s (208 s)\* | 2 min 05 s (125 s) | 1 min 24 s (84 s) | 2.48×\* |
| CinemaDNG Uncompressed, chroma 5×5 | 3 min 28 s (208 s)\* | not measured | 2 min 06 s (126 s) | 1.65×\* |
| ProRes 4444 AVFoundation, chroma off | not measured | not measured | 3 min 02 s (182 s) | – |
| ProRes 4444 AVFoundation, chroma 5×5 | 7 min 13 s (433 s)† | 4 min 59 s (299 s)† | 3 min 47 s (227 s) | 1.91×† |
| ProRes 4444 FFmpeg (Kostya), chroma off | not measured | not measured | 4 min 17 s (257 s) | – |

\* The original app was timed once for CinemaDNG (208 s). The chroma setting of that run is not recorded, so the same time is shown on both rows. Re-time it to confirm.

† These two times were first reported as FFmpeg exports. Confirm they were AVFoundation exports.

Summary for clip A:

- CinemaDNG export is 84 s with chroma off and 126 s with chroma 5×5. Chroma 5×5 adds about 42 s (about 57 ms per frame).
- The CinemaDNG chroma-off time went from 125 s to 84 s between PR #334 and today. The chroma code does not run when chroma is off, so this gain comes from the other changes in today's build.
- On this clip with chroma off, FFmpeg ProRes (257 s) takes longer than AVFoundation ProRes (182 s). The output format of the two encoders has not been compared.

### Clip B: Canon EOS M 1080p standard RAW (`M04-1803.MLV`)

- Frame size: 1736×738, 614 frames, AMaZE

| Preset | Original | PR #334 | Today | Speed-up |
| :--- | :--- | :--- | :--- | :--- |
| ProRes 4444 (Force AMaZE) | 53.5 s (11.5 fps) | 36.5 s (16.8 fps) | 36.5 s (16.8 fps) | 1.47× |
| AVFoundation export | not measured | 21.7 s (28.3 fps) | 21.7 s (28.3 fps) | – |

Today's times are the same as PR #334's, so the gain comes from PR #334.

### Clip C: 5.4K RAW (`M16-1105.MLV`)

- 14-bit lossless, 606.9 MB, 7 seconds
- Settings on in every run: Force AMaZE, Fix Bad Pixels (Force, Norm), Fix Focus Pixels, Chroma Smooth 5×5

| Preset | Original | Previous Build | With Non-Dual-ISO Opts | Speed-up vs Original |
| :--- | :--- | :--- | :--- | :--- |
| ProRes 4444 AVFoundation | 49 s | 38 s | **~28–29 s** | **~1.7×** |
| ProRes 4444 FFmpeg (Kostya) | 85 s | 62 s | **~50–52 s** | **~1.65×** |
| CinemaDNG Uncompressed | 13 s | 12 s | **~10–11 s** | **~1.2×** |

*Note: Non-Dual-ISO Chroma Smooth 5×5 vectorization drops the per-frame 16-bit chroma pass from 2,140 ms down to 148 ms (14.4× speedup), and DNG bit-packing drops from 26.5 ms to 3.3 ms (8.1× speedup).*

---

## 2. Playback (timeline scrubbing)

Read from the app's fps counter on clip A. Values marked "~" are approximate. The "Today" values were taken before the final OpenMP build and need re-measuring.

| Mode | Original | PR #334 | Today | Time per frame today |
| :--- | :--- | :--- | :--- | :--- |
| AMaZE + chroma 5×5 | ~0.3 fps | ~3–4 fps | 5 fps | 200 ms |
| AMaZE + chroma off | ~2–3 fps | ~4–5 fps | 7 fps | 143 ms |

The counter counts redraws, not video frames. It also runs while a slider is moved on a still frame.

---

## 3. Per-frame timings (test harness)

Dual ISO frame computation from the test harness, without disk or encoder time. These are not export or playback times and should not be compared with them.

| Frame size and algorithm | Original | PR #334 | Today | Speed-up |
| :--- | :--- | :--- | :--- | :--- |
| 4096×2160, Dual ISO, chroma 5×5 | 3,956.3 ms | 3,850.0 ms | 394.6 ms | 10.0× |
| 2050×1038, Dual ISO, chroma 5×5 | 933.2 ms | 910.0 ms | 98.2 ms | 9.5× |
| 1472×1846, Dual ISO, chroma 5×5 | 1,316.8 ms | 1,280.0 ms | re-run needed | – |
| 1736×738, AMaZE only (no Dual ISO) | 83.1 ms | 49.9 ms | 49.9 ms | 1.67× |

- The 4K and 2.5K "Today" values come from the final OpenMP build. Most of that gain is from the chroma change (3,850 ms to 395 ms against PR #334).
- The 1472×1846 value measured earlier (291.4 ms) came from an earlier build, so it is not shown. By pixel count it should now be about 120 ms.
- The 1736×738 gain is from PR #334.

---

## 4. Output checks

**5×5 chroma change (Dual ISO)**

- 10 of 10 test cases give identical output to the reference code (FNV-1a 64-bit hash).
- A full 4096×2160 frame (17,694,720 bytes) compared with `cmp`: 0 bytes differ.

**Whole app against the original (clip C, ProRes 4444 exports, 5.4K, 7 s)**

| Encoder | PSNR vs original-app export |
| :--- | :--- |
| AVFoundation | 75.5 dB average (Y 74.7, U 74.0, V 74.1, alpha identical) |
| FFmpeg (Kostya) | 73.3 dB average (Y 73.3, U 73.1, V 73.6) |

- The exports are not bit-identical to the original app. A difference at this level is not visible.
- The cause is not confirmed. Small float rounding changes from the NEON AMaZE in PR #334 are the likely reason.

**Not done yet**

- Compare Dual ISO exports: today's build with and without `DISO_NO_FAST_CHROMA5`.
- Build and run on Intel Mac, Linux and Windows.
- Re-measure playback and the 1472×1846 frame with the final build.
- Time the histogram-matching bypass and print removal on their own.