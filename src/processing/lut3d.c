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
double lerp(double x, double x1, double x2, double q00, double q01) {
    if( ( x2 - x1 ) == 0 ) return q00;
    else return ( ( ( x2 - x ) / ( x2 - x1 ) ) * q00 ) + ( ( ( x - x1 ) / ( x2 - x1 ) ) * q01 );
}

double biLerp(double x, double y, double q11, double q12, double q21, double q22, double x1, double x2, double y1, double y2) {
  double r1 = lerp(x, x1, x2, q11, q21);
  double r2 = lerp(x, x1, x2, q12, q22);

  return lerp(y, y1, y2, r1, r2);
}

double triLerp(double x, double y, double z, double q000, double q001, double q010, double q011, double q100, double q101, double q110, double q111, double x1, double x2, double y1, double y2, double z1, double z2) {
  double x00 = lerp(x, x1, x2, q000, q100);
  double x10 = lerp(x, x1, x2, q010, q110);
  double x01 = lerp(x, x1, x2, q001, q101);
  double x11 = lerp(x, x1, x2, q011, q111);
  double r0 = lerp(y, y1, y2, x00, x10);
  double r1 = lerp(y, y1, y2, x01, x11);

  return lerp(z, z1, z2, r0, r1);
}
// //////////////

//Initialize 3d LUT object
lut3d_t * init_lut3d( void )
{
    lut3d_t *lut3d = malloc( sizeof( lut3d_t ) );
    lut3d->dimension = 0;
    return lut3d;
}

//Unload the whole lut object
void free_lut3d(lut3d_t *lut3d)
{
    if( lut3d )
    {
        unload_lut3d( lut3d );
        free( lut3d );
    }
}

//Load the 3dLUT
int load_lut3d( lut3d_t *lut3d, char *filename )
{
    FILE *fp;
    fp = fopen( filename, "r" );
    uint16_t dimension = 33;
    lut3d->dimension = dimension;
    lut3d->cube = malloc( (uint32_t)dimension * (uint32_t)dimension * (uint32_t)dimension * 3 * sizeof( double ) );

    for( uint32_t i = 0; i < dimension*dimension*dimension*3; i+=3 )
    {
        double r, g, b;
        int ret = fscanf(fp, "%lf %lf %lf\n", &r, &g, &b);
        if( ret == EOF ) //Error! File to short.
        {
            unload_lut3d( lut3d );
            fclose( fp );
            return -1;
        }
        if( ret != 3 ) //Error! Not 3 doubles found in line!
        {
            unload_lut3d( lut3d );
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
void unload_lut3d( lut3d_t *lut3d )
{
    if( !lut3d ) return;
    if( lut3d->dimension == 0 ) return;
    if( lut3d->cube ) free( lut3d->cube );
    lut3d->dimension = 0;
}

//Read out a point from the lut (input: 0..dimension-1)
double getLutPoint( lut3d_t *lut3d, uint16_t rIn, uint16_t gIn, uint16_t bIn, uint8_t outChannel )
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
void apply_lut3d(lut3d_t *lut3d, int width, int height, uint16_t *image)
{
    if( lut3d->dimension <= 1 || !lut3d->cube ) return;

    uint16_t * end = image + (width * height * 3);

    for (uint16_t * pix = image; pix < end; pix += 3)
    {
        //x
        double red = pix[0] * ( lut3d->dimension - 1 ) / 65536.0;
        double green = pix[1] * ( lut3d->dimension - 1 ) / 65536.0;
        double blue = pix[2] * ( lut3d->dimension - 1 ) / 65536.0;

        //x0
        uint16_t r0 = (uint16_t)red;
        uint16_t g0 = (uint16_t)green;
        uint16_t b0 = (uint16_t)blue;
        //x1
        uint16_t r1 = r0 + 1;
        uint16_t g1 = g0 + 1;
        uint16_t b1 = b0 + 1;

        if( 0 )
        {
            //Linear Interpolation
            //y0 & //y1
            double pix00 = getLutPoint( lut3d, r0, g0, b0, 0 );
            double pix01 = getLutPoint( lut3d, r1, g1, b1, 0 );
            double pix10 = getLutPoint( lut3d, r0, g0, b0, 1 );
            double pix11 = getLutPoint( lut3d, r1, g1, b1, 1 );
            double pix20 = getLutPoint( lut3d, r0, g0, b0, 2 );
            double pix21 = getLutPoint( lut3d, r1, g1, b1, 2 );
            //3x Linear Interpolation
            double a = lerp( red,   r0, r1, pix00, pix01 );
            double b = lerp( green, g0, g1, pix10, pix11 );
            double c = lerp( blue,  b0, b1, pix20, pix21 );
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
                double q000 = getLutPoint( lut3d, r0, g0, b0, i );
                double q001 = getLutPoint( lut3d, r0, g0, b1, i );
                double q010 = getLutPoint( lut3d, r0, g1, b0, i );
                double q011 = getLutPoint( lut3d, r0, g1, b1, i );
                double q100 = getLutPoint( lut3d, r1, g0, b0, i );
                double q101 = getLutPoint( lut3d, r1, g0, b1, i );
                double q110 = getLutPoint( lut3d, r1, g1, b0, i );
                double q111 = getLutPoint( lut3d, r1, g1, b1, i );

                double out = triLerp(red, green, blue, q000, q001, q010, q011, q100, q101, q110, q111, r0, r1, g0, g1, b0, b1);

                //Output
                pix[i] = LIMIT16( out * 65535.0 );
            }
        }
    }
}
