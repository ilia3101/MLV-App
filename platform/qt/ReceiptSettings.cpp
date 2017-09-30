/*!
 * \file ReceiptSettings.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Holds slider settings
 */

#include "ReceiptSettings.h"

ReceiptSettings::ReceiptSettings()
{
    //default settings
    m_exposure = 0;
    m_temperature = 6250;
    m_tint = 0;
    m_saturation = 50;
    m_ds = 50;
    m_dr = 73;
    m_ls = 0;
    m_lr = 50;
    m_lightening = 0;
    m_sharpen = 0;
    m_highlightReconstruction = false;
    m_vertical_stripes = 1;
    m_focus_pixels = 1;
    m_fpi_method = 1;
    m_bad_pixels = 1;
    m_bpi_method = 1;
    m_chroma_smooth = 0;
    m_pattern_noise = 0;
    m_deflicker_target = 0;
    m_dualIso = 0;
    m_profile = 1;
}
