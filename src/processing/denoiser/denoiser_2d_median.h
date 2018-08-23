/*!
 * \file denoise_2D_median.h
 * \author masc4ii
 * \copyright 2018
 * \brief an very easy 2D median denoiser
 */

#ifndef DENOISER_2D_MEDIAN_H
#define DENOISER_2D_MEDIAN_H

#include "stdint.h"

void denoise_2D_median(uint16_t *data, int width, int height, uint8_t window, uint8_t strength);

#endif // DENOISER_2D_MEDIAN_H
