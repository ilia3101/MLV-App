/*!
 * \file CA_correct_RT.h
 * \author masc4ii
 * \copyright 2019
 * \brief Header for CA_correct_RT from RawTherapee
 */

#ifndef CA_CORRECT_RT_H
#define CA_CORRECT_RT_H

void CA_correct_RT(float **rawData, int winx, int winy, int winw, int winh,
    int tilex, int tiley, int tilew, int tileh, uint8_t autoCA, float cared, float cablue);

#endif // CA_CORRECT_RT_H
