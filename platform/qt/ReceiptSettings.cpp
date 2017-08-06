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
    m_highlightReconstruction = false;
    m_reinhardTonemapping = false;
}
