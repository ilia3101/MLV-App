/*!
 * \file wb_conversion.h
 * \author masc4ii
 * \copyright 2019
 * \brief WB conversion do & undo for ideal debayer result
 */

#ifndef WB_CONVERSION_H
#define WB_CONVERSION_H

#include "stdlib.h"
#include "stdint.h"

typedef struct {
    float r, g, b;
} wb_convert_info_t;
// typedef struct {
//     float min_r, max_r;
//     float min_g, max_g;
//     float min_b, max_b;
// } wb_convert_info_t;

void wb_convert(wb_convert_info_t * wb_info, float *rawData, int width, int height, int blacklevel);
void wb_undo(const wb_convert_info_t * wb_info, uint16_t *debayeredFrame, int width, int height, int blacklevel);

#endif // WB_CONVERSION_H
