/**
 * @file basic.h
 * @brief Memory management, portable types, math constants, and timing
 * @author Pascal Getreuer <getreuer@gmail.com>
 * 
 * This purpose of this file is to improve portability.
 * 
 * Types \c uint8_t, \c uint16_t, \c uint32_t should be defined as
 * unsigned integer types such that
 * @li \c uint8_t  is 8-bit,  range 0 to 255
 * @li \c uint16_t is 16-bit, range 0 to 65535
 * @li \c uint32_t is 32-bit, range 0 to 4294967295
 *
 * Similarly, \c int8_t, \c int16_t, \c int32_t should be defined as
 * signed integer types such that
 * @li \c int8_t  is  8-bit, range        -128 to +127
 * @li \c int16_t is 16-bit, range      -32768 to +32767
 * @li \c int32_t is 32-bit, range -2147483648 to +2147483647
 *
 * These definitions are implemented with types \c __int8, \c __int16,
 * and \c __int32 under Windows and by including stdint.h under UNIX.
 * 
 * To define the math constants, math.h is included, and any of the 
 * following that were not defined by math.h are defined here according
 * to the values from Hart & Cheney.
 * @li M_2PI     = 2 pi      = 6.28318530717958647692528676655900576
 * @li M_PI      = pi        = 3.14159265358979323846264338327950288
 * @li M_PI_2    = pi/2      = 1.57079632679489661923132169163975144
 * @li M_PI_4    = pi/4      = 0.78539816339744830961566084581987572
 * @li M_PI_8    = pi/8      = 0.39269908169872415480783042290993786
 * @li M_SQRT2   = sqrt(2)   = 1.41421356237309504880168872420969808
 * @li M_1_SQRT2 = 1/sqrt(2) = 0.70710678118654752440084436210484904
 * @li M_E       = e         = 2.71828182845904523536028747135266250
 * @li M_LOG2E   = log_2(e)  = 1.44269504088896340735992468100189213
 * @li M_LOG10E  = log_10(e) = 0.43429448190325182765112891891660508
 * @li M_LN2     = log_e(2)  = 0.69314718055994530941723212145817657
 * @li M_LN10    = log_e(10) = 2.30258509299404568401799145468436421
 * @li M_EULER   = Euler     = 0.57721566490153286060651209008240243
 * 
 * 
 * Copyright (c) 2010-2011, Pascal Getreuer
 * All rights reserved.
 * 
 * This program is free software: you can use, modify and/or 
 * redistribute it under the terms of the simplified BSD License. You 
 * should have received a copy of this license along this program. If 
 * not, see <http://www.opensource.org/licenses/bsd-license.html>.
 */

#ifndef _BASIC_H_
#define _BASIC_H_

#include <math.h>
#include <stdio.h>
#include <stdlib.h>


/* Memory management */
/** @brief Function to allocate a block of memory */
#define Malloc(s)               MallocWithErrorMessage(s)
void *MallocWithErrorMessage(size_t Size);
/** @brief Function to reallocate a block of memory */
#define Realloc(p, s)           ReallocWithErrorMessage(p, s)
void *ReallocWithErrorMessage(void *Ptr, size_t Size);
/** @brief Function to free memory */
#define Free(p)                 free(p)


/* Portable integer types */
#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)

    /* Windows system: Use __intN types to define uint8_t, etc. */
    typedef unsigned __int8 uint8_t;
    typedef unsigned __int16 uint16_t;
    typedef unsigned __int32 uint32_t;
    typedef __int8 int8_t;
    typedef __int16 int16_t;
    typedef __int32 int32_t;
    
#else

    /* UNIX system: Use stdint to define uint8_t, etc. */
    #include <stdint.h>

#endif


/* Math constants (Hart & Cheney) */
#ifndef M_2PI
/** @brief The constant 2 pi */
#define M_2PI       6.28318530717958647692528676655900576
#endif
#ifndef M_PI
/** @brief The constant pi */
#define M_PI        3.14159265358979323846264338327950288
#endif
#ifndef M_PI_2
/** @brief The constant pi/2 */
#define M_PI_2      1.57079632679489661923132169163975144
#endif
#ifndef M_PI_4
/** @brief The constant pi/4 */
#define M_PI_4      0.78539816339744830961566084581987572
#endif
#ifndef M_PI_8
/** @brief The constant pi/8 */
#define M_PI_8      0.39269908169872415480783042290993786
#endif
#ifndef M_SQRT2
/** @brief The constant sqrt(2) */
#define M_SQRT2     1.41421356237309504880168872420969808
#endif
#ifndef M_1_SQRT2
/** @brief The constant 1/sqrt(2) */
#define M_1_SQRT2   0.70710678118654752440084436210484904
#endif
#ifndef M_E
/** @brief The natural number */
#define M_E         2.71828182845904523536028747135266250
#endif
#ifndef M_LOG2E
/** @brief Log base 2 of the natural number */
#define M_LOG2E     1.44269504088896340735992468100189213
#endif
#ifndef M_LOG10E
/** @brief Log base 10 of the natural number */
#define M_LOG10E    0.43429448190325182765112891891660508
#endif
#ifndef M_LN2
/** @brief Natural log of 2  */
#define M_LN2       0.69314718055994530941723212145817657
#endif
#ifndef M_LN10
/** @brief Natural log of 10 */
#define M_LN10      2.30258509299404568401799145468436421
#endif
#ifndef M_EULER
/** @brief Euler number */
#define M_EULER     0.57721566490153286060651209008240243
#endif

/** @brief Round double X */
#define ROUND(X) (floor((X) + 0.5))

/** @brief Round float X */
#define ROUNDF(X) (floor((X) + 0.5f))


#ifdef __GNUC__
    #ifndef ATTRIBUTE_UNUSED
    /** @brief Macro for the unused attribue GNU extension */
    #define ATTRIBUTE_UNUSED __attribute__((unused))
    #endif
    #ifndef ATTRIBUTE_ALWAYSINLINE
    /** @brief Macro for the always inline attribue GNU extension */
    #define ATTRIBUTE_ALWAYSINLINE __attribute__((always_inline))
    #endif
#else
    #define ATTRIBUTE_UNUSED
    #define ATTRIBUTE_ALWAYSINLINE
#endif


/* Error messaging */
void ErrorMessage(const char *Format, ...);

/* Timer function */
unsigned long Clock();

#endif /* _BASIC_H_ */
