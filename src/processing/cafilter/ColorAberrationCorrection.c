#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

#include <string.h>
#include <stdlib.h>
#include "ColorAberrationCorrection.h"

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CA_TLS __thread
#elif defined(_MSC_VER)
#define CA_TLS __declspec(thread)
#elif __STDC_VERSION__ >= 201112L
#define CA_TLS _Thread_local
#else
#define CA_TLS
#endif

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define LIMIT16(X) MAX(MIN(X, 65535), 0)

static CA_TLS uint16_t *ca_buf = NULL;
static CA_TLS size_t ca_buf_size = 0;

void freeCACorrection(void)
{
    if (ca_buf)
    {
        free(ca_buf);
        ca_buf = NULL;
        ca_buf_size = 0;
    }
}

void rmCA(uint16_t* rVec, uint16_t* gVec, uint16_t* bVec, int width, int height, int threshold, int radius)
{
#ifdef __APPLE__
    size_t nworkers = (height < 8) ? (size_t)height : 8;
    if (nworkers < 1) nworkers = 1;
    size_t chunk = ((size_t)height + nworkers - 1) / nworkers;
    dispatch_apply(nworkers, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t t) {
        int i_start = (int)(t * chunk);
        int i_end = (int)(i_start + chunk);
        if (i_end > height) i_end = height;
        for (int i = i_start; i < i_end; ++i)
#else
#pragma omp parallel for schedule(static)
    for (int i = 0; i < height; ++i)
#endif
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
#ifdef __APPLE__
    });
#endif
}

static void transpose_plane(const uint16_t *src, uint16_t *dst, int width, int height)
{
    const int TILE = 32;
#ifdef __APPLE__
    size_t nworkers = (height < 8) ? (size_t)height : 8;
    if (nworkers < 1) nworkers = 1;
    size_t chunk = ((size_t)height + nworkers - 1) / nworkers;
    dispatch_apply(nworkers, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t t) {
        int y_start = (int)(t * chunk);
        int y_end = (int)(y_start + chunk);
        if (y_end > height) y_end = height;
        for (int y0 = y_start; y0 < y_end; y0 += TILE)
        {
            int y_max = (y0 + TILE < y_end) ? (y0 + TILE) : y_end;
            for (int x0 = 0; x0 < width; x0 += TILE)
            {
                int x_max = (x0 + TILE < width) ? (x0 + TILE) : width;
                for (int y = y0; y < y_max; ++y)
                {
                    for (int x = x0; x < x_max; ++x)
                    {
                        dst[x * height + y] = src[y * width + x];
                    }
                }
            }
        }
    });
#else
#pragma omp parallel for schedule(static)
    for (int y0 = 0; y0 < height; y0 += TILE)
    {
        int y_max = (y0 + TILE < height) ? (y0 + TILE) : height;
        for (int x0 = 0; x0 < width; x0 += TILE)
        {
            int x_max = (x0 + TILE < width) ? (x0 + TILE) : width;
            for (int y = y0; y < y_max; ++y)
            {
                for (int x = x0; x < x_max; ++x)
                {
                    dst[x * height + y] = src[y * width + x];
                }
            }
        }
    }
#endif
}

/* Filter CAs and ColorMoiree in RGB picture data */
void CACorrection(int imageX, int imageY,
                  uint16_t * __restrict inputImage,
                  uint16_t * __restrict outputImage,
                  uint16_t threshold, uint8_t radius)
{
    size_t npix = (size_t)imageX * (size_t)imageY;
    size_t needed = npix * 6;
    if (ca_buf_size < needed)
    {
        if (ca_buf) free(ca_buf);
        ca_buf = (uint16_t *)malloc(needed * sizeof(uint16_t));
        ca_buf_size = needed;
    }

    uint16_t *r1 = ca_buf;
    uint16_t *g1 = r1 + npix;
    uint16_t *b1 = g1 + npix;
    uint16_t *r2 = b1 + npix;
    uint16_t *g2 = r2 + npix;
    uint16_t *b2 = g2 + npix;

    // Split RGB
#ifdef __APPLE__
    size_t nworkers = (npix < 8) ? npix : 8;
    if (nworkers < 1) nworkers = 1;
    size_t chunk = (npix + nworkers - 1) / nworkers;
    dispatch_apply(nworkers, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t t) {
        size_t start = t * chunk;
        size_t end = start + chunk;
        if (end > npix) end = npix;
        for (size_t i = start; i < end; ++i)
        {
            size_t j = i * 3;
            r1[i] = inputImage[j + 0];
            g1[i] = inputImage[j + 1];
            b1[i] = inputImage[j + 2];
        }
    });
#else
#pragma omp parallel for schedule(static)
    for (size_t i = 0; i < npix; ++i)
    {
        size_t j = i * 3;
        r1[i] = inputImage[j + 0];
        g1[i] = inputImage[j + 1];
        b1[i] = inputImage[j + 2];
    }
#endif

    if( threshold < 1 ) threshold = 1;

    // Horizontal pass
    rmCA(r1, g1, b1, imageX, imageY, threshold, radius);

    // Direct tiled transpose into r2, g2, b2
    transpose_plane(r1, r2, imageX, imageY);
    transpose_plane(g1, g2, imageX, imageY);
    transpose_plane(b1, b2, imageX, imageY);

    // Vertical pass
    rmCA(r2, g2, b2, imageY, imageX, threshold, radius);

    // Direct tiled transpose back into r1, g1, b1
    transpose_plane(r2, r1, imageY, imageX);
    transpose_plane(g2, g1, imageY, imageX);
    transpose_plane(b2, b1, imageY, imageX);

    //Merge into outputImage
#ifdef __APPLE__
    dispatch_apply(nworkers, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t t) {
        size_t start = t * chunk;
        size_t end = start + chunk;
        if (end > npix) end = npix;
        for (size_t i = start; i < end; ++i)
        {
            size_t j = i * 3;
            outputImage[j + 0] = r1[i];
            outputImage[j + 1] = g1[i];
            outputImage[j + 2] = b1[i];
        }
    });
#else
#pragma omp parallel for schedule(static)
    for (size_t i = 0; i < npix; ++i)
    {
        size_t j = i * 3;
        outputImage[j + 0] = r1[i];
        outputImage[j + 1] = g1[i];
        outputImage[j + 2] = b1[i];
    }
#endif
}
