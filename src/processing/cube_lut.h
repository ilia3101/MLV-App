/*!
 * \file cube_lut.h
 * \author masc4ii
 * \copyright 2018
 * \brief this module allows loading 3d luts (.cube) and applies them on a picture
 */

#ifndef CUBE_LUT_H
#define CUBE_LUT_H

#include <stdint.h>

typedef struct {
    uint16_t dimension;
    float *cube;
    int is3d;
} lut_t;

lut_t * init_lut( void );
void free_lut( lut_t *lut );
int load_lut(lut_t *lut, char *filename);
void unload_lut( lut_t *lut );
void apply_lut( lut_t *lut, int width, int height, uint16_t * image );

#endif // CUBE_LUT_H
