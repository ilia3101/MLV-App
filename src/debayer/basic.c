/** 
 * @file basic.c
 * @brief Memory management, portable types, math constants, and timing
 * @author Pascal Getreuer <getreuer@gmail.com>
 * 
 * This file implements a function Clock, a timer with millisecond
 * precision.  In order to obtain timing at high resolution, platform-
 * specific functions are needed:
 * 
 *    - On Windows systems, the GetSystemTime function is used.
 *    - On UNIX systems, the gettimeofday function is used.
 * 
 * This file attempts to detect whether the platform is Windows or UNIX
 * and defines Clock accordingly. 
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

#include <stdlib.h>
#include <stdarg.h>
#include "basic.h"


/** @brief malloc with an error message on failure. */
void *MallocWithErrorMessage(size_t Size)
{
    void *Ptr;
    
    if(!(Ptr = malloc(Size)))
        ErrorMessage("Memory allocation of %u bytes failed.\n", Size);
        
    return Ptr;
}


/** @brief realloc with an error message and free on failure. */
void *ReallocWithErrorMessage(void *Ptr, size_t Size)
{
    void *NewPtr;
    
    if(!(NewPtr = realloc(Ptr, Size)))
    {
        ErrorMessage("Memory reallocation of %u bytes failed.\n", Size);
        Free(Ptr);  /* Free the previous block on failure */
    }
        
    return NewPtr;
}


/** @brief Redefine this function to customize error messages. */
void ErrorMessage(const char *Format, ...)
{
    va_list Args;
    
    va_start(Args, Format);
    /* Write a formatted error message to stderr */
    vfprintf(stderr, Format, Args);
    va_end(Args);
}


#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
/* Windows: implement with GetSystemTime */
	
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Clock:  Get the system clock in milliseconds */
unsigned long Clock()
{
	static SYSTEMTIME TimeVal;
	GetSystemTime(&TimeVal);
	return (unsigned long)((unsigned long)TimeVal.wMilliseconds
		+ 1000*((unsigned long)TimeVal.wSecond
		+ 60*((unsigned long)TimeVal.wMinute 
		+ 60*((unsigned long)TimeVal.wHour 
		+ 24*(unsigned long)TimeVal.wDay))));
}

#else
/* UNIX: implement with gettimeofday */

#include <sys/time.h>
#include <unistd.h>

/* Clock:  Get the system clock in milliseconds */
unsigned long Clock()
{
	struct timeval TimeVal;
	gettimeofday(&TimeVal, NULL); 
	return (unsigned long)(TimeVal.tv_usec/1000 + TimeVal.tv_sec*1000);
}

#endif
