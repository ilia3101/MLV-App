#ifndef EMMINTRIN_H
#define EMMINTRIN_H

#ifdef __x86_64__
#include_next <emmintrin.h>
#else
#include "sse2neon.h"
#endif

#endif
