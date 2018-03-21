/*!
 * \file ReceiptSettings.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Holds slider settings
 */

#include "ReceiptSettings.h"

ReceiptSettings::ReceiptSettings()
{
    m_neverLoaded = true;

    //default settings
    m_exposure = 0;
    m_temperature = -1;
    m_tint = 0;
    m_saturation = 0;
    m_ds = 22;
    m_dr = 73;
    m_ls = 0;
    m_lr = 50;
    m_lightening = 0;
    m_shadows = 0;
    m_highlights = 0;
    m_sharpen = 0;
    m_chromaBlur = 0;
    m_highlightReconstruction = false;
    m_chromaSeparation = false;
    m_rawFixesEnabled = true;
    m_vertical_stripes = 0;
    m_focus_pixels = 1;
    m_fpi_method = 0;
    m_bad_pixels = 1;
    m_bps_method = 0;
    m_bpi_method = 0;
    m_chroma_smooth = 0;
    m_pattern_noise = 0;
    m_deflicker_target = 0;
    m_dualIsoOn = 0;
    m_dualIsoInt = 0;
    m_dualIsoAliasMap = 1;
    m_dualIsoFrBlending = 1;
    m_darkFrameSubtractionMode = -1;
    m_darkFrameSubtractionName = QString( "No file selected" );
    m_stretchFactorX = 1.0;
    m_stretchFactorY = 1.0;
    m_upsideDown = false;
    m_filterEnabled = false;
    m_filterIndex = 0;
    m_filterStrength = 100;
    m_profile = 1;
    m_cutIn = 1;
    m_cutOut = INT32_MAX;
}
