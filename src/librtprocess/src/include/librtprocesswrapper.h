/*!
 * \file librtprocesswrapper.h
 * \author masc4ii
 * \copyright 2021
 * \brief A C wrapper for librtprocesswrapper with simplyfied interface
 */

#ifndef LIBRTPROCESSWRAPPER_H
#define LIBRTPROCESSWRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void lrtpLmmseDemosaic( float ** restrict rawData, float ** restrict red, float ** restrict green, float ** restrict blue, int width, int height );
void lrtpIgvDemosaic( float ** restrict rawData, float ** restrict red, float ** restrict green, float ** restrict blue, int width, int height );
void lrtpRcdDemosaic( float ** restrict rawData, float ** restrict red, float ** restrict green, float ** restrict blue, int width, int height );
void lrtpAmazeDemosaic( float ** restrict rawData, float ** restrict red, float ** restrict green, float ** restrict blue, int width, int height );
void lrtpAhdDemosaic( float ** restrict rawData, float ** restrict red, float ** restrict green, float ** restrict blue, int width, int height );
void lrtpBayerfastDemosaic( float ** restrict rawData, float ** restrict red, float ** restrict green, float ** restrict blue, int width, int height );
void lrtpHLRecovery( float ** restrict red, float ** restrict green, float ** restrict blue, int width, int height, const float chmax[3], const float clmax[3] );
void lrtpCaCorrect( float ** restrict rawData, int winx, int winy, int winw, int winh, const uint8_t autoCA, size_t autoIterations, const double cared, const double cablue, uint8_t avoidColourshift );

#ifdef __cplusplus
}
#endif

#endif // LIBRTPROCESSWRAPPER_H
