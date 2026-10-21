#ifndef IMMINTRIN_H
#define IMMINTRIN_H

#ifdef __x86_64__
#include_next <immintrin.h>
#else
#include "sse2neon.h"
#endif

#endif
