/*!
* \file rbf_wrapper.cpp
* \author masc4ii
* \copyright 2019
* \brief Wrapper for rbfilter
*/

#include "rbf.h"
#include "RBFilterPlain.h"

#ifdef __cplusplus
extern "C" {
#endif

void recursive_bf_wrap(uint16_t * img_in,
        uint16_t * img_out,
        float sigma_spatial, float sigma_range,
        int width, int height, int channel)
{
    if( 0 )
    {
        //Qingxiong Yang version
        recursive_bf(
            img_in,
            img_out,
            sigma_spatial*12.0f, sigma_range*4.0f/3.0f,
            width, height, channel,
            0);
    }
    else
    {
        //Ming version with better right boarder
        CRBFilterPlain rbf;
        rbf.reserveMemory( width, height, channel );
        rbf.filter( img_in, img_out, sigma_spatial, sigma_range, width, height, channel );
        rbf.releaseMemory();
    }
}

#ifdef __cplusplus
}
#endif
