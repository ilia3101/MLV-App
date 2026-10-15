#ifndef X86INTRIN_H
#define X86INTRIN_H

#ifdef __x86_64__
#include_next <x86intrin.h>
#else
#include "immintrin.h"
#endif

#endif
