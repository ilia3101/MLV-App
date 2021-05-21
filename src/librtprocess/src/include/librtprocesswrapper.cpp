/*!
 * \file librtprocesswrapper.cpp
 * \author masc4ii
 * \copyright 2021
 * \brief A C wrapper for librtprocesswrapper with simplyfied interface
 */

#include "librtprocess.h"

#if defined(__linux)
#include <inttypes.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

//Module globals
bool myFunc( double val ){ return true; }
unsigned cfa[2][2] = { { 0, 1 }, { 1, 2 } };
const std::function<bool(double)> sPC = myFunc;
size_t chunkSize = 2;
bool measure = false;

void lrtpLmmseDemosaic(float **rawData, float **red, float **green, float **blue, int width, int height)
{
    lmmse_demosaic( width, height, rawData, red, green, blue, cfa, sPC, 1 );
}

void lrtpIgvDemosaic(float **rawData, float **red, float **green, float **blue, int width, int height)
{
    igv_demosaic( width, height, rawData, red, green, blue, cfa, sPC );
}

void lrtpRcdDemosaic(float **rawData, float **red, float **green, float **blue, int width, int height)
{
    rcd_demosaic( width, height, rawData, red, green, blue, cfa, sPC, chunkSize, measure, true );
}

void lrtpAmazeDemosaic(float **rawData, float **red, float **green, float **blue, int width, int height)
{
    amaze_demosaic( width, height, 0, 0, width, height, rawData, red, green, blue, cfa, sPC, 1.0, 0, 1.0, 1.0, chunkSize, measure );
}

void lrtpAhdDemosaic(float **rawData, float **red, float **green, float **blue, int width, int height, double camMatrix[9])
{
    float rgbCam[3][4];
    for( int i = 0; i < 3; i++ )
        for( int j = 0; j < 3; j++ )
            rgbCam[i][j] = camMatrix[i*3+j];
    ahd_demosaic( width, height, rawData, red, green, blue, cfa, rgbCam, sPC );
}

void lrtpBayerfastDemosaic(float **rawData, float **red, float **green, float **blue, int width, int height)
{
    bayerfast_demosaic( width, height, rawData, red, green, blue, cfa, sPC, 1.0 );
}

void lrtpDcbDemosaic(float **rawData, float **red, float **green, float **blue, int width, int height)
{
    dcb_demosaic( width, height, rawData, red, green, blue, cfa, sPC, 2, true );
}

void lrtpHphdDemosaic(float **rawData, float **red, float **green, float **blue, int width, int height)
{
    hphd_demosaic( width, height, rawData, red, green, blue, cfa, sPC );
}

void lrtpHLRecovery(float **red, float **green, float **blue, int width, int height, const float chmax[], const float clmax[])
{
    HLRecovery_inpaint( width, height, red, green, blue, chmax, clmax, sPC );
}

void lrtpCaCorrect(float **rawData, int winx, int winy, int winw, int winh, const uint8_t autoCA, int autoIterations, const double cared, const double cablue, uint8_t avoidColourshift)
{
    size_t autoIt = autoIterations;
    double fitParams[2][2][16];
    CA_correct( winx, winy, winw, winh, autoCA, autoIt, cared, cablue, avoidColourshift, rawData, rawData, cfa, sPC, fitParams, false, 1.0f, 1.0f, chunkSize, measure );
}

#ifdef __cplusplus
}
#endif
