#include "easy_denoise.h"
#include "math.h"

//Easy denoiser, stolen from MLV-Producer :o)
void easy_denoise( uint16_t *image, int width, int height, int strength )
{
    if( strength > 10 ) strength = 10;
    if( strength < 0 ) strength = 0;

    int W3 = width * 3;
    int k, l;
    int ln = width * height * 3;

    int v1, v2;

    for( k = W3; k < ln-W3; k++ )
    {
        if( strength == 0 ) break;
        v1 = image[k];
        v2 = (image[k - 3] + image[k + 3] + image[k - W3] + image[k + W3] ) >> 2;
        l = abs( v2 - v1 );
        if( l < (1<<(strength+6)) )
        {
            image[k] = (v1 * l + v2 * ((1<<(strength+6)) - l)) / (1<<(strength+6));
        }
    }
}
