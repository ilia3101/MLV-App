/*!
* \file rbf_wrapper.cpp
* \author masc4ii
* \copyright 2019
* \brief Wrapper for rbfilter
*/

#include "rbf.h"

#ifdef __cplusplus
extern "C" {
#endif

void recursive_bf_wrap(uint16_t * img_in,
        uint16_t * img_out,
        float sigma_spatial, float sigma_range,
        int width, int height, int channel)
{
    recursive_bf(
            img_in,
            img_out,
            sigma_spatial, sigma_range,
            width, height, channel,
            0);
}

#ifdef __cplusplus
}
#endif
