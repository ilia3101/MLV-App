/*!
 * \file blur_threaded.h
 * \author masc4ii & ilia3101
 * \copyright 2018
 * \brief a blur using threads
 */

#ifndef _blur_threaded_
#define _blur_threaded_

#include <stdint.h>

void blur_image_threaded( uint16_t * __restrict in,
                 uint16_t * __restrict temp,
                 int width, int height, int radius,
                 uint8_t threads );

#endif //_blur_threaded_
