/* galosh_f16_rne.glsl — [V2.0 FP16 storage contract v1, dataflow_spec.md §4]
 * explicit IEEE binary16 round-to-nearest-even, integer-only.
 *
 * WHY THIS EXISTS: the contract is defined at binary16 RNE — the CPU
 * oracle (`--f16-storage`) rounds with (_Float16) casts (= RNE).  The
 * SPIR-V OpFConvert f32→f16 rounding mode is IMPLEMENTATION-DEFINED
 * unless decorated.  Measured 2026-07-10 (p3_L_cs dump forensics,
 * synth_1080p): NVIDIA + Intel Arc = RNE (99.88% of stores == RNE(oracle
 * f32)), AMD = ROUND-TOWARD-ZERO (99.94% of store errors toward zero;
 * RTZ-vs-RNE quantization MSE ratio 6× = the observed 7.8 dB parity gap,
 * 63.9 dB < the 65 dB gate).  Rounding HERE with integer ops makes the
 * value exactly f16-representable, so the hardware conversion that
 * follows is exact under ANY rounding mode → ONE vendor-agnostic shader
 * set, no per-GPU branching, no VK_KHR_shader_float_controls dependency.
 * (日) AMD の f32→f16 変換が RTZ だったため、丸めを整数演算で明示化。
 *   格納値が f16 で正確に表現可能になり、後続変換はどの丸めモードでも
 *   無損失 → 全ベンダーでオラクルと同一の丸め点・丸め方向。
 *
 * Cost: ~10 int ops per contract store (per-pixel writes only) — noise
 * against the kernels' compute (measured TOTALs unchanged).
 *
 * Usage: v_rounded = galosh_f16_rne(v);  then store float16_t(v_rounded)
 * (or keep the f32 value, e.g. the pass12 shared-memory pilot).
 */

/* f32 bits → f16 bits, IEEE 754 RNE.  Handles normal / subnormal /
 * underflow→±0 / overflow→Inf / Inf / NaN.  Integer-only → bit-identical
 * on every device. */
uint galosh_f32_to_f16_bits_rne(const uint x)
{
  const uint s  = (x >> 16) & 0x8000u;
  const uint ax = x & 0x7FFFFFFFu;
  if(ax > 0x7F800000u)  return s | 0x7E00u;   /* NaN → quiet NaN */
  if(ax >= 0x47800000u) return s | 0x7C00u;   /* ≥ 65536 or Inf → Inf */
  if(ax >= 0x38800000u)                       /* normal f16: |v| ≥ 2⁻¹⁴ */
  {
    /* rebias exponent 127→15 and pack; RNE on the 13 dropped bits.
     * A mantissa carry naturally bumps the exponent (65504↗Inf band OK). */
    const uint h   = (ax - 0x38000000u) >> 13;
    const uint rem = ax & 0x1FFFu;
    return s | (h + ((rem > 0x1000u || (rem == 0x1000u && (h & 1u) == 1u)) ? 1u : 0u));
  }
  if(ax >= 0x33000000u)                       /* subnormal f16 band: |v| ≥ 2⁻²⁵ */
  {
    const uint sh   = 126u - (ax >> 23);      /* 14..24 within this band */
    const uint m    = (ax & 0x007FFFFFu) | 0x00800000u;
    const uint h    = m >> sh;
    const uint rem  = m & ((1u << sh) - 1u);
    const uint mid  = 1u << (sh - 1u);        /* halfway point ("half" is reserved) */
    return s | (h + ((rem > mid || (rem == mid && (h & 1u) == 1u)) ? 1u : 0u));
  }
  return s;                                   /* |v| < 2⁻²⁵ → ±0 (RNE) */
}

/* f16 bits → f32, exact, integer-only (no unpackHalf2x16: a driver could
 * flush f16 subnormals there; this path cannot). */
float galosh_f16_bits_to_f32(const uint hb)
{
  const uint s = (hb & 0x8000u) << 16;
  const uint e = (hb >> 10) & 0x1Fu;
  const uint m = hb & 0x3FFu;
  if(e == 0u)
  {
    if(m == 0u) return uintBitsToFloat(s);    /* ±0 */
    /* f16 subnormal = m·2⁻²⁴ — normal in f32; int→float exact */
    return uintBitsToFloat(s | floatBitsToUint(float(m) * 5.9604644775390625e-08f));
  }
  if(e == 31u) return uintBitsToFloat(s | 0x7F800000u | (m << 13));  /* Inf/NaN */
  return uintBitsToFloat(s | ((e + 112u) << 23) | (m << 13));
}

/* Round v to binary16 (RNE), returned as the exact f32 value. */
float galosh_f16_rne(const float v)
{
  return galosh_f16_bits_to_f32(galosh_f32_to_f16_bits_rne(floatBitsToUint(v)));
}
