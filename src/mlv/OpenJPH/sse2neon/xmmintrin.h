#ifndef XMMINTRIN_H
#define XMMINTRIN_H

#ifdef __x86_64__
#include_next <xmmintrin.h>
#else
#include "sse2neon.h"
#endif

#endif
