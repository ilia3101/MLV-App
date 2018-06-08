/*!
 * \file lut3d.c
 * \author masc4ii
 * \copyright 2018
 * \brief this module allows loading 3d luts (.cube) and applies them on a picture
 */

#include "lut3d.h"
#include <stdlib.h>
#include <stdio.h>

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define LIMIT16(X) MAX(MIN(X, 65535), 0)

//Load the 3dLUT
void load_lut3d( lut3d_t *lut3d, char *filename )
{
    FILE *fp;
    fp = fopen( filename, "r" );
    uint16_t dimension = 33;
    lut3d->dimension = 33;
    lut3d->cube = malloc( (uint32_t)dimension * (uint32_t)dimension * (uint32_t)dimension * 3 * sizeof( double ) );

    for( uint32_t i = 0; i < dimension*dimension*dimension*3; i+=3 )
    {
        double r, g, b;
        int ret = fscanf(fp, "%lf %lf %lf\n", &r, &g, &b);
        if( ret == EOF ) break; //Error! File to short.
        if( ret != 3 ) continue; //Error! Not 3 floats found in line!
        lut3d->cube[i+0] = r;
        lut3d->cube[i+1] = g;
        lut3d->cube[i+2] = b;
    }

    fclose( fp );
}

//Unload the LUT
void unload_lut3d( lut3d_t *lut3d )
{
    if( lut3d->cube ) free( lut3d->cube );
    lut3d->dimension = 0;
}

//Apply LUT on picture
void apply_lut3d(lut3d_t *lut3d, int width, int height, uint16_t *image)
{
    if( !lut3d->dimension || !lut3d->cube ) return;

    uint16_t * end = image + (width * height * 3);

    for (uint16_t * pix = image; pix < end; pix += 3)
    {
        //x
        double red = pix[0] * lut3d->dimension / 65536.0;
        double green = pix[1] * lut3d->dimension / 65536.0;
        double blue = pix[2] * lut3d->dimension / 65536.0;
        //x0
        uint16_t red0 = (uint16_t)red;
        uint16_t green0 = (uint16_t)green;
        uint16_t blue0 = (uint16_t)blue;

        if( red0 >= lut3d->dimension ) red0 = lut3d->dimension - 1;
        if( green0 >= lut3d->dimension ) green0 = lut3d->dimension - 1;
        if( blue0 >= lut3d->dimension ) blue0 = lut3d->dimension - 1;
        uint32_t red0_offs = red0 * lut3d->dimension * lut3d->dimension;
        uint32_t green0_offs = green0 * lut3d->dimension;
        uint32_t blue0_offs = blue0;
        uint32_t offset0 = ( red0_offs + green0_offs + blue0_offs ) * 3;

        //x1
        uint32_t red1 = red0 + 1;
        uint32_t green1 = green0 + 1;
        uint32_t blue1 = blue0 + 1;
        if( red1 >= lut3d->dimension ) red1 = lut3d->dimension - 1;
        if( green1 >= lut3d->dimension ) green1 = lut3d->dimension - 1;
        if( blue1 >= lut3d->dimension ) blue1 = lut3d->dimension - 1;

        uint32_t red1_offs = red1 * lut3d->dimension * lut3d->dimension;
        uint32_t green1_offs = green1 * lut3d->dimension;
        uint32_t blue1_offs = blue1;
        uint32_t offset1 = ( red1_offs + green1_offs + blue1_offs ) * 3;

        //y0
        double pix00 = lut3d->cube[ offset0 + 2 ];
        double pix01 = lut3d->cube[ offset1 + 2 ];
        double pix10 = lut3d->cube[ offset0 + 1 ];
        //y1
        double pix11 = lut3d->cube[ offset1 + 1 ];
        double pix20 = lut3d->cube[ offset0 + 0 ];
        double pix21 = lut3d->cube[ offset1 + 0 ];

        //Interpolation
        double a = pix00 + ( ( pix01 - pix00 ) / ( red1 - red0 ) * ( red - red0 ) );
        double b = pix10 + ( ( pix11 - pix10 ) / ( green1 - green0 ) * ( green - green0 ) );
        double c = pix20 + ( ( pix21 - pix20 ) / ( blue1 - blue0 ) * ( blue - blue0 ) );

        //Output
        pix[0] = LIMIT16( a * 65535.0 );
        pix[1] = LIMIT16( b * 65535.0 );
        pix[2] = LIMIT16( c * 65535.0 );
    }
}
