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

void wb_convert(float *rawData, int width, int height, int blacklevel );
void wb_undo( uint16_t *debayeredFrame, int width, int height, int blacklevel );

#endif // WB_CONVERSION_H
