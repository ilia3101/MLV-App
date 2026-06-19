#ifdef __x86_64__
#include_next <xmmintrin.h>
#else
#include "../sse2neon/sse2neon.h"

#define _MM_ROUND_NEAREST    0x0000
#define _MM_ROUND_DOWN       0x2000
#define _MM_ROUND_UP         0x4000
#define _MM_ROUND_TOWARD_ZERO 0x6000
static inline unsigned int _MM_GET_ROUNDING_MODE(void) { return _MM_ROUND_NEAREST; }
static inline void _MM_SET_ROUNDING_MODE(unsigned int m) { (void)m; }

#define _mm_bslli_si128(a, imm) _mm_slli_si128(a, imm)
#define _mm_bsrli_si128(a, imm) _mm_srli_si128(a, imm)

#endif
