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
    m_contrast = 0;
    m_temperature = -1;
    m_tint = 0;
    m_clarity = 0;
    m_vibrance = 0;
    m_saturation = 0;
    m_ds = 20;
    m_dr = 70;
    m_ls = 0;
    m_lr = 50;
    m_lightening = 0;
    m_shadows = 0;
    m_highlights = 0;
    m_gradationCurve = QString( "1e-5;1e-5;1;1;?1e-5;1e-5;1;1;?1e-5;1e-5;1;1;?1e-5;1e-5;1;1;" );
    m_hueVsHue = QString( "0;0;1;0;" );
    m_hueVsSat = QString( "0;0;1;0;" );
    m_hueVsLuma = QString( "0;0;1;0;" );
    m_lumaVsSat = QString( "0;0;1;0;" );

    m_isGradientEnabled = false;
    m_gradientExposure = 0;
    m_gradientContrast = 0;
    m_gradientX1 = 0;
    m_gradientY1 = 0;
    m_gradientLength = 1;
    m_gradientAngle = 0;

    m_sharpen = 0;
    m_shMasking = 0;
    m_chromaBlur = 0;
    m_denoiserWindow = 3;
    m_denoiserStrength = 0;
    m_rbfDenoiserLuma = 0;
    m_rbfDenoiserChroma = 0;
    m_rbfDenoiserRange = 40;
    m_grainStrength = 0;
    m_highlightReconstruction = false;
    m_useCamMatrix = 1;
    m_chromaSeparation = false;
    m_rawFixesEnabled = true;
    m_vertical_stripes = 0;
    m_focus_pixels = -1;
    m_fpi_method = 0;
    m_bad_pixels = 0;
    m_bps_method = 0;
    m_bpi_method = 0;
    m_chroma_smooth = 0;
    m_pattern_noise = 0;
    m_deflicker_target = 0;
    m_dualIsoForced = -1;
    m_dualIsoOn = 0;
    m_dualIsoInt = 0;
    m_dualIsoAliasMap = 1;
    m_dualIsoFrBlending = 1;
    m_dualIsoWhite = 0;
    m_dualIsoBlack = 0;
    m_darkFrameSubtractionMode = -1;
    m_darkFrameSubtractionName = QString( "No file selected" );
    m_stretchFactorX = 1.0;
    m_stretchFactorY = -1;
    m_upsideDown = false;
    m_vidstabEnable = false;
    m_vidstabZoom = 0;
    m_vidstabSmoothing = 10;
    m_vidstabStepsize = 32;
    m_vidstabShakiness = 10;
    m_vidstabAccuracy = 10;
    m_vidstabTripod = false;
    m_lutEnabled = false;
    m_lutName = QString( "" );
    m_lutStrength = 100;
    m_filterEnabled = false;
    m_filterIndex = 0;
    m_filterStrength = 100;
    m_vignetteStrength = 0;
    m_vignetteRadius = 20;
    m_vignetteShape = 0;
    m_caRed = 0;
    m_caBlue = 0;
    m_caDesaturate = 0;
    m_caRadius = 1;
    m_profile = 2;
    m_tonemap = -1;
    m_gamut = -1;
    m_gamma = 315;
    m_creativeAdjustments = 1;
    m_rawWhite = -1;
    m_rawBlack = -1;
    m_tone = 0;
    m_toningStrength = 0;
    m_cutIn = 1;
    m_cutOut = INT32_MAX;

    m_lastPlaybackPosition = 0;

    m_debayer = AMaZE;
}
