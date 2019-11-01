#include <string.h>
#include "ColorAberrationCorrection.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define LIMIT16(X) MAX(MIN(X, 65535), 0)

void rmCA(uint16_t* rVec, uint16_t* gVec, uint16_t* bVec, int width, int height, int threshold, int radius)
{
#pragma omp parallel for
    for (int i = 0; i < height; ++i)
	{
        uint16_t *bptr = &bVec[i*width];
        uint16_t *gptr = &gVec[i*width];
        uint16_t *rptr = &rVec[i*width];

        for (int j = 2; j < width - 2; ++j)
		{
			//find the edge by finding green channel gradient bigger than threshold
			if (abs(gptr[j + 1] - gptr[j - 1]) >= threshold)
			{
				// +/- sign of this edge
				int sign = 0;
				if (gptr[j + 1] - gptr[j - 1] > 0) { sign = 1; }
				else { sign = -1; }

				//Searching the boundary for correction range
				int lpos = j-1, rpos = j+1;
                for (; lpos > 1; --lpos)
				{
					//make sure the gradient is the same sign with edge
					int ggrad = (gptr[lpos + 1] - gptr[lpos - 1])*sign;
					int bgrad = (bptr[lpos + 1] - bptr[lpos - 1])*sign;
					int rgrad = (rptr[lpos + 1] - rptr[lpos - 1])*sign;
                    if ( j-lpos >= radius ) { break; }
                    if (MAX(MAX(bgrad, ggrad), rgrad) < threshold) { break; }
                }
				lpos -= 1;
                for (; rpos < width - 2; ++rpos)
				{
					//make sure the gradient is the same sign with edge
					int ggrad = (gptr[rpos + 1] - gptr[rpos - 1])*sign;
					int bgrad = (bptr[rpos + 1] - bptr[rpos - 1])*sign;
					int rgrad = (rptr[rpos + 1] - rptr[rpos - 1])*sign;
                    if ( rpos-j >= radius ) { break; }
                    if (MAX(MAX(bgrad, ggrad), rgrad) < threshold) { break; }
                }
				rpos += 1;

				//record the maximum and minimum color difference between R&G and B&G of range boundary
                int bgmaxVal = MAX(bptr[lpos] - gptr[lpos], bptr[rpos] - gptr[rpos]);
                int bgminVal = MIN(bptr[lpos] - gptr[lpos], bptr[rpos] - gptr[rpos]);
                int rgmaxVal = MAX(rptr[lpos] - gptr[lpos], rptr[rpos] - gptr[rpos]);
                int rgminVal = MIN(rptr[lpos] - gptr[lpos], rptr[rpos] - gptr[rpos]);

				for (int k = lpos; k <= rpos; ++k)
				{
					int bdiff = bptr[k] - gptr[k];
					int rdiff = rptr[k] - gptr[k];

					//Replace the B or R value if its color difference of R/G and B/G is bigger(smaller)
					//than maximum(minimum) of color difference on range boundary
                    bptr[k] = LIMIT16( bdiff > bgmaxVal ? bgmaxVal + gptr[k] :
						(bdiff < bgminVal ? bgminVal + gptr[k] : bptr[k]) );
                    rptr[k] = LIMIT16( rdiff > rgmaxVal ? rgmaxVal + gptr[k] :
						(rdiff < rgminVal ? rgminVal + gptr[k] : rptr[k]) );			
				}
				j = rpos - 2;
			}
		}
	}
}

/* Filter CAs and ColorMoiree in RGB picture data */
void CACorrection(int imageX, int imageY,
                  uint16_t * __restrict inputImage,
                  uint16_t * __restrict outputImage,
                  uint16_t threshold, uint8_t radius)
{
    //getting working memory
    uint16_t *bVec = malloc( imageX * imageY * sizeof( uint16_t ) );
    uint16_t *gVec = malloc( imageX * imageY * sizeof( uint16_t ) );
    uint16_t *rVec = malloc( imageX * imageY * sizeof( uint16_t ) );
    uint16_t *temp = malloc( imageX * imageY * sizeof( uint16_t ) );

	//split the color image into individual color channel for convenient in calculation
#pragma omp parallel for
    for( int i = 0; i < imageX*imageY; i++ )
    {
        int j = i * 3;
        rVec[i] = inputImage[j+0];
        gVec[i] = inputImage[j+1];
        bVec[i] = inputImage[j+2];
    }

	//setting threshold to find the edge and correction range(in g channel)
    //threshold = 7700;//30;
    if( threshold < 1 ) threshold = 1;

    //first run
    rmCA(rVec, gVec, bVec, imageX, imageY, threshold, radius);

	//transpose the R,G B channel image to correct chromatic aberration in vertical direction 
    memcpy( temp, rVec, imageX*imageY * sizeof(uint16_t) );
#pragma omp parallel for
    for( int y = 0; y < imageY; y++ )
        for( int x = 0; x < imageX; x++ )
            rVec[x*imageY+y] = temp[y*imageX+x];
    memcpy( temp, gVec, imageX*imageY * sizeof(uint16_t) );
#pragma omp parallel for
    for( int y = 0; y < imageY; y++ )
        for( int x = 0; x < imageX; x++ )
            gVec[x*imageY+y] = temp[y*imageX+x];
    memcpy( temp, bVec, imageX*imageY * sizeof(uint16_t) );
#pragma omp parallel for
    for( int y = 0; y < imageY; y++ )
        for( int x = 0; x < imageX; x++ )
            bVec[x*imageY+y] = temp[y*imageX+x];

    //second run
    rmCA(rVec, gVec, bVec, imageY, imageX, threshold, radius);

    //rotate the image back to original position
    memcpy( temp, rVec, imageX*imageY * sizeof(uint16_t) );
#pragma omp parallel for
    for( int y = 0; y < imageY; y++ )
        for( int x = 0; x < imageX; x++ )
            rVec[y*imageX+x] = temp[x*imageY+y];
    memcpy( temp, gVec, imageX*imageY * sizeof(uint16_t) );
#pragma omp parallel for
    for( int y = 0; y < imageY; y++ )
        for( int x = 0; x < imageX; x++ )
            gVec[y*imageX+x] = temp[x*imageY+y];
    memcpy( temp, bVec, imageX*imageY * sizeof(uint16_t) );
#pragma omp parallel for
    for( int y = 0; y < imageY; y++ )
        for( int x = 0; x < imageX; x++ )
            bVec[y*imageX+x] = temp[x*imageY+y];

    //merge channels into final image
#pragma omp parallel for
    for( int i = 0; i < imageX*imageY; i++ )
    {
        int j = i * 3;
        outputImage[j+0] = rVec[i];
        outputImage[j+1] = gVec[i];
        outputImage[j+2] = bVec[i];
    }

    //clean up
    free( bVec );
    free( gVec );
    free( rVec );
    free( temp );
}
