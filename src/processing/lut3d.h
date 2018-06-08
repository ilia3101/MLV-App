/*!
 * \file lut3d.h
 * \author masc4ii
 * \copyright 2018
 * \brief this module allows loading 3d luts (.cube) and applies them on a picture
 */

#ifndef LUT_H
#define LUT_H

#include <stdint.h>

typedef struct {
    uint16_t dimension;
    double *cube;
} lut3d_t;

lut3d_t * init_lut3d( void );
void free_lut3d( lut3d_t *lut3d );
int load_lut3d(lut3d_t *lut3d, char *filename);
void unload_lut3d( lut3d_t *lut3d );
void apply_lut3d( lut3d_t *lut3d, int width, int height, uint16_t * image );

#endif // LUT_H
