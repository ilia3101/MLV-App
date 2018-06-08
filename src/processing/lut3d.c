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

// //////////////
//Interpolation functions
float lerp(float x, float x1, float x2, float q00, float q01) {
    if( ( x2 - x1 ) == 0 ) return q00;
    else return ( ( ( x2 - x ) / ( x2 - x1 ) ) * q00 ) + ( ( ( x - x1 ) / ( x2 - x1 ) ) * q01 );
}

float biLerp(float x, float y, float q11, float q12, float q21, float q22, float x1, float x2, float y1, float y2) {
  float r1 = lerp(x, x1, x2, q11, q21);
  float r2 = lerp(x, x1, x2, q12, q22);

  return lerp(y, y1, y2, r1, r2);
}

float triLerp(float x, float y, float z, float q000, float q001, float q010, float q011, float q100, float q101, float q110, float q111, float x1, float x2, float y1, float y2, float z1, float z2) {
  float x00 = lerp(x, x1, x2, q000, q100);
  float x10 = lerp(x, x1, x2, q010, q110);
  float x01 = lerp(x, x1, x2, q001, q101);
  float x11 = lerp(x, x1, x2, q011, q111);
  float r0 = lerp(y, y1, y2, x00, x10);
  float r1 = lerp(y, y1, y2, x01, x11);

  return lerp(z, z1, z2, r0, r1);
}
// //////////////

//Initialize LUT object
lut_t * init_lut( void )
{
    lut_t *lut3d = malloc( sizeof( lut_t ) );
    lut3d->dimension = 0;
    return lut3d;
}

//Unload the whole lut object
void free_lut(lut_t *lut3d)
{
    if( lut3d )
    {
        unload_lut( lut3d );
        free( lut3d );
    }
}

//Load the LUT
int load_lut( lut_t *lut3d, char *filename )
{
    FILE *fp;
    fp = fopen( filename, "r" );
    uint16_t dimension = 33;
    lut3d->dimension = dimension;
    lut3d->is3d = 1;
    uint32_t size;
    if( !lut3d->is3d )
    {
        size = dimension * 3;
    }
    else
    {
        size = (uint32_t)dimension * (uint32_t)dimension * (uint32_t)dimension * 3;
    }

    lut3d->cube = malloc( size * sizeof( float ) );
    for( uint32_t i = 0; i < size; i+=3 )
    {
        float r, g, b;
        int ret = fscanf(fp, "%f %f %f\n", &r, &g, &b);
        if( ret == EOF ) //Error! File to short.
        {
            unload_lut( lut3d );
            fclose( fp );
            return -1;
        }
        if( ret != 3 ) //Error! Not 3 floats found in line!
        {
            unload_lut( lut3d );
            fclose( fp );
            return -2;
        }
        lut3d->cube[i+0] = r;
        lut3d->cube[i+1] = g;
        lut3d->cube[i+2] = b;
    }

    fclose( fp );
    return 0;
}

//Unload the LUT
void unload_lut( lut_t *lut3d )
{
    if( !lut3d ) return;
    if( lut3d->dimension == 0 ) return;
    if( lut3d->cube ) free( lut3d->cube );
    lut3d->dimension = 0;
}

//Read out a point from the 1dlut (input: 0..dimension-1)
float getLut1dPoint( lut_t *lut3d, uint16_t inPos, uint8_t outChannel )
{
    if( inPos >= lut3d->dimension ) inPos = lut3d->dimension - 1;
    return lut3d->cube[ inPos * 3 + outChannel ];
}

//Read out a point from the 3dlut (input: 0..dimension-1)
float getLut3dPoint( lut_t *lut3d, uint16_t rIn, uint16_t gIn, uint16_t bIn, uint8_t outChannel )
{
    if( rIn >= lut3d->dimension ) rIn = lut3d->dimension - 1;
    if( gIn >= lut3d->dimension ) gIn = lut3d->dimension - 1;
    if( bIn >= lut3d->dimension ) bIn = lut3d->dimension - 1;
    uint32_t red0_offs = rIn;
    uint32_t green0_offs = gIn * lut3d->dimension;
    uint32_t blue0_offs = bIn * lut3d->dimension * lut3d->dimension;
    uint32_t offset0 = ( red0_offs + green0_offs + blue0_offs ) * 3;

    return lut3d->cube[ offset0 + outChannel ];
}

//Apply LUT on picture
void apply_lut(lut_t *lut3d, int width, int height, uint16_t *image)
{
    if( lut3d->dimension <= 1 || !lut3d->cube ) return;

    uint16_t * end = image + (width * height * 3);

    for (uint16_t * pix = image; pix < end; pix += 3)
    {
        //x
        float red = pix[0] * ( lut3d->dimension - 1 ) / 65536.0;
        float green = pix[1] * ( lut3d->dimension - 1 ) / 65536.0;
        float blue = pix[2] * ( lut3d->dimension - 1 ) / 65536.0;

        //x0
        uint16_t r0 = (uint16_t)red;
        uint16_t g0 = (uint16_t)green;
        uint16_t b0 = (uint16_t)blue;
        //x1
        uint16_t r1 = r0 + 1;
        uint16_t g1 = g0 + 1;
        uint16_t b1 = b0 + 1;

        if( lut3d->is3d == 0 )
        {
            //Linear Interpolation
            //y0 & //y1
            float pix00 = getLut1dPoint( lut3d, r0, 0 );
            float pix01 = getLut1dPoint( lut3d, r1, 0 );
            float pix10 = getLut1dPoint( lut3d, g0, 1 );
            float pix11 = getLut1dPoint( lut3d, g1, 1 );
            float pix20 = getLut1dPoint( lut3d, b0, 2 );
            float pix21 = getLut1dPoint( lut3d, b1, 2 );
            //3x Linear Interpolation
            float a = lerp( red,   r0, r1, pix00, pix01 );
            float b = lerp( green, g0, g1, pix10, pix11 );
            float c = lerp( blue,  b0, b1, pix20, pix21 );
            //Output
            pix[0] = LIMIT16( a * 65535.0 );
            pix[1] = LIMIT16( b * 65535.0 );
            pix[2] = LIMIT16( c * 65535.0 );
        }
        else
        {
            //Trilinear Interpolation
            for( int i = 0; i < 3; i++ )
            {
                float q000 = getLut3dPoint( lut3d, r0, g0, b0, i );
                float q001 = getLut3dPoint( lut3d, r0, g0, b1, i );
                float q010 = getLut3dPoint( lut3d, r0, g1, b0, i );
                float q011 = getLut3dPoint( lut3d, r0, g1, b1, i );
                float q100 = getLut3dPoint( lut3d, r1, g0, b0, i );
                float q101 = getLut3dPoint( lut3d, r1, g0, b1, i );
                float q110 = getLut3dPoint( lut3d, r1, g1, b0, i );
                float q111 = getLut3dPoint( lut3d, r1, g1, b1, i );

                float out = triLerp(red, green, blue, q000, q001, q010, q011, q100, q101, q110, q111, r0, r1, g0, g1, b0, b1);

                //Output
                pix[i] = LIMIT16( out * 65535.0 );
            }
        }
    }
}
