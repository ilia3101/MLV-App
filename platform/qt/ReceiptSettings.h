/*!
 * \file ReceiptSettings.h
 * \author masc4ii
 * \copyright 2017
 * \brief Holds slider settings
 */

#ifndef RECEIPTSETTINGS_H
#define RECEIPTSETTINGS_H

#include <QObject>

class ReceiptSettings
{
public:
    ReceiptSettings();
    void setExposure( int value )   {m_exposure = value;}
    void setTemperature( int value ){m_temperature = value;}
    void setTint( int value )       {m_tint = value;}
    void setSaturation( int value ) {m_saturation = value;}
    void setDr( int value )         {m_dr = value;}
    void setDs( int value )         {m_ds = value;}
    void setLr( int value )         {m_lr = value;}
    void setLs( int value )         {m_ls = value;}
    void setLightening( int value ) {m_lightening = value;}
    void setHighlightReconstruction( bool on ){m_highlightReconstruction = on;}
    void setReinhardTonemapping( bool on )    {m_reinhardTonemapping = on;}
    int exposure( void )   {return m_exposure;}
    int temperature( void ){return m_temperature;}
    int tint( void )       {return m_tint;}
    int saturation( void ) {return m_saturation;}
    int dr( void )         {return m_dr;}
    int ds( void )         {return m_ds;}
    int lr( void )         {return m_lr;}
    int ls( void )         {return m_ls;}
    int lightening( void ) {return m_lightening;}
    bool isHighlightReconstruction( void ){return m_highlightReconstruction;}
    bool isReinhardTonemapping( void )    {return m_reinhardTonemapping;}

private:
    int m_exposure;
    int m_temperature;
    int m_tint;
    int m_saturation;
    int m_dr;
    int m_ds;
    int m_lr;
    int m_ls;
    int m_lightening;
    bool m_highlightReconstruction;
    bool m_reinhardTonemapping;
};

#endif // RECEIPTSETTINGS_H
