/*!
* \file rbf_wrapper.h
* \author masc4ii
* \copyright 2019
* \brief Wrapper for rbfilter
*/

#ifndef RBF_WRAP_H
#define RBF_WRAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern void recursive_bf_wrap(
        uint16_t * img_in,
        uint16_t * img_out,
        float sigma_spatial, float sigma_range,
        int width, int height, int channel);

#ifdef __cplusplus
}
#endif

#endif // RBF_WRAP_H
