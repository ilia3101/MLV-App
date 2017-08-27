////////////////////////////////////////////////////////////////
//
//			AMaZE demosaic algorithm
// (Aliasing minimization and Zipper Elimination)
//
//	copyright (c) 2008-2010  Emil Martinec <ejmartin@uchicago.edu>
//
// incorporating ideas of Luis Sanz Rodrigues and Paul Lee
//
// original code dated: May 27, 2010, last update 9bd3ef6835e4 (May 15, 2013)
// https://code.google.com/p/rawtherapee/source/browse/rtengine/amaze_demosaic_RT.cc
// modified by a1ex for integration with cr2hdr 
//
//	amaze_interpolate_RT.cc is free software: you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
////////////////////////////////////////////////////////////////

/* Collapse omp things removed */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include "debayer.h"

#ifdef __SSE2__
    #include "helpersse2.h"

    static INLINE vfloat vabsf(vfloat f) { return (vfloat)vandnotm((vmask)vcast_vf_f(-0.0f), (vmask)f); }

    static INLINE vfloat SQRV(vfloat a){
	    return _mm_mul_ps( a,a );
    }

    static INLINE vfloat vself(vmask mask, vfloat x, vfloat y) {
        return (vfloat)vorm(vandm(mask, (vmask)x), vandnotm(mask, (vmask)y));
    }

    static INLINE vfloat LIMV( vfloat a, vfloat b, vfloat c ) {
        return _mm_max_ps( b, _mm_min_ps(a,c));
    }

    static INLINE vfloat ULIMV( vfloat a, vfloat b, vfloat c  ){
	    return vself( vmaskf_lt(b,c), LIMV(a,b,c), LIMV(a,c,b));
    }
#else
    #define INLINE inline
#endif

#define initialGain 1.0 /* IDK */

/* assume RGGB */
/* see RT rawimage.h */

static int filter = 0x0;
static inline int FC(int row, int col)
{
    if ((row%2) == filter && (col%2) == 0)
        return 0;  /* red */
    else if ((row%2) != filter && (col%2) == 1)
        return 2;  /* blue */
    else
        return 1;  /* green */
}

#define COERCE(x,lo,hi) MAX(min((x),(hi)),(lo))

#define min(a,b) \
   ({ __typeof__ ((a)+(b)) _a = (a); \
      __typeof__ ((a)+(b)) _b = (b); \
     _a < _b ? _a : _b; })

#define MAX(a,b) \
   ({ __typeof__ ((a)+(b)) _a = (a); \
      __typeof__ ((a)+(b)) _b = (b); \
     _a > _b ? _a : _b; })

#define SQR(a) \
   ({ __typeof__ (a) _a = (a); \
     _a * _a; })

/* from RT sleef.c */
static INLINE float xmul2f(float d) {
	if (*(int*)&d & 0x7FFFFFFF) { // if f==0 do nothing
		*(int*)&d += 1 << 23; // add 1 to the exponent
		}
	return d;
}

static INLINE float xdiv2f(float d) {
	if (*(int*)&d & 0x7FFFFFFF) { // if f==0 do nothing
		*(int*)&d -= 1 << 23; // sub 1 from the exponent
		}
	return d;
}

static INLINE float xdivf( float d, int n){
	if (*(int*)&d & 0x7FFFFFFF) { // if f==0 do nothing
		*(int*)&d -= n << 23; // add n to the exponent
		}
	return d;
}	

/* adapted from rt_math.h */
#define LIM COERCE
#define ULIM(a, b, c) (((b) < (c)) ? LIM(a,b,c) : LIM(a,c,b))


void 
#ifdef __minGW32__ // Needed for win32/mingw (might need include gaurd with another compiler)
    __attribute__ ((force_align_arg_pointer))
#endif
demosaic(
	amazeinfo_t * inputdata /* All arguments in 1 struct for posix */
)

{
	float ** restrict rawData = inputdata->rawData;    /* holds preprocessed pixel values, rawData[i][j] corresponds to the ith row and jth column */
    float ** restrict red = inputdata->red;        /* the interpolated red plane */
    float ** restrict green = inputdata->green;      /* the interpolated green plane */
    float ** restrict blue = inputdata->blue;       /* the interpolated blue plane */
    int winx = inputdata->winx; int winy = inputdata->winy; /* crop window for demosaicing */
    int winw = inputdata->winw; int winh = inputdata->winh;
    int cfa = inputdata->cfa;

    filter = cfa;
//clock_t	t1,t2;
//t1 = clock();

#define HCLIP(x) x //is this still necessary???
	//min(clip_pt,x)

	int width=winw, height=winh;


	const float clip_pt = 1/initialGain;
	const float clip_pt8 = 0.8f/initialGain;


#define TS 224	 // Tile size; the image is processed in square tiles to lower memory requirements and facilitate multi-threading
#define TSH	112
//#define TS6 500
	// local variables


	//offset of R pixel within a Bayer quartet
	int ex, ey;

	//shifts of pointer value to access pixels in vertical and diagonal directions
	static const int v1=TS, v2=2*TS, v3=3*TS, p1=-TS+1, p2=-2*TS+2, p3=-3*TS+3, m1=TS+1, m2=2*TS+2, m3=3*TS+3;

	//tolerance to avoid dividing by zero
	static const float eps=1e-5, epssq=1e-10;			//tolerance to avoid dividing by zero

	//adaptive ratios threshold
	static const float arthresh=0.75;
	//nyquist texture test threshold
	static const float nyqthresh=0.5;

	//gaussian on 5x5 quincunx, sigma=1.2
	static const float gaussodd[4] = {0.14659727707323927f, 0.103592713382435f, 0.0732036125103057f, 0.0365543548389495f};
	//gaussian on 5x5, sigma=1.2
	static const float gaussgrad[6] = {0.07384411893421103f, 0.06207511968171489f, 0.0521818194747806f,
	0.03687419286733595f, 0.03099732204057846f, 0.018413194161458882f};
	//gaussian on 5x5 alt quincunx, sigma=1.5
	static const float gausseven[2] = {0.13719494435797422f, 0.05640252782101291f};
	//guassian on quincunx grid
	static const float gquinc[4] = {0.169917f, 0.108947f, 0.069855f, 0.0287182f};

	//~ volatile double progress = 0.0;
	// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

struct s_mp {
	float m;
	float p;
};
struct s_hv {
	float h;
	float v;
};
#pragma omp parallel
{
	//position of top/left corner of the tile
	int top, left;
	// beginning of storage block for tile
	char  *buffer;
	// rgb values
	float (*rgbgreen);

//	float (*rgb)[3];
	// horizontal gradient
//	float (*delh);
	// vertical gradient
//	float (*delv);
	// square of delh
	float (*delhsq);
	// square of delv
	float (*delvsq);
	// gradient based directional weights for interpolation
//	float (*dirwts)[2];
	float (*dirwts0);
	float (*dirwts1);

	// vertically interpolated color differences G-R, G-B
	float (*vcd);
	// horizontally interpolated color differences
	float (*hcd);
	// vertically interpolated color differences G-R, G-B
	float (*vcdnew);
	// horizontally interpolated color differences
	float (*hcdnew);
	// alternative vertical interpolation
	float (*vcdalt);
	// alternative horizontal interpolation
	float (*hcdalt);
	// square of average color difference
	float (*cddiffsq);
	// weight to give horizontal vs vertical interpolation
	float (*hvwt);
	// final interpolated color difference
	float (*Dgrb)[TS*TS];
//	float (*Dgrb)[2];
	// gradient in plus (NE/SW) direction
	float (*delp);
	// gradient in minus (NW/SE) direction
	float (*delm);
	// diagonal interpolation of R+B
	float (*rbint);
	struct s_hv  (*Dgrb2);
	// horizontal curvature of interpolated G (used to refine interpolation in Nyquist texture regions)
//	float (*Dgrbh2);
	// vertical curvature of interpolated G
//	float (*Dgrbv2);
	// difference between up/down interpolations of G
	float (*dgintv);
	// difference between left/right interpolations of G
	float (*dginth);
	// diagonal (plus) color difference R-B or G1-G2
//	float (*Dgrbp1);
	// diagonal (minus) color difference R-B or G1-G2
//	float (*Dgrbm1);
	struct s_mp  (*Dgrbsq1);
	// square of diagonal color difference
//	float (*Dgrbpsq1);
	// square of diagonal color difference
//	float (*Dgrbmsq1);
	// tile raw data
	float (*cfa);
	// relative weight for combining plus and minus diagonal interpolations
	float (*pmwt);
	// interpolated color difference R-B in minus and plus direction
	struct s_mp  (*rb);
	// interpolated color difference R-B in plus direction
//	float (*rbp);
	// interpolated color difference R-B in minus direction
//	float (*rbm);

	// nyquist texture flag 1=nyquist, 0=not nyquist
	char   (*nyquist);

#define CLF 1
	// assign working space
	buffer = (char *) malloc(29*sizeof(float)*TS*TS - sizeof(float)*TS*TSH + sizeof(char)*TS*TSH+24*CLF*64);
	char 	*data;
    data = (char*)( ( (uintptr_t)(buffer) + (uintptr_t)(63)) / 64 * 64);


	//merror(buffer,"amaze_interpolate()");
	//memset(buffer,0,(34*sizeof(float)+sizeof(int))*TS*TS);
	// rgb array
	rgbgreen	= (float (*))			data; //pointers to array
	
//	rgb			= (float (*)[3])		data; //pointers to array
//	delh		= (float (*))			(data +  3*sizeof(float)*TS*TS+1*CLF*64);
//	delv		= (float (*))			(data +  4*sizeof(float)*TS*TS+2*CLF*64);
	delhsq		= (float (*))			(data +  1*sizeof(float)*TS*TS+1*CLF*64);
	delvsq		= (float (*))			(data +  2*sizeof(float)*TS*TS+2*CLF*64);
	dirwts0		= (float (*)	)		(data +  3*sizeof(float)*TS*TS+3*CLF*64);
	dirwts1		= (float (*)	)		(data +  4*sizeof(float)*TS*TS+4*CLF*64);
	
//	dirwts		= (float (*)[2])		(data +  7*sizeof(float)*TS*TS+5*CLF*64);
	vcd			= (float (*))			(data +  5*sizeof(float)*TS*TS+5*CLF*64);
	hcd			= (float (*))			(data +  6*sizeof(float)*TS*TS+6*CLF*64);
	vcdalt		= (float (*))			(data +  7*sizeof(float)*TS*TS+7*CLF*64);
	hcdalt		= (float (*))			(data +  8*sizeof(float)*TS*TS+8*CLF*64);
	cddiffsq	= (float (*))			(data +  9*sizeof(float)*TS*TS+9*CLF*64);
	hvwt		= (float (*))			(data +  10*sizeof(float)*TS*TS+10*CLF*64);							//compressed			0.5 MB
	Dgrb		= (float (*)[TS*TS])	(data +  11*sizeof(float)*TS*TS - sizeof(float)*TS*TSH+11*CLF*64);  //compressed
	delp		= (float (*))			(data +  12*sizeof(float)*TS*TS - sizeof(float)*TS*TSH+12*CLF*64);	// compressed			0.5 MB
	delm		= (float (*))			(data +  12*sizeof(float)*TS*TS+13*CLF*64);							// compressed			0.5 MB
	rbint		= (float (*))			(data +  13*sizeof(float)*TS*TS - sizeof(float)*TS*TSH+14*CLF*64);	// compressed			0.5 MB
	Dgrb2		= (struct s_hv  (*))			(data +  13*sizeof(float)*TS*TS+15*CLF*64);							// compressed			1.0 MB
//	Dgrbh2		= (float (*))			(data +  19*sizeof(float)*TS*TS);
//	Dgrbv2		= (float (*))			(data +  20*sizeof(float)*TS*TS);
	dgintv		= (float (*))			(data +  14*sizeof(float)*TS*TS+16*CLF*64);
	dginth		= (float (*))			(data +  15*sizeof(float)*TS*TS+17*CLF*64);
//	Dgrbp1		= (float (*))			(data +  23*sizeof(float)*TS*TS);													1.0 MB
//	Dgrbm1		= (float (*))			(data +  23*sizeof(float)*TS*TS);													1.0 MB
	Dgrbsq1		= (struct s_mp  (*))			(data +  16*sizeof(float)*TS*TS+18*CLF*64);							// compressed			1.0 MB
//	Dgrbpsq1	= (float (*))			(data +  23*sizeof(float)*TS*TS);
//	Dgrbmsq1	= (float (*))			(data +  24*sizeof(float)*TS*TS);
	cfa			= (float (*))			(data +  17*sizeof(float)*TS*TS+19*CLF*64);
	pmwt		= (float (*))			(data +  18*sizeof(float)*TS*TS+20*CLF*64);		// compressed								0.5 MB
	rb			= (struct s_mp  (*))			(data +  19*sizeof(float)*TS*TS - sizeof(float)*TS*TSH+21*CLF*64);		// compressed		1.0 MB
	hcdnew      = (float (*))			(data +  20*sizeof(float)*TS*TS+22*CLF*64);
	vcdnew      = (float (*))			(data +  21*sizeof(float)*TS*TS+23*CLF*64);
//	rbp			= (float (*))			(data +  30*sizeof(float)*TS*TS);
//	rbm			= (float (*))			(data +  31*sizeof(float)*TS*TS);

	nyquist		= (char (*))				(data +  22*sizeof(float)*TS*TS +24*CLF*64);	//compressed		0.875 MB
#undef CLF
	// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

	/*double dt;
	 clock_t t1, t2;

	 clock_t t1_init,       t2_init       = 0;
	 clock_t t1_vcdhcd,      t2_vcdhcd      = 0;
	 clock_t t1_cdvar,		t2_cdvar = 0;
	 clock_t t1_nyqtest,   t2_nyqtest   = 0;
	 clock_t t1_areainterp,  t2_areainterp  = 0;
	 clock_t t1_compare,   t2_compare   = 0;
	 clock_t t1_diag,   t2_diag   = 0;
	 clock_t t1_chroma,    t2_chroma    = 0;*/


	// start
	//printf ("AMaZE interpolation ...\n");
	//t1 = clock();

	// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%


	// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%


	//determine GRBG coset; (ey,ex) is the offset of the R subarray
	if (FC(0,0)==1) {//first pixel is G
		if (FC(0,1)==0) {ey=0; ex=1;} else {ey=1; ex=0;}
	} else {//first pixel is R or B
		if (FC(0,0)==0) {ey=0; ex=0;} else {ey=1; ex=1;}
	}

// #pragma omp for schedule(dynamic) collapse(2) 
    for (top=winy; top < winy+winh; top += 1) 
        for (left=winx; left < winx+winw; left += 1) {
            float val = rawData[top][left];
            val = 64.0f*log2(val);
            rawData[top][left] = val;
    } 
	// Main algorithm: Tile loop
	//#pragma omp parallel for shared(rawData,height,width,red,green,blue) private(top,left) schedule(dynamic)
	//code is openmp ready; just have to pull local tile variable declarations inside the tile loop

// Issue 1676
// use collapse(2) to collapse the 2 loops to one large loop, so there is better scaling
// #pragma omp for schedule(dynamic) collapse(2) 
	for (top=winy-16; top < winy+height; top += TS-32)
		for (left=winx-16; left < winx+width; left += TS-32) {
            //printf("top %d left %d",top,left);
			memset(nyquist, 0, sizeof(char)*TS*TSH);
			memset(rbint, 0, sizeof(float)*TS*TSH);
			//location of tile bottom edge
			int bottom = min(top+TS,winy+height+16);
			//location of tile right edge
			int right  = min(left+TS, winx+width+16);
			//tile width  (=TS except for right edge of image)
			int rr1 = bottom - top;
			//tile height (=TS except for bottom edge of image)
			int cc1 = right - left;

			//tile vars
			//counters for pixel location in the image
			int row, col;
			//min and max row/column in the tile
			int rrmin, rrmax, ccmin, ccmax;
			//counters for pixel location within the tile
			int rr, cc;
			//color index 0=R, 1=G, 2=B
			int c;
			//pointer counters within the tile
			int indx, indx1;
			//dummy indices
			int i, j;
			// +1 or -1
//			int sgn;

			//color ratios in up/down/left/right directions
			float cru, crd, crl, crr;
			//adaptive weights for vertical/horizontal/plus/minus directions
			float vwt, hwt, pwt, mwt;
			//vertical and horizontal G interpolations
			float Gintv, Ginth;
			//G interpolated in vert/hor directions using adaptive ratios
			// float guar, gdar, glar, grar;
			//G interpolated in vert/hor directions using Hamilton-Adams method
			// float guha, gdha, glha, grha;
			//interpolated G from fusing left/right or up/down
			// float Ginthar, Ginthha, Gintvar, Gintvha;
			//color difference (G-R or G-B) variance in up/down/left/right directions
			float Dgrbvvaru, Dgrbvvard, Dgrbhvarl, Dgrbhvarr;
			float uave, dave, lave, rave;

			//color difference variances in vertical and horizontal directions
			float vcdvar, hcdvar, vcdvar1, hcdvar1;
			//adaptive interpolation weight using variance of color differences
			float varwt;																										// 639 - 644
			//adaptive interpolation weight using difference of left-right and up-down G interpolations
			float diffwt;																										// 640 - 644
			//alternative adaptive weight for combining horizontal/vertical interpolations
			float hvwtalt;																										// 745 - 748
			//temporary variables for combining interpolation weights at R and B sites
//			float vo, ve;
			//interpolation of G in four directions
			float gu, gd, gl, gr;
			//variance of G in vertical/horizontal directions
			float gvarh, gvarv;

			//Nyquist texture test
			float nyqtest;																										// 658 - 681
			//accumulators for Nyquist texture interpolation
			float sumh, sumv, sumsqh, sumsqv, areawt;

			//color ratios in diagonal directions
			float crse, crnw, crne, crsw;
			//color differences in diagonal directions
			float rbse, rbnw, rbne, rbsw;
			//adaptive weights for combining diagonal interpolations
			float wtse, wtnw, wtsw, wtne;
			//alternate weight for combining diagonal interpolations
			float pmwtalt;																										// 885 - 888
			//variance of R-B in plus/minus directions
			float rbvarm;																										// 843 - 848



			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%


			// rgb from input CFA data
			// rgb values should be floating point number between 0 and 1
			// after white balance multipliers are applied
			// a 16 pixel border is added to each side of the image

			// bookkeeping for borders
			if (top<winy) {rrmin=16;} else {rrmin=0;}
			if (left<winx) {ccmin=16;} else {ccmin=0;}
			if (bottom>(winy+height)) {rrmax=winy+height-top;} else {rrmax=rr1;}
			if (right>(winx+width)) {ccmax=winx+width-left;} else {ccmax=cc1;}

#ifdef __SSE2__
			const __m128 d65535v = _mm_set1_ps( 65535.0f );
#endif // __SSE2__

#ifdef __SSE2__
			__m128	tempv;
			for (rr=rrmin; rr < rrmax; rr++){
//				_mm_prefetch( &rawData[rr+top+1][ccmin+left], _MM_HINT_NTA);
				for (row=rr+top, cc=ccmin; cc < ccmax; cc+=4) {
					indx1=rr*TS+cc;
					tempv = LVFU(rawData[row][cc+left]) / d65535v;
					_mm_store_ps( &cfa[indx1], tempv );
					_mm_store_ps( &rgbgreen[indx1], tempv );
				}
			}
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			//fill borders
			if (rrmin>0) {
				for (rr=0; rr<16; rr++)
					for (cc=ccmin,row = 32-rr+top; cc<ccmax; cc++) {
						cfa[rr*TS+cc] = (rawData[row][cc+left])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[rr*TS+cc] = cfa[rr*TS+cc];
					}
			}
			if (rrmax<rr1) {
				for (rr=0; rr<16; rr++)
					for (cc=ccmin; cc<ccmax; cc+=4) {
						indx1 = (rrmax+rr)*TS+cc;
						tempv = LVFU(rawData[(winy+height-rr-2)][left+cc]) / d65535v;
						_mm_store_ps( &cfa[indx1], tempv );
						_mm_store_ps( &rgbgreen[indx1], tempv );
					}
			}

			if (ccmin>0) {
				for (rr=rrmin; rr<rrmax; rr++)
					for (cc=0,row = rr + top; cc<16; cc++) {
						cfa[rr*TS+cc] = (rawData[row][32-cc+left])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[rr*TS+cc] = cfa[rr*TS+cc];
					}
			}

			if (ccmax<cc1) {
				for (rr=rrmin; rr<rrmax; rr++)
					for (cc=0; cc<16; cc++) {
						cfa[rr*TS+ccmax+cc] = (rawData[(top+rr)][(winx+width-cc-2)])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[rr*TS+ccmax+cc] = cfa[rr*TS+ccmax+cc];
					}
			}

/*
			if (ccmin>0) {
				for (rr=rrmin; rr<rrmax; rr++)
					for (cc=0,row = rr + top; cc<16; cc+=4) {
						indx1 = rr*TS+cc;
						tempv = LVFU(rawData[row][32-cc+left]) / d65535v;
						_mm_store_ps( &cfa[indx1], tempv );
						_mm_store_ps( &rgbgreen[indx1], tempv );
					}
			}
*/
/*
			if (ccmax<cc1) {
				for (rr=rrmin; rr<rrmax; rr++)
					for (cc=0; cc<16; cc+=4) {
						indx1 = rr*TS+ccmax+cc;
						tempv = LVFU(rawData[(top+rr)][(winx+width-cc-2)]) / d65535v;
						_mm_storeu_ps( &cfa[indx1], tempv );
						_mm_storeu_ps( &rgbgreen[indx1], tempv );
					}
			}
*/
			//also, fill the image corners
			if (rrmin>0 && ccmin>0) {
				for (rr=0; rr<16; rr++)
					for (cc=0; cc<16; cc+=4) {
						indx1 = (rr)*TS+cc;
						tempv = LVFU(rawData[winy+32-rr][winx+32-cc]) / d65535v;
						_mm_store_ps( &cfa[indx1], tempv );
						_mm_store_ps( &rgbgreen[indx1], tempv );
					}
			}
			if (rrmax<rr1 && ccmax<cc1) {
				for (rr=0; rr<16; rr++)
					for (cc=0; cc<16; cc+=4) {
						indx1 = (rrmax+rr)*TS+ccmax+cc;
						tempv = LVFU(rawData[(winy+height-rr-2)][(winx+width-cc-2)]) / d65535v;
						_mm_storeu_ps( &cfa[indx1], tempv );
						_mm_storeu_ps( &rgbgreen[indx1], tempv );
					}
			}
			if (rrmin>0 && ccmax<cc1) {
				for (rr=0; rr<16; rr++)
					for (cc=0; cc<16; cc++) {
							
						cfa[(rr)*TS+ccmax+cc] = (rawData[(winy+32-rr)][(winx+width-cc-2)])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[(rr)*TS+ccmax+cc] = cfa[(rr)*TS+ccmax+cc];
					}
			}
			if (rrmax<rr1 && ccmin>0) {
				for (rr=0; rr<16; rr++)
					for (cc=0; cc<16; cc++) {
						cfa[(rrmax+rr)*TS+cc] = (rawData[(winy+height-rr-2)][(winx+32-cc)])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[(rrmax+rr)*TS+cc] = cfa[(rrmax+rr)*TS+cc];
					}
			}				
#else
			for (rr=rrmin; rr < rrmax; rr++)
				for (row=rr+top, cc=ccmin; cc < ccmax; cc++) {
					indx1=rr*TS+cc;
					cfa[indx1] = (rawData[row][cc+left])/65535.0f;
					if(FC(rr,cc)==1)
						rgbgreen[indx1] = cfa[indx1];
						
				}

			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			//fill borders
			if (rrmin>0) {
				for (rr=0; rr<16; rr++)
					for (cc=ccmin,row = 32-rr+top; cc<ccmax; cc++) {
						cfa[rr*TS+cc] = (rawData[row][cc+left])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[rr*TS+cc] = cfa[rr*TS+cc];
					}
			}
			if (rrmax<rr1) {
				for (rr=0; rr<16; rr++)
					for (cc=ccmin; cc<ccmax; cc++) {
						cfa[(rrmax+rr)*TS+cc] = (rawData[(winy+height-rr-2)][left+cc])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[(rrmax+rr)*TS+cc] = cfa[(rrmax+rr)*TS+cc];
					}
			}
			if (ccmin>0) {
				for (rr=rrmin; rr<rrmax; rr++)
					for (cc=0,row = rr + top; cc<16; cc++) {
						cfa[rr*TS+cc] = (rawData[row][32-cc+left])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[rr*TS+cc] = cfa[rr*TS+cc];
					}
			}
			if (ccmax<cc1) {
				for (rr=rrmin; rr<rrmax; rr++)
					for (cc=0; cc<16; cc++) {
						cfa[rr*TS+ccmax+cc] = (rawData[(top+rr)][(winx+width-cc-2)])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[rr*TS+ccmax+cc] = cfa[rr*TS+ccmax+cc];
					}
			}

			//also, fill the image corners
			if (rrmin>0 && ccmin>0) {
				for (rr=0; rr<16; rr++)
					for (cc=0; cc<16; cc++) {
						cfa[(rr)*TS+cc] = (rawData[winy+32-rr][winx+32-cc])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[(rr)*TS+cc] = cfa[(rr)*TS+cc];
					}
			}
			if (rrmax<rr1 && ccmax<cc1) {
				for (rr=0; rr<16; rr++)
					for (cc=0; cc<16; cc++) {
						cfa[(rrmax+rr)*TS+ccmax+cc] = (rawData[(winy+height-rr-2)][(winx+width-cc-2)])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[(rrmax+rr)*TS+ccmax+cc] = cfa[(rrmax+rr)*TS+ccmax+cc];
					}
			}
			if (rrmin>0 && ccmax<cc1) {
				for (rr=0; rr<16; rr++)
					for (cc=0; cc<16; cc++) {
						cfa[(rr)*TS+ccmax+cc] = (rawData[(winy+32-rr)][(winx+width-cc-2)])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[(rr)*TS+ccmax+cc] = cfa[(rr)*TS+ccmax+cc];
					}
			}
			if (rrmax<rr1 && ccmin>0) {
				for (rr=0; rr<16; rr++)
					for (cc=0; cc<16; cc++) {
						cfa[(rrmax+rr)*TS+cc] = (rawData[(winy+height-rr-2)][(winx+32-cc)])/65535.0f;
						if(FC(rr,cc)==1)
							rgbgreen[(rrmax+rr)*TS+cc] = cfa[(rrmax+rr)*TS+cc];
					}
			}
#endif

			//end of border fill
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
#ifdef __SSE2__
			__m128 delhv,delvv;
			const __m128 epsv = _mm_set1_ps( eps );

			for (rr=2; rr < rr1-2; rr++) {
				for (cc=0, indx=(rr)*TS+cc; cc < cc1; cc+=4, indx+=4) {
					delhv = vabsf( LVFU( cfa[indx+1] ) -  LVFU( cfa[indx-1] ) );
					delvv = vabsf( LVF( cfa[indx+v1] ) -  LVF( cfa[indx-v1] ) );
					_mm_store_ps( &dirwts1[indx], epsv + vabsf( LVFU( cfa[indx+2] ) - LVF( cfa[indx] )) + vabsf( LVF( cfa[indx] ) - LVFU( cfa[indx-2] )) + delhv );
					delhv = delhv * delhv;
					_mm_store_ps( &dirwts0[indx], epsv + vabsf( LVF( cfa[indx+v2] ) - LVF( cfa[indx] )) + vabsf( LVF( cfa[indx] ) - LVF( cfa[indx-v2] )) + delvv );
					delvv = delvv * delvv;
					_mm_store_ps( &delhsq[indx], delhv);
					_mm_store_ps( &delvsq[indx], delvv);
				}
			}

#else
			// horizontal and vedrtical gradient
			float delh,delv;
			for (rr=2; rr < rr1-2; rr++)
				for (cc=2, indx=(rr)*TS+cc; cc < cc1-2; cc++, indx++) {

					delh = fabsf(cfa[indx+1]-cfa[indx-1]);
					delv = fabsf(cfa[indx+v1]-cfa[indx-v1]);
					dirwts0[indx] = eps+fabsf(cfa[indx+v2]-cfa[indx])+fabsf(cfa[indx]-cfa[indx-v2])+delv;
					dirwts1[indx] = eps+fabsf(cfa[indx+2]-cfa[indx])+fabsf(cfa[indx]-cfa[indx-2])+delh;//+fabsf(cfa[indx+2]-cfa[indx-2]);
					delhsq[indx] = SQR(delh);
					delvsq[indx] = SQR(delv);
				}
#endif

#ifdef __SSE2__
//			__m128	tempv;
			__m128	Dgrbsq1pv, Dgrbsq1mv,temp2v;
			for (rr=6; rr < rr1-6; rr++){
				if((FC(rr,2)&1)==0) {
					for (cc=6, indx=(rr)*TS+cc; cc < cc1-6; cc+=8, indx+=8) {
						tempv = LC2VFU(cfa[indx+1]);
						Dgrbsq1pv = (SQRV(tempv-LC2VFU(cfa[indx+1-p1]))+SQRV(tempv-LC2VFU(cfa[indx+1+p1])));
						_mm_storeu_ps( &delp[indx>>1], vabsf(LC2VFU(cfa[indx+p1])-LC2VFU(cfa[indx-p1])));
						_mm_storeu_ps( &delm[indx>>1], vabsf(LC2VFU(cfa[indx+m1])-LC2VFU(cfa[indx-m1])));
						Dgrbsq1mv = (SQRV(tempv-LC2VFU(cfa[indx+1-m1]))+SQRV(tempv-LC2VFU(cfa[indx+1+m1])));
						tempv = _mm_shuffle_ps( Dgrbsq1mv, Dgrbsq1pv, _MM_SHUFFLE( 1,0,1,0 ) );
						_mm_storeu_ps( &Dgrbsq1[indx>>1].m, _mm_shuffle_ps( tempv, tempv, _MM_SHUFFLE( 3,1,2,0 ) ));
						temp2v = _mm_shuffle_ps( Dgrbsq1mv, Dgrbsq1pv, _MM_SHUFFLE( 3,2,3,2 ) );
						_mm_storeu_ps( &Dgrbsq1[(indx+4)>>1].m, _mm_shuffle_ps( temp2v, temp2v, _MM_SHUFFLE( 3,1,2,0 ) ));
					}
				}
				else {
					for (cc=6, indx=(rr)*TS+cc; cc < cc1-6; cc+=8, indx+=8) {
						tempv = LC2VFU(cfa[indx]);
						Dgrbsq1pv = (SQRV(tempv-LC2VFU(cfa[indx-p1]))+SQRV(tempv-LC2VFU(cfa[indx+p1])));
						_mm_storeu_ps( &delp[indx>>1], vabsf(LC2VFU(cfa[indx+1+p1])-LC2VFU(cfa[indx+1-p1])));
						_mm_storeu_ps( &delm[indx>>1], vabsf(LC2VFU(cfa[indx+1+m1])-LC2VFU(cfa[indx+1-m1])));
						Dgrbsq1mv = (SQRV(tempv-LC2VFU(cfa[indx-m1]))+SQRV(tempv-LC2VFU(cfa[indx+m1])));
						tempv = _mm_shuffle_ps( Dgrbsq1mv, Dgrbsq1pv, _MM_SHUFFLE( 1,0,1,0 ) );
						_mm_storeu_ps( &Dgrbsq1[indx>>1].m, _mm_shuffle_ps( tempv, tempv, _MM_SHUFFLE( 3,1,2,0 ) ));
						temp2v = _mm_shuffle_ps( Dgrbsq1mv, Dgrbsq1pv, _MM_SHUFFLE( 3,2,3,2 ) );
						_mm_storeu_ps( &Dgrbsq1[(indx+4)>>1].m, _mm_shuffle_ps( temp2v, temp2v, _MM_SHUFFLE( 3,1,2,0 ) ));
					}
				}
			}
#else
			for (rr=6; rr < rr1-6; rr++){
				if((FC(rr,2)&1)==0) {
					for (cc=6, indx=(rr)*TS+cc; cc < cc1-6; cc+=2, indx+=2) {
						delp[indx>>1] = fabsf(cfa[indx+p1]-cfa[indx-p1]);
						delm[indx>>1] = fabsf(cfa[indx+m1]-cfa[indx-m1]);
						Dgrbsq1[indx>>1].p=(SQR(cfa[indx+1]-cfa[indx+1-p1])+SQR(cfa[indx+1]-cfa[indx+1+p1]));
						Dgrbsq1[indx>>1].m=(SQR(cfa[indx+1]-cfa[indx+1-m1])+SQR(cfa[indx+1]-cfa[indx+1+m1]));
					}
				}
				else {
					for (cc=6, indx=(rr)*TS+cc; cc < cc1-6; cc+=2, indx+=2) {
						Dgrbsq1[indx>>1].p=(SQR(cfa[indx]-cfa[indx-p1])+SQR(cfa[indx]-cfa[indx+p1]));
						Dgrbsq1[indx>>1].m=(SQR(cfa[indx]-cfa[indx-m1])+SQR(cfa[indx]-cfa[indx+m1]));
						delp[indx>>1] = fabsf(cfa[indx+1+p1]-cfa[indx+1-p1]);
						delm[indx>>1] = fabsf(cfa[indx+1+m1]-cfa[indx+1-m1]);
					}
				}
			}
#endif



			//t2_init += clock()-t1_init;
			// end of tile initialization
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

			//interpolate vertical and horizontal color differences
			//t1_vcdhcd = clock();

			// int	fcswitch = 0;
#ifdef __SSE2__
			__m128	sgnv,cruv,crdv,crlv,crrv,guhav,gdhav,glhav,grhav,hwtv,vwtv,Gintvhav,Ginthhav,guarv,gdarv,glarv,grarv;
			vmask	clipmask;
			if( !(FC(4,4)&1) )
				sgnv = _mm_set_ps( 1.0f, -1.0f, 1.0f, -1.0f );
			else
				sgnv = _mm_set_ps( -1.0f, 1.0f, -1.0f, 1.0f );
			
			__m128	zd5v = _mm_set1_ps( 0.5f );
			__m128	onev = _mm_set1_ps( 1.0f );
			__m128  arthreshv = _mm_set1_ps( arthresh );
			__m128  clip_pt8v = _mm_set1_ps( clip_pt8 );
			
			for (rr=4; rr<rr1-4; rr++) {
				sgnv = -sgnv;
				for (cc=4,indx=rr*TS+cc; cc<cc1-7; cc+=4,indx+=4) {
					
					//color ratios in each cardinal direction
					cruv = LVF(cfa[indx-v1])*(LVF(dirwts0[indx-v2])+LVF(dirwts0[indx]))/(LVF(dirwts0[indx-v2])*(epsv+LVF(cfa[indx]))+LVF(dirwts0[indx])*(epsv+LVF(cfa[indx-v2])));
					crdv = LVF(cfa[indx+v1])*(LVF(dirwts0[indx+v2])+LVF(dirwts0[indx]))/(LVF(dirwts0[indx+v2])*(epsv+LVF(cfa[indx]))+LVF(dirwts0[indx])*(epsv+LVF(cfa[indx+v2])));
					crlv = LVFU(cfa[indx-1])*(LVFU(dirwts1[indx-2])+LVF(dirwts1[indx]))/(LVFU(dirwts1[indx-2])*(epsv+LVF(cfa[indx]))+LVF(dirwts1[indx])*(epsv+LVFU(cfa[indx-2])));
					crrv = LVFU(cfa[indx+1])*(LVFU(dirwts1[indx+2])+LVF(dirwts1[indx]))/(LVFU(dirwts1[indx+2])*(epsv+LVF(cfa[indx]))+LVF(dirwts1[indx])*(epsv+LVFU(cfa[indx+2])));

					guhav=LVF(cfa[indx-v1])+zd5v*(LVF(cfa[indx])-LVF(cfa[indx-v2]));
					gdhav=LVF(cfa[indx+v1])+zd5v*(LVF(cfa[indx])-LVF(cfa[indx+v2]));
					glhav=LVFU(cfa[indx-1])+zd5v*(LVF(cfa[indx])-LVFU(cfa[indx-2]));
					grhav=LVFU(cfa[indx+1])+zd5v*(LVF(cfa[indx])-LVFU(cfa[indx+2]));
					
					guarv = vself(vmaskf_lt(vabsf(onev-cruv), arthreshv), LVF(cfa[indx])*cruv, guhav);
					gdarv = vself(vmaskf_lt(vabsf(onev-crdv), arthreshv), LVF(cfa[indx])*crdv, gdhav);
					glarv = vself(vmaskf_lt(vabsf(onev-crlv), arthreshv), LVF(cfa[indx])*crlv, glhav);
					grarv = vself(vmaskf_lt(vabsf(onev-crrv), arthreshv), LVF(cfa[indx])*crrv, grhav);

					hwtv = LVFU(dirwts1[indx-1])/(LVFU(dirwts1[indx-1])+LVFU(dirwts1[indx+1]));
					vwtv = LVF(dirwts0[indx-v1])/(LVF(dirwts0[indx+v1])+LVF(dirwts0[indx-v1]));

					//interpolated G via adaptive weights of cardinal evaluations
					Ginthhav = hwtv*grhav+(onev-hwtv)*glhav;
					Gintvhav = vwtv*gdhav+(onev-vwtv)*guhav;
					//interpolated color differences
					
					_mm_store_ps( &hcdalt[indx], sgnv*(Ginthhav-LVF(cfa[indx])));
					_mm_store_ps( &vcdalt[indx], sgnv*(Gintvhav-LVF(cfa[indx])));

					clipmask = vorm( vorm( vmaskf_gt( LVF(cfa[indx]), clip_pt8v ), vmaskf_gt( Gintvhav, clip_pt8v ) ), vmaskf_gt( Ginthhav, clip_pt8v ));
					guarv = vself( clipmask, guhav, guarv);
					gdarv = vself( clipmask, gdhav, gdarv);
					glarv = vself( clipmask, glhav, glarv);
					grarv = vself( clipmask, grhav, grarv);
					_mm_store_ps( &vcd[indx], vself( clipmask, LVF(vcdalt[indx]), sgnv*((vwtv*gdarv+(onev-vwtv)*guarv)-LVF(cfa[indx]))));
					_mm_store_ps( &hcd[indx], vself( clipmask, LVF(hcdalt[indx]), sgnv*((hwtv*grarv+(onev-hwtv)*glarv)-LVF(cfa[indx]))));
					//differences of interpolations in opposite directions
					
					_mm_store_ps(&dgintv[indx],_mm_min_ps(SQRV(guhav-gdhav),SQRV(guarv-gdarv)));
					_mm_store_ps(&dginth[indx],_mm_min_ps(SQRV(glhav-grhav),SQRV(glarv-grarv)));

				}
			}
#else
			for (rr=4; rr<rr1-4; rr++) {
				for (cc=4,indx=rr*TS+cc,fcswitch = FC(rr,cc)&1; cc<cc1-4; cc++,indx++) {
					//color ratios in each cardinal direction
					cru = cfa[indx-v1]*(dirwts0[indx-v2]+dirwts0[indx])/(dirwts0[indx-v2]*(eps+cfa[indx])+dirwts0[indx]*(eps+cfa[indx-v2]));
					crd = cfa[indx+v1]*(dirwts0[indx+v2]+dirwts0[indx])/(dirwts0[indx+v2]*(eps+cfa[indx])+dirwts0[indx]*(eps+cfa[indx+v2]));
					crl = cfa[indx-1]*(dirwts1[indx-2]+dirwts1[indx])/(dirwts1[indx-2]*(eps+cfa[indx])+dirwts1[indx]*(eps+cfa[indx-2]));
					crr = cfa[indx+1]*(dirwts1[indx+2]+dirwts1[indx])/(dirwts1[indx+2]*(eps+cfa[indx])+dirwts1[indx]*(eps+cfa[indx+2]));

					guha=HCLIP(cfa[indx-v1])+xdiv2f(cfa[indx]-cfa[indx-v2]);
					gdha=HCLIP(cfa[indx+v1])+xdiv2f(cfa[indx]-cfa[indx+v2]);
					glha=HCLIP(cfa[indx-1])+xdiv2f(cfa[indx]-cfa[indx-2]);
					grha=HCLIP(cfa[indx+1])+xdiv2f(cfa[indx]-cfa[indx+2]);

					if (fabsf(1.0f-cru)<arthresh) {guar=cfa[indx]*cru;} else {guar=guha;}
					if (fabsf(1.0f-crd)<arthresh) {gdar=cfa[indx]*crd;} else {gdar=gdha;}
					if (fabsf(1.0f-crl)<arthresh) {glar=cfa[indx]*crl;} else {glar=glha;}
					if (fabsf(1.0f-crr)<arthresh) {grar=cfa[indx]*crr;} else {grar=grha;}

					hwt = dirwts1[indx-1]/(dirwts1[indx-1]+dirwts1[indx+1]);
					vwt = dirwts0[indx-v1]/(dirwts0[indx+v1]+dirwts0[indx-v1]);

					//interpolated G via adaptive weights of cardinal evaluations
					Gintvha = vwt*gdha+(1.0f-vwt)*guha;
					Ginthha = hwt*grha+(1.0f-hwt)*glha;
					//interpolated color differences
					if (fcswitch) {
						vcd[indx] = cfa[indx]-(vwt*gdar+(1.0f-vwt)*guar);
						hcd[indx] = cfa[indx]-(hwt*grar+(1.0f-hwt)*glar);
						vcdalt[indx] = cfa[indx]-Gintvha;
						hcdalt[indx] = cfa[indx]-Ginthha;
					} else {
					//interpolated color differences
						vcd[indx] = (vwt*gdar+(1.0f-vwt)*guar)-cfa[indx];
						hcd[indx] = (hwt*grar+(1.0f-hwt)*glar)-cfa[indx];
						vcdalt[indx] = Gintvha-cfa[indx];
						hcdalt[indx] = Ginthha-cfa[indx];
					}
					fcswitch = !fcswitch;

					if (cfa[indx] > clip_pt8 || Gintvha > clip_pt8 || Ginthha > clip_pt8) {
						//use HA if highlights are (nearly) clipped
						guar=guha; gdar=gdha; glar=glha; grar=grha;
						vcd[indx]=vcdalt[indx]; hcd[indx]=hcdalt[indx];
					}

					//differences of interpolations in opposite directions
					dgintv[indx]=min(SQR(guha-gdha),SQR(guar-gdar));
					dginth[indx]=min(SQR(glha-grha),SQR(glar-grar));

				}

			
			}
#endif
			//t2_vcdhcd += clock() - t1_vcdhcd;

			//t1_cdvar = clock();
#ifdef __SSE2__
			__m128	zerov = _mm_set1_ps( 0.0f );
			__m128  hcdvarv, vcdvarv;
			__m128	hcdaltvarv,vcdaltvarv,hcdv,vcdv,hcdaltv,vcdaltv,sgn3v,Ginthv,Gintvv,hcdoldv,vcdoldv;
			__m128	threev = _mm_set1_ps( 3.0f );
			__m128 	clip_ptv = _mm_set1_ps( clip_pt );
			__m128	nsgnv;
			vmask	hcdmask, vcdmask;

			if( !(FC(4,4)&1) )
				sgnv = _mm_set_ps( 1.0f, -1.0f, 1.0f, -1.0f );
			else
				sgnv = _mm_set_ps( -1.0f, 1.0f, -1.0f, 1.0f );

			sgn3v = threev * sgnv;
			for (rr=4; rr<rr1-4; rr++) {
				nsgnv = sgnv;
				sgnv = -sgnv;
				sgn3v = -sgn3v;
				for (cc=4,indx=rr*TS+cc,c=FC(rr,cc)&1; cc<cc1-4; cc+=4,indx+=4) {
					hcdv = LVF( hcd[indx] );
					hcdvarv = threev*(SQRV(LVFU(hcd[indx-2]))+SQRV(hcdv)+SQRV(LVFU(hcd[indx+2])))-SQRV(LVFU(hcd[indx-2])+hcdv+LVFU(hcd[indx+2]));
					hcdaltv = LVF( hcdalt[indx] );
					hcdaltvarv = threev*(SQRV(LVFU(hcdalt[indx-2]))+SQRV(hcdaltv)+SQRV(LVFU(hcdalt[indx+2])))-SQRV(LVFU(hcdalt[indx-2])+hcdaltv+LVFU(hcdalt[indx+2]));
					vcdv = LVF( vcd[indx] );
					vcdvarv = threev*(SQRV(LVF(vcd[indx-v2]))+SQRV(vcdv)+SQRV(LVF(vcd[indx+v2])))-SQRV(LVF(vcd[indx-v2])+vcdv+LVF(vcd[indx+v2]));
					vcdaltv = LVF( vcdalt[indx] );
					vcdaltvarv = threev*(SQRV(LVF(vcdalt[indx-v2]))+SQRV(vcdaltv)+SQRV(LVF(vcdalt[indx+v2])))-SQRV(LVF(vcdalt[indx-v2])+vcdaltv+LVF(vcdalt[indx+v2]));
					//choose the smallest variance; this yields a smoother interpolation
					hcdv = vself( vmaskf_lt( hcdaltvarv, hcdvarv ), hcdaltv, hcdv);
					vcdv = vself( vmaskf_lt( vcdaltvarv, vcdvarv ), vcdaltv, vcdv);

					Ginthv = sgnv * hcdv + LVF( cfa[indx] );
					temp2v = sgn3v * hcdv;
					hwtv = onev + temp2v / ( epsv + Ginthv + LVF( cfa[indx]));
					hcdmask = vmaskf_gt( hcdv, zerov );
					hcdoldv = hcdv;
					tempv = nsgnv * (LVF(cfa[indx]) - ULIMV( Ginthv, LVFU(cfa[indx-1]), LVFU(cfa[indx+1]) ));
					hcdv = vself( vmaskf_lt( (temp2v), -(LVF(cfa[indx])+Ginthv)), tempv, hwtv*hcdv + (onev - hwtv)*tempv);
					hcdv = vself( hcdmask, hcdv, hcdoldv );
					hcdv = vself( vmaskf_gt( Ginthv, clip_ptv), tempv, hcdv);
					_mm_store_ps( &hcdnew[indx], hcdv);

					Gintvv = sgnv * vcdv + LVF( cfa[indx] );
					temp2v = sgn3v * vcdv;
					vwtv = onev + temp2v / ( epsv + Gintvv + LVF( cfa[indx]));
					vcdmask = vmaskf_gt( vcdv, zerov );
					vcdoldv = vcdv;
					tempv = nsgnv * (LVF(cfa[indx]) - ULIMV( Gintvv, LVF(cfa[indx-v1]), LVF(cfa[indx+v1]) ));
					vcdv = vself( vmaskf_lt( (temp2v), -(LVF(cfa[indx])+Gintvv)), tempv, vwtv*vcdv + (onev - vwtv)*tempv);
					vcdv = vself( vcdmask, vcdv, vcdoldv );
					vcdv = vself( vmaskf_gt( Gintvv, clip_ptv), tempv, vcdv);
					_mm_store_ps( &vcdnew[indx], vcdv);
					_mm_storeu_ps(&cddiffsq[indx], SQRV(vcdv-hcdv));
				}

			}
			for (rr=4; rr<rr1-4; rr++)
				for (cc=4,indx=rr*TS+cc; cc<cc1-4; cc++,indx++) {
					hcd[indx] = hcdnew[indx];
					vcd[indx] = vcdnew[indx];
				}

#else

			//t1_cdvar = clock();
			for (rr=4; rr<rr1-4; rr++) {
				//for (cc=4+(FC(rr,2)&1),indx=rr*TS+cc,c=FC(rr,cc); cc<cc1-4; cc+=2,indx+=2) {
				for (cc=4,indx=rr*TS+cc,c=FC(rr,cc)&1; cc<cc1-4; cc++,indx++) {
					hcdvar =3.0f*(SQR(hcd[indx-2])+SQR(hcd[indx])+SQR(hcd[indx+2]))-SQR(hcd[indx-2]+hcd[indx]+hcd[indx+2]);
					hcdaltvar =3.0f*(SQR(hcdalt[indx-2])+SQR(hcdalt[indx])+SQR(hcdalt[indx+2]))-SQR(hcdalt[indx-2]+hcdalt[indx]+hcdalt[indx+2]);
					vcdvar =3.0f*(SQR(vcd[indx-v2])+SQR(vcd[indx])+SQR(vcd[indx+v2]))-SQR(vcd[indx-v2]+vcd[indx]+vcd[indx+v2]);
					vcdaltvar =3.0f*(SQR(vcdalt[indx-v2])+SQR(vcdalt[indx])+SQR(vcdalt[indx+v2]))-SQR(vcdalt[indx-v2]+vcdalt[indx]+vcdalt[indx+v2]);
					//choose the smallest variance; this yields a smoother interpolation
					if (hcdaltvar<hcdvar) hcd[indx]=hcdalt[indx];
					if (vcdaltvar<vcdvar) vcd[indx]=vcdalt[indx];

					//bound the interpolation in regions of high saturation
					if (c) {//G site
						Ginth = -hcd[indx]+cfa[indx];//R or B
						Gintv = -vcd[indx]+cfa[indx];//B or R

						if (hcd[indx]>0) {
							if (3.0f*hcd[indx] > (Ginth+cfa[indx])) {
								hcd[indx]=-ULIM(Ginth,cfa[indx-1],cfa[indx+1])+cfa[indx];
							} else {
								hwt = 1.0f -3.0f*hcd[indx]/(eps+Ginth+cfa[indx]);
								hcd[indx]=hwt*hcd[indx] + (1.0f-hwt)*(-ULIM(Ginth,cfa[indx-1],cfa[indx+1])+cfa[indx]);
							}
						}
						if (vcd[indx]>0) {
							if (3.0f*vcd[indx] > (Gintv+cfa[indx])) {
								vcd[indx]=-ULIM(Gintv,cfa[indx-v1],cfa[indx+v1])+cfa[indx];
							} else {
								vwt = 1.0f -3.0f*vcd[indx]/(eps+Gintv+cfa[indx]);
								vcd[indx]=vwt*vcd[indx] + (1.0f-vwt)*(-ULIM(Gintv,cfa[indx-v1],cfa[indx+v1])+cfa[indx]);
							}
						}

						if (Ginth > clip_pt) hcd[indx]=-ULIM(Ginth,cfa[indx-1],cfa[indx+1])+cfa[indx];//for RT implementation
						if (Gintv > clip_pt) vcd[indx]=-ULIM(Gintv,cfa[indx-v1],cfa[indx+v1])+cfa[indx];
						//if (Ginth > pre_mul[c]) hcd[indx]=-ULIM(Ginth,cfa[indx-1],cfa[indx+1])+cfa[indx];//for dcraw implementation
						//if (Gintv > pre_mul[c]) vcd[indx]=-ULIM(Gintv,cfa[indx-v1],cfa[indx+v1])+cfa[indx];
						
					} else {//R or B site

						Ginth = hcd[indx]+cfa[indx];//interpolated G
						Gintv = vcd[indx]+cfa[indx];

						if (hcd[indx]<0) {
							if (3.0f*hcd[indx] < -(Ginth+cfa[indx])) {
								hcd[indx]=ULIM(Ginth,cfa[indx-1],cfa[indx+1])-cfa[indx];
							} else {
								hwt = 1.0f +3.0f*hcd[indx]/(eps+Ginth+cfa[indx]);
								hcd[indx]=hwt*hcd[indx] + (1.0f-hwt)*(ULIM(Ginth,cfa[indx-1],cfa[indx+1])-cfa[indx]);
							}
						}
						if (vcd[indx]<0) {
							if (3.0f*vcd[indx] < -(Gintv+cfa[indx])) {
								vcd[indx]=ULIM(Gintv,cfa[indx-v1],cfa[indx+v1])-cfa[indx];
							} else {
								vwt = 1.0f +3.0f*vcd[indx]/(eps+Gintv+cfa[indx]);
								vcd[indx]=vwt*vcd[indx] + (1.0f-vwt)*(ULIM(Gintv,cfa[indx-v1],cfa[indx+v1])-cfa[indx]);
							}
						}

						if (Ginth > clip_pt) hcd[indx]=ULIM(Ginth,cfa[indx-1],cfa[indx+1])-cfa[indx];//for RT implementation
						if (Gintv > clip_pt) vcd[indx]=ULIM(Gintv,cfa[indx-v1],cfa[indx+v1])-cfa[indx];
						//if (Ginth > pre_mul[c]) hcd[indx]=ULIM(Ginth,cfa[indx-1],cfa[indx+1])-cfa[indx];//for dcraw implementation
						//if (Gintv > pre_mul[c]) vcd[indx]=ULIM(Gintv,cfa[indx-v1],cfa[indx+v1])-cfa[indx];
						cddiffsq[indx] = SQR(vcd[indx]-hcd[indx]);
					}
					c = !c;

//					cddiffsq[indx] = SQR(vcd[indx]-hcd[indx]);
				}
			}
#endif

			for (rr=6; rr<rr1-6; rr++)
				for (cc=6+(FC(rr,2)&1),indx=rr*TS+cc; cc<cc1-6; cc+=2,indx+=2) {

					//compute color difference variances in cardinal directions

					uave = vcd[indx]+vcd[indx-v1]+vcd[indx-v2]+vcd[indx-v3];
					dave = vcd[indx]+vcd[indx+v1]+vcd[indx+v2]+vcd[indx+v3];
					lave = (hcd[indx]+hcd[indx-1]+hcd[indx-2]+hcd[indx-3]);
					rave = (hcd[indx]+hcd[indx+1]+hcd[indx+2]+hcd[indx+3]);

					Dgrbvvaru = SQR(vcd[indx]-uave)+SQR(vcd[indx-v1]-uave)+SQR(vcd[indx-v2]-uave)+SQR(vcd[indx-v3]-uave);
					Dgrbvvard = SQR(vcd[indx]-dave)+SQR(vcd[indx+v1]-dave)+SQR(vcd[indx+v2]-dave)+SQR(vcd[indx+v3]-dave);
					Dgrbhvarl = SQR(hcd[indx]-lave)+SQR(hcd[indx-1]-lave)+SQR(hcd[indx-2]-lave)+SQR(hcd[indx-3]-lave);
					Dgrbhvarr = SQR(hcd[indx]-rave)+SQR(hcd[indx+1]-rave)+SQR(hcd[indx+2]-rave)+SQR(hcd[indx+3]-rave);

					hwt = dirwts1[indx-1]/(dirwts1[indx-1]+dirwts1[indx+1]);
					vwt = dirwts0[indx-v1]/(dirwts0[indx+v1]+dirwts0[indx-v1]);

					vcdvar = epssq+vwt*Dgrbvvard+(1.0f-vwt)*Dgrbvvaru;
					hcdvar = epssq+hwt*Dgrbhvarr+(1.0f-hwt)*Dgrbhvarl;

					//compute fluctuations in up/down and left/right interpolations of colors
					Dgrbvvaru = (dgintv[indx])+(dgintv[indx-v1])+(dgintv[indx-v2]);
					Dgrbvvard = (dgintv[indx])+(dgintv[indx+v1])+(dgintv[indx+v2]);
					Dgrbhvarl = (dginth[indx])+(dginth[indx-1])+(dginth[indx-2]);
					Dgrbhvarr = (dginth[indx])+(dginth[indx+1])+(dginth[indx+2]);

					vcdvar1 = epssq+vwt*Dgrbvvard+(1.0f-vwt)*Dgrbvvaru;
					hcdvar1 = epssq+hwt*Dgrbhvarr+(1.0f-hwt)*Dgrbhvarl;

					//determine adaptive weights for G interpolation
					varwt=hcdvar/(vcdvar+hcdvar);
					diffwt=hcdvar1/(vcdvar1+hcdvar1);

					//if both agree on interpolation direction, choose the one with strongest directional discrimination;
					//otherwise, choose the u/d and l/r difference fluctuation weights
					if ((0.5-varwt)*(0.5-diffwt)>0 && fabsf(0.5f-diffwt)<fabsf(0.5f-varwt)) {hvwt[indx>>1]=varwt;} else {hvwt[indx>>1]=diffwt;}

					//hvwt[indx]=varwt;
				}
			//t2_cdvar += clock() - t1_cdvar;

			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			// Nyquist test
			//t1_nyqtest = clock();

			for (rr=6; rr<rr1-6; rr++)
				for (cc=6+(FC(rr,2)&1),indx=rr*TS+cc; cc<cc1-6; cc+=2,indx+=2) {

					//nyquist texture test: ask if difference of vcd compared to hcd is larger or smaller than RGGB gradients
					nyqtest = (gaussodd[0]*cddiffsq[indx]+
							   gaussodd[1]*(cddiffsq[indx-m1]+cddiffsq[indx+p1]+
											cddiffsq[indx-p1]+cddiffsq[indx+m1])+
							   gaussodd[2]*(cddiffsq[indx-v2]+cddiffsq[indx-2]+
											cddiffsq[indx+2]+cddiffsq[indx+v2])+
							   gaussodd[3]*(cddiffsq[indx-m2]+cddiffsq[indx+p2]+
											cddiffsq[indx-p2]+cddiffsq[indx+m2]));

					nyqtest -= nyqthresh*(gaussgrad[0]*(delhsq[indx]+delvsq[indx])+
										  gaussgrad[1]*(delhsq[indx-v1]+delvsq[indx-v1]+delhsq[indx+1]+delvsq[indx+1]+
														delhsq[indx-1]+delvsq[indx-1]+delhsq[indx+v1]+delvsq[indx+v1])+
										  gaussgrad[2]*(delhsq[indx-m1]+delvsq[indx-m1]+delhsq[indx+p1]+delvsq[indx+p1]+
														delhsq[indx-p1]+delvsq[indx-p1]+delhsq[indx+m1]+delvsq[indx+m1])+
										  gaussgrad[3]*(delhsq[indx-v2]+delvsq[indx-v2]+delhsq[indx-2]+delvsq[indx-2]+
														delhsq[indx+2]+delvsq[indx+2]+delhsq[indx+v2]+delvsq[indx+v2])+
										  gaussgrad[4]*(delhsq[indx-2*TS-1]+delvsq[indx-2*TS-1]+delhsq[indx-2*TS+1]+delvsq[indx-2*TS+1]+
														delhsq[indx-TS-2]+delvsq[indx-TS-2]+delhsq[indx-TS+2]+delvsq[indx-TS+2]+
														delhsq[indx+TS-2]+delvsq[indx+TS-2]+delhsq[indx+TS+2]+delvsq[indx-TS+2]+
														delhsq[indx+2*TS-1]+delvsq[indx+2*TS-1]+delhsq[indx+2*TS+1]+delvsq[indx+2*TS+1])+
										  gaussgrad[5]*(delhsq[indx-m2]+delvsq[indx-m2]+delhsq[indx+p2]+delvsq[indx+p2]+
														delhsq[indx-p2]+delvsq[indx-p2]+delhsq[indx+m2]+delvsq[indx+m2]));


					if (nyqtest>0) {nyquist[indx>>1]=1;}//nyquist=1 for nyquist region
				}
			unsigned int nyquisttemp;
			for (rr=8; rr<rr1-8; rr++)
				for (cc=8+(FC(rr,2)&1),indx=rr*TS+cc; cc<cc1-8; cc+=2,indx+=2) {

					nyquisttemp=(nyquist[(indx-v2)>>1]+nyquist[(indx-m1)>>1]+nyquist[(indx+p1)>>1]+
							nyquist[(indx-2)>>1]+nyquist[indx>>1]+nyquist[(indx+2)>>1]+
							nyquist[(indx-p1)>>1]+nyquist[(indx+m1)>>1]+nyquist[(indx+v2)>>1]);
					//if most of your neighbors are named Nyquist, it's likely that you're one too
					if (nyquisttemp>4) nyquist[indx>>1]=1;
					//or not
					if (nyquisttemp<4) nyquist[indx>>1]=0;
				}

			//t2_nyqtest += clock() - t1_nyqtest;
			// end of Nyquist test
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%




			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			// in areas of Nyquist texture, do area interpolation
			//t1_areainterp = clock();
			for (rr=8; rr<rr1-8; rr++)
				for (cc=8+(FC(rr,2)&1),indx=rr*TS+cc; cc<cc1-8; cc+=2,indx+=2) {

					if (nyquist[indx>>1]) {
						// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
						// area interpolation

						sumh=sumv=sumsqh=sumsqv=areawt=0;
						for (i=-6; i<7; i+=2)
							for (j=-6; j<7; j+=2) {
								indx1=(rr+i)*TS+cc+j;
								if (nyquist[indx1>>1]) {
									sumh += cfa[indx1]-xdiv2f(cfa[indx1-1]+cfa[indx1+1]);
									sumv += cfa[indx1]-xdiv2f(cfa[indx1-v1]+cfa[indx1+v1]);
									sumsqh += xdiv2f(SQR(cfa[indx1]-cfa[indx1-1])+SQR(cfa[indx1]-cfa[indx1+1]));
									sumsqv += xdiv2f(SQR(cfa[indx1]-cfa[indx1-v1])+SQR(cfa[indx1]-cfa[indx1+v1]));
									areawt +=1;
								}
							}

						//horizontal and vertical color differences, and adaptive weight
						hcdvar=epssq+fabsf(areawt*sumsqh-sumh*sumh);
						vcdvar=epssq+fabsf(areawt*sumsqv-sumv*sumv);
						hvwt[indx>>1]=hcdvar/(vcdvar+hcdvar);

						// end of area interpolation
						// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

					}
				}
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			//t2_areainterp += clock() - t1_areainterp;

			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			//populate G at R/B sites
			for (rr=8; rr<rr1-8; rr++)
				for (cc=8+(FC(rr,2)&1),indx=rr*TS+cc; cc<cc1-8; cc+=2,indx+=2) {

					//first ask if one gets more directional discrimination from nearby B/R sites
					hvwtalt = xdivf(hvwt[(indx-m1)>>1]+hvwt[(indx+p1)>>1]+hvwt[(indx-p1)>>1]+hvwt[(indx+m1)>>1],2);
//					hvwtalt = 0.25*(hvwt[(indx-m1)>>1]+hvwt[(indx+p1)>>1]+hvwt[(indx-p1)>>1]+hvwt[(indx+m1)>>1]);
//					vo=fabsf(0.5-hvwt[indx>>1]);
//					ve=fabsf(0.5-hvwtalt);
					if (fabsf(0.5f-hvwt[indx>>1])<fabsf(0.5f-hvwtalt)) {hvwt[indx>>1]=hvwtalt;}//a better result was obtained from the neighbors
//					if (vo<ve) {hvwt[indx>>1]=hvwtalt;}//a better result was obtained from the neighbors



					Dgrb[0][indx>>1] = (hcd[indx]*(1.0f-hvwt[indx>>1]) + vcd[indx]*hvwt[indx>>1]);//evaluate color differences
					//if (hvwt[indx]<0.5) Dgrb[indx][0]=hcd[indx];
					//if (hvwt[indx]>0.5) Dgrb[indx][0]=vcd[indx];
					rgbgreen[indx] = cfa[indx] + Dgrb[0][indx>>1];//evaluate G (finally!)

					//local curvature in G (preparation for nyquist refinement step)
					if (nyquist[indx>>1]) {
						Dgrb2[indx>>1].h = SQR(rgbgreen[indx] - xdiv2f(rgbgreen[indx-1]+rgbgreen[indx+1]));
						Dgrb2[indx>>1].v = SQR(rgbgreen[indx] - xdiv2f(rgbgreen[indx-v1]+rgbgreen[indx+v1]));
					} else {
						Dgrb2[indx>>1].h = Dgrb2[indx>>1].v = 0;
					}
				}

			//end of standard interpolation
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%


			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			// refine Nyquist areas using G curvatures

			for (rr=8; rr<rr1-8; rr++)
				for (cc=8+(FC(rr,2)&1),indx=rr*TS+cc; cc<cc1-8; cc+=2,indx+=2) {

					if (nyquist[indx>>1]) {
						//local averages (over Nyquist pixels only) of G curvature squared
						gvarh = epssq + (gquinc[0]*Dgrb2[indx>>1].h+
									   gquinc[1]*(Dgrb2[(indx-m1)>>1].h+Dgrb2[(indx+p1)>>1].h+Dgrb2[(indx-p1)>>1].h+Dgrb2[(indx+m1)>>1].h)+
									   gquinc[2]*(Dgrb2[(indx-v2)>>1].h+Dgrb2[(indx-2)>>1].h+Dgrb2[(indx+2)>>1].h+Dgrb2[(indx+v2)>>1].h)+
									   gquinc[3]*(Dgrb2[(indx-m2)>>1].h+Dgrb2[(indx+p2)>>1].h+Dgrb2[(indx-p2)>>1].h+Dgrb2[(indx+m2)>>1].h));
						gvarv = epssq + (gquinc[0]*Dgrb2[indx>>1].v+
									   gquinc[1]*(Dgrb2[(indx-m1)>>1].v+Dgrb2[(indx+p1)>>1].v+Dgrb2[(indx-p1)>>1].v+Dgrb2[(indx+m1)>>1].v)+
									   gquinc[2]*(Dgrb2[(indx-v2)>>1].v+Dgrb2[(indx-2)>>1].v+Dgrb2[(indx+2)>>1].v+Dgrb2[(indx+v2)>>1].v)+
									   gquinc[3]*(Dgrb2[(indx-m2)>>1].v+Dgrb2[(indx+p2)>>1].v+Dgrb2[(indx-p2)>>1].v+Dgrb2[(indx+m2)>>1].v));
						//use the results as weights for refined G interpolation
						Dgrb[0][indx>>1] = (hcd[indx]*gvarv + vcd[indx]*gvarh)/(gvarv+gvarh);
						rgbgreen[indx] = cfa[indx] + Dgrb[0][indx>>1];
					}
				}

			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

			//t1_diag = clock();
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			// diagonal interpolation correction

			for (rr=8; rr<rr1-8; rr++)
				for (cc=8+(FC(rr,2)&1),indx=rr*TS+cc,indx1=indx>>1; cc<cc1-8; cc+=2,indx+=2,indx1++) {

/*
					rbvarp = epssq + (gausseven[0]*(Dgrbsq1[indx-v1].p+Dgrbsq1[indx-1].p+Dgrbsq1[indx+1].p+Dgrbsq1[indx+v1].p) +
									gausseven[1]*(Dgrbsq1[indx-v2-1].p+Dgrbsq1[indx-v2+1].p+Dgrbsq1[indx-2-v1].p+Dgrbsq1[indx+2-v1].p+
												  Dgrbsq1[indx-2+v1].p+Dgrbsq1[indx+2+v1].p+Dgrbsq1[indx+v2-1].p+Dgrbsq1[indx+v2+1].p));
					rbvarm = epssq + (gausseven[0]*(Dgrbsq1[indx-v1].m+Dgrbsq1[indx-1].m+Dgrbsq1[indx+1].m+Dgrbsq1[indx+v1].m) +
									gausseven[1]*(Dgrbsq1[indx-v2-1].m+Dgrbsq1[indx-v2+1].m+Dgrbsq1[indx-2-v1].m+Dgrbsq1[indx+2-v1].m+
												  Dgrbsq1[indx-2+v1].m+Dgrbsq1[indx+2+v1].m+Dgrbsq1[indx+v2-1].m+Dgrbsq1[indx+v2+1].m));
*/
					// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

					//diagonal color ratios
					crse=xmul2f(cfa[indx+m1])/(eps+cfa[indx]+(cfa[indx+m2]));
					crnw=xmul2f(cfa[indx-m1])/(eps+cfa[indx]+(cfa[indx-m2]));
					crne=xmul2f(cfa[indx+p1])/(eps+cfa[indx]+(cfa[indx+p2]));
					crsw=xmul2f(cfa[indx-p1])/(eps+cfa[indx]+(cfa[indx-p2]));

					//assign B/R at R/B sites
					if (fabsf(1.0f-crse)<arthresh) {rbse=cfa[indx]*crse;}//use this if more precise diag interp is necessary
					else {rbse=(cfa[indx+m1])+xdiv2f(cfa[indx]-cfa[indx+m2]);}
					if (fabsf(1.0f-crnw)<arthresh) {rbnw=cfa[indx]*crnw;}
					else {rbnw=(cfa[indx-m1])+xdiv2f(cfa[indx]-cfa[indx-m2]);}
					if (fabsf(1.0f-crne)<arthresh) {rbne=cfa[indx]*crne;}
					else {rbne=(cfa[indx+p1])+xdiv2f(cfa[indx]-cfa[indx+p2]);}
					if (fabsf(1.0f-crsw)<arthresh) {rbsw=cfa[indx]*crsw;}
					else {rbsw=(cfa[indx-p1])+xdiv2f(cfa[indx]-cfa[indx-p2]);}

					wtse= eps+delm[indx1]+delm[(indx+m1)>>1]+delm[(indx+m2)>>1];//same as for wtu,wtd,wtl,wtr
					wtnw= eps+delm[indx1]+delm[(indx-m1)>>1]+delm[(indx-m2)>>1];
					wtne= eps+delp[indx1]+delp[(indx+p1)>>1]+delp[(indx+p2)>>1];
					wtsw= eps+delp[indx1]+delp[(indx-p1)>>1]+delp[(indx-p2)>>1];


					rb[indx1].m = (wtse*rbnw+wtnw*rbse)/(wtse+wtnw);
					rb[indx1].p = (wtne*rbsw+wtsw*rbne)/(wtne+wtsw);
/*
					rbvarp = epssq + (gausseven[0]*(Dgrbsq1[indx-v1].p+Dgrbsq1[indx-1].p+Dgrbsq1[indx+1].p+Dgrbsq1[indx+v1].p) +
									gausseven[1]*(Dgrbsq1[indx-v2-1].p+Dgrbsq1[indx-v2+1].p+Dgrbsq1[indx-2-v1].p+Dgrbsq1[indx+2-v1].p+
												  Dgrbsq1[indx-2+v1].p+Dgrbsq1[indx+2+v1].p+Dgrbsq1[indx+v2-1].p+Dgrbsq1[indx+v2+1].p));
*/
					rbvarm = epssq + (gausseven[0]*(Dgrbsq1[(indx-v1)>>1].m+Dgrbsq1[(indx-1)>>1].m+Dgrbsq1[(indx+1)>>1].m+Dgrbsq1[(indx+v1)>>1].m) +
									gausseven[1]*(Dgrbsq1[(indx-v2-1)>>1].m+Dgrbsq1[(indx-v2+1)>>1].m+Dgrbsq1[(indx-2-v1)>>1].m+Dgrbsq1[(indx+2-v1)>>1].m+
												  Dgrbsq1[(indx-2+v1)>>1].m+Dgrbsq1[(indx+2+v1)>>1].m+Dgrbsq1[(indx+v2-1)>>1].m+Dgrbsq1[(indx+v2+1)>>1].m));
					pmwt[indx1] = rbvarm/((epssq + (gausseven[0]*(Dgrbsq1[(indx-v1)>>1].p+Dgrbsq1[(indx-1)>>1].p+Dgrbsq1[(indx+1)>>1].p+Dgrbsq1[(indx+v1)>>1].p) +
									gausseven[1]*(Dgrbsq1[(indx-v2-1)>>1].p+Dgrbsq1[(indx-v2+1)>>1].p+Dgrbsq1[(indx-2-v1)>>1].p+Dgrbsq1[(indx+2-v1)>>1].p+
												  Dgrbsq1[(indx-2+v1)>>1].p+Dgrbsq1[(indx+2+v1)>>1].p+Dgrbsq1[(indx+v2-1)>>1].p+Dgrbsq1[(indx+v2+1)>>1].p)))+rbvarm);

					// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
					//bound the interpolation in regions of high saturation
					if (rb[indx1].p<cfa[indx]) {
						if (xmul2f(rb[indx1].p) < cfa[indx]) {
							rb[indx1].p = ULIM(rb[indx1].p ,cfa[indx-p1],cfa[indx+p1]);
						} else {
							pwt = xmul2f(cfa[indx]-rb[indx1].p)/(eps+rb[indx1].p+cfa[indx]);
							rb[indx1].p=pwt*rb[indx1].p + (1.0f-pwt)*ULIM(rb[indx1].p,cfa[indx-p1],cfa[indx+p1]);
						}
					}
					if (rb[indx1].m<cfa[indx]) {
						if (xmul2f(rb[indx1].m) < cfa[indx]) {
							rb[indx1].m = ULIM(rb[indx1].m ,cfa[indx-m1],cfa[indx+m1]);
						} else {
							mwt = xmul2f(cfa[indx]-rb[indx1].m)/(eps+rb[indx1].m+cfa[indx]);
							rb[indx1].m=mwt*rb[indx1].m + (1.0f-mwt)*ULIM(rb[indx1].m,cfa[indx-m1],cfa[indx+m1]);
						}
					}

					if (rb[indx1].p > clip_pt) rb[indx1].p=ULIM(rb[indx1].p,cfa[indx-p1],cfa[indx+p1]);//for RT implementation
					if (rb[indx1].m > clip_pt) rb[indx1].m=ULIM(rb[indx1].m,cfa[indx-m1],cfa[indx+m1]);
					//c=2-FC(rr,cc);//for dcraw implementation
					//if (rbp[indx] > pre_mul[c]) rbp[indx]=ULIM(rbp[indx],cfa[indx-p1],cfa[indx+p1]);
					//if (rbm[indx] > pre_mul[c]) rbm[indx]=ULIM(rbm[indx],cfa[indx-m1],cfa[indx+m1]);
					// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

					//rbint[indx] = 0.5*(cfa[indx] + (rbp*rbvarm+rbm*rbvarp)/(rbvarp+rbvarm));//this is R+B, interpolated
				}


			for (rr=10; rr<rr1-10; rr++)
				for (cc=10+(FC(rr,2)&1),indx=rr*TS+cc,indx1=indx>>1; cc<cc1-10; cc+=2,indx+=2,indx1++) {

					//first ask if one gets more directional discrimination from nearby B/R sites
					pmwtalt = xdivf(pmwt[(indx-m1)>>1]+pmwt[(indx+p1)>>1]+pmwt[(indx-p1)>>1]+pmwt[(indx+m1)>>1],2);
//					vo=fabsf(0.5-pmwt[indx1]);
//					ve=fabsf(0.5-pmwtalt);
					if (fabsf(0.5f-pmwt[indx1])<fabsf(0.5f-pmwtalt)) {pmwt[indx1]=pmwtalt;}//a better result was obtained from the neighbors
					
//					if (vo<ve) {pmwt[indx1]=pmwtalt;}//a better result was obtained from the neighbors
					rbint[indx1] = xdiv2f(cfa[indx] + rb[indx1].m*(1.0f-pmwt[indx1]) + rb[indx1].p*pmwt[indx1]);//this is R+B, interpolated
				}

			for (rr=12; rr<rr1-12; rr++)
				for (cc=12+(FC(rr,2)&1),indx=rr*TS+cc,indx1=indx>>1; cc<cc1-12; cc+=2,indx+=2,indx1++) {

					if (fabsf(0.5f-pmwt[indx>>1])<fabsf(0.5f-hvwt[indx>>1]) ) continue;

					//now interpolate G vertically/horizontally using R+B values
					//unfortunately, since G interpolation cannot be done diagonally this may lead to color shifts
					//color ratios for G interpolation

					cru = cfa[indx-v1]*2.0/(eps+rbint[indx1]+rbint[(indx1-v1)]);
					crd = cfa[indx+v1]*2.0/(eps+rbint[indx1]+rbint[(indx1+v1)]);
					crl = cfa[indx-1]*2.0/(eps+rbint[indx1]+rbint[(indx1-1)]);
					crr = cfa[indx+1]*2.0/(eps+rbint[indx1]+rbint[(indx1+1)]);

					//interpolated G via adaptive ratios or Hamilton-Adams in each cardinal direction
					if (fabsf(1.0f-cru)<arthresh) {gu=rbint[indx1]*cru;}
					else {gu=cfa[indx-v1]+xdiv2f(rbint[indx1]-rbint[(indx1-v1)]);}
					if (fabsf(1.0f-crd)<arthresh) {gd=rbint[indx1]*crd;}
					else {gd=cfa[indx+v1]+xdiv2f(rbint[indx1]-rbint[(indx1+v1)]);}
					if (fabsf(1.0f-crl)<arthresh) {gl=rbint[indx1]*crl;}
					else {gl=cfa[indx-1]+xdiv2f(rbint[indx1]-rbint[(indx1-1)]);}
					if (fabsf(1.0f-crr)<arthresh) {gr=rbint[indx1]*crr;}
					else {gr=cfa[indx+1]+xdiv2f(rbint[indx1]-rbint[(indx1+1)]);}

					//gu=rbint[indx]*cru;
					//gd=rbint[indx]*crd;
					//gl=rbint[indx]*crl;
					//gr=rbint[indx]*crr;

					//interpolated G via adaptive weights of cardinal evaluations
					Gintv = (dirwts0[indx-v1]*gd+dirwts0[indx+v1]*gu)/(dirwts0[indx+v1]+dirwts0[indx-v1]);
					Ginth = (dirwts1[indx-1]*gr+dirwts1[indx+1]*gl)/(dirwts1[indx-1]+dirwts1[indx+1]);

					// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
					//bound the interpolation in regions of high saturation
					if (Gintv<rbint[indx1]) {
						if (2*Gintv < rbint[indx1]) {
							Gintv = ULIM(Gintv ,cfa[indx-v1],cfa[indx+v1]);
						} else {
							vwt = 2.0*(rbint[indx1]-Gintv)/(eps+Gintv+rbint[indx1]);
							Gintv=vwt*Gintv + (1.0f-vwt)*ULIM(Gintv,cfa[indx-v1],cfa[indx+v1]);
						}
					}
					if (Ginth<rbint[indx1]) {
						if (2*Ginth < rbint[indx1]) {
							Ginth = ULIM(Ginth ,cfa[indx-1],cfa[indx+1]);
						} else {
							hwt = 2.0*(rbint[indx1]-Ginth)/(eps+Ginth+rbint[indx1]);
							Ginth=hwt*Ginth + (1.0f-hwt)*ULIM(Ginth,cfa[indx-1],cfa[indx+1]);
						}
					}

					if (Ginth > clip_pt) Ginth=ULIM(Ginth,cfa[indx-1],cfa[indx+1]);//for RT implementation
					if (Gintv > clip_pt) Gintv=ULIM(Gintv,cfa[indx-v1],cfa[indx+v1]);
					//c=FC(rr,cc);//for dcraw implementation
					//if (Ginth > pre_mul[c]) Ginth=ULIM(Ginth,cfa[indx-1],cfa[indx+1]);
					//if (Gintv > pre_mul[c]) Gintv=ULIM(Gintv,cfa[indx-v1],cfa[indx+v1]);
					// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

					rgbgreen[indx] = Ginth*(1.0f-hvwt[indx1]) + Gintv*hvwt[indx1];
					//rgb[indx][1] = 0.5*(rgb[indx][1]+0.25*(rgb[indx-v1][1]+rgb[indx+v1][1]+rgb[indx-1][1]+rgb[indx+1][1]));
					Dgrb[0][indx>>1] = rgbgreen[indx]-cfa[indx];

					//rgb[indx][2-FC(rr,cc)]=2*rbint[indx]-cfa[indx];
				}
			//end of diagonal interpolation correction
			//t2_diag += clock() - t1_diag;
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

			//t1_chroma = clock();
			//fancy chrominance interpolation
			//(ey,ex) is location of R site
			for (rr=13-ey; rr<rr1-12; rr+=2)
				for (cc=13-ex,indx=rr*TS+cc; cc<cc1-12; cc+=2,indx+=2) {//B coset
					Dgrb[1][indx>>1]=Dgrb[0][indx>>1];//split out G-B from G-R
					Dgrb[0][indx>>1]=0;
				}
			for (rr=14; rr<rr1-14; rr++)
				for (cc=14+(FC(rr,2)&1),indx=rr*TS+cc,c=1-FC(rr,cc)/2; cc<cc1-14; cc+=2,indx+=2) {
					wtnw=1.0f/(eps+fabsf(Dgrb[c][(indx-m1)>>1]-Dgrb[c][(indx+m1)>>1])+fabsf(Dgrb[c][(indx-m1)>>1]-Dgrb[c][(indx-m3)>>1])+fabsf(Dgrb[c][(indx+m1)>>1]-Dgrb[c][(indx-m3)>>1]));
					wtne=1.0f/(eps+fabsf(Dgrb[c][(indx+p1)>>1]-Dgrb[c][(indx-p1)>>1])+fabsf(Dgrb[c][(indx+p1)>>1]-Dgrb[c][(indx+p3)>>1])+fabsf(Dgrb[c][(indx-p1)>>1]-Dgrb[c][(indx+p3)>>1]));
					wtsw=1.0f/(eps+fabsf(Dgrb[c][(indx-p1)>>1]-Dgrb[c][(indx+p1)>>1])+fabsf(Dgrb[c][(indx-p1)>>1]-Dgrb[c][(indx+m3)>>1])+fabsf(Dgrb[c][(indx+p1)>>1]-Dgrb[c][(indx-p3)>>1]));
					wtse=1.0f/(eps+fabsf(Dgrb[c][(indx+m1)>>1]-Dgrb[c][(indx-m1)>>1])+fabsf(Dgrb[c][(indx+m1)>>1]-Dgrb[c][(indx-p3)>>1])+fabsf(Dgrb[c][(indx-m1)>>1]-Dgrb[c][(indx+m3)>>1]));

					//Dgrb[indx][c]=(wtnw*Dgrb[indx-m1][c]+wtne*Dgrb[indx+p1][c]+wtsw*Dgrb[indx-p1][c]+wtse*Dgrb[indx+m1][c])/(wtnw+wtne+wtsw+wtse);

					Dgrb[c][indx>>1]=(wtnw*(1.325f*Dgrb[c][(indx-m1)>>1]-0.175f*Dgrb[c][(indx-m3)>>1]-0.075f*Dgrb[c][(indx-m1-2)>>1]-0.075f*Dgrb[c][(indx-m1-v2)>>1] )+
								   wtne*(1.325f*Dgrb[c][(indx+p1)>>1]-0.175f*Dgrb[c][(indx+p3)>>1]-0.075f*Dgrb[c][(indx+p1+2)>>1]-0.075f*Dgrb[c][(indx+p1+v2)>>1] )+
								   wtsw*(1.325f*Dgrb[c][(indx-p1)>>1]-0.175f*Dgrb[c][(indx-p3)>>1]-0.075f*Dgrb[c][(indx-p1-2)>>1]-0.075f*Dgrb[c][(indx-p1-v2)>>1] )+
								   wtse*(1.325f*Dgrb[c][(indx+m1)>>1]-0.175f*Dgrb[c][(indx+m3)>>1]-0.075f*Dgrb[c][(indx+m1+2)>>1]-0.075f*Dgrb[c][(indx+m1+v2)>>1] ))/(wtnw+wtne+wtsw+wtse);
				}
			float	temp;
			for (rr=16; rr<rr1-16; rr++) {
				if((FC(rr,2)&1)==1)
					for (cc=16,indx=rr*TS+cc,row=rr+top; cc<cc1-16; cc+=2,indx++) {
						col = cc + left;
						temp = 	1.0f/((hvwt[(indx-v1)>>1])+(1.0f-hvwt[(indx+1)>>1])+(1.0f-hvwt[(indx-1)>>1])+(hvwt[(indx+v1)>>1]));
						red[row][col]=65535.0f*(rgbgreen[indx]-	((hvwt[(indx-v1)>>1])*Dgrb[0][(indx-v1)>>1]+(1.0f-hvwt[(indx+1)>>1])*Dgrb[0][(indx+1)>>1]+(1.0f-hvwt[(indx-1)>>1])*Dgrb[0][(indx-1)>>1]+(hvwt[(indx+v1)>>1])*Dgrb[0][(indx+v1)>>1])*
							temp);
						blue[row][col]=65535.0f*(rgbgreen[indx]- ((hvwt[(indx-v1)>>1])*Dgrb[1][(indx-v1)>>1]+(1.0f-hvwt[(indx+1)>>1])*Dgrb[1][(indx+1)>>1]+(1.0f-hvwt[(indx-1)>>1])*Dgrb[1][(indx-1)>>1]+(hvwt[(indx+v1)>>1])*Dgrb[1][(indx+v1)>>1])*
							temp);
						
						indx++;
						col++;
						red[row][col]=65535.0f*(rgbgreen[indx]-Dgrb[0][indx>>1]);
						blue[row][col]=65535.0f*(rgbgreen[indx]-Dgrb[1][indx>>1]);
					}
				else
					for (cc=16,indx=rr*TS+cc,row=rr+top; cc<cc1-16; cc+=2,indx++) {
						col = cc + left;
						red[row][col]=65535.0f*(rgbgreen[indx]-Dgrb[0][indx>>1]);
						blue[row][col]=65535.0f*(rgbgreen[indx]-Dgrb[1][indx>>1]);
						indx++;
						col++;
						temp = 	1.0f/((hvwt[(indx-v1)>>1])+(1.0f-hvwt[(indx+1)>>1])+(1.0f-hvwt[(indx-1)>>1])+(hvwt[(indx+v1)>>1]));
						red[row][col]=65535.0f*(rgbgreen[indx]-	((hvwt[(indx-v1)>>1])*Dgrb[0][(indx-v1)>>1]+(1.0f-hvwt[(indx+1)>>1])*Dgrb[0][(indx+1)>>1]+(1.0f-hvwt[(indx-1)>>1])*Dgrb[0][(indx-1)>>1]+(hvwt[(indx+v1)>>1])*Dgrb[0][(indx+v1)>>1])*
							temp);
						blue[row][col]=65535.0f*(rgbgreen[indx]- ((hvwt[(indx-v1)>>1])*Dgrb[1][(indx-v1)>>1]+(1.0f-hvwt[(indx+1)>>1])*Dgrb[1][(indx+1)>>1]+(1.0f-hvwt[(indx-1)>>1])*Dgrb[1][(indx-1)>>1]+(hvwt[(indx+v1)>>1])*Dgrb[1][(indx+v1)>>1])*
							temp);
						
					}
			}

			//t2_chroma += clock() - t1_chroma;

			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
			// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%


			// copy smoothed results back to image matrix
			for (rr=16; rr < rr1-16; rr++){
#ifdef __SSE2__
				for (row=rr+top, cc=16; cc < cc1-19; cc+=4) {
					_mm_storeu_ps(&green[row][cc + left], LVF(rgbgreen[rr*TS+cc]) * d65535v);
				}
#else
				for (row=rr+top, cc=16; cc < cc1-16; cc++) {
					col = cc + left;
					indx=rr*TS+cc;
					green[row][col] = ((65535.0f*rgbgreen[indx]));

					//for dcraw implementation
					//for (c=0; c<3; c++){
					//	image[indx][c] = CLIP((int)(65535.0f*rgb[rr*TS+cc][c] + 0.5f));
					//}
				}
#endif
			}
			//end of main loop

			// clean up
			//free(buffer);
			/*progress+=(double)((TS-32)*(TS-32))/(height*width);
			if (progress>1.0)
			{
				progress=1.0;
			}
			if(plistener) plistener->setProgress(progress);*/
		}

	// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

#pragma omp for schedule(dynamic) collapse(2) 
    for (top=winy; top < winy+winh; top += 1) 
        for (left=winx; left < winx+winw; left += 1) {
            float r = red[top][left]/64.0f;
            if (r<1.0f) r = 1.0f;
            r = exp2(r)-15.0f;
            if (r<0.0f) r = 0.0f;
            red[top][left] = r;
            float g = green[top][left]/64.0f;
            if (g<1.0f) g = 1.0f;
            g = exp2(g)-15.0f;
            if (g<0.0f) g = 0.0f;
            green[top][left] = g;
            float b = blue[top][left]/64.0f;
            if (b<1.0f) b = 1.0f;
            b = exp2(b)-15.0f;
            if (b<0.0f) b = 0.0f;
            blue[top][left] = b;
    } 

	// clean up
	free(buffer);
}
	// done

#undef TS
//t2 = clock() - t1;
//printf("Amaze took %.2f s\n", (double)t2 / CLOCKS_PER_SEC);

}
