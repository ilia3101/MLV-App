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
    void setSharpen( int value )    {m_sharpen = value;}
    void setHighlightReconstruction( bool on ){m_highlightReconstruction = on;}
    void setProfile( uint8_t num )  {m_profile = num;}
    void setFileName( QString fileName )      {m_fileName = fileName;}
    void setExportFileName( QString fileName ){m_exportFileName = fileName;}
    void setVerticalStripes( int mode )       {m_vertical_stripes = mode;}
    void setFocusPixels( int mode )           {m_focus_pixels = mode;}
    void setFpiMethod( int mode )             {m_fpi_method = mode;}
    void setBadPixels( int mode )             {m_bad_pixels = mode;}
    void setBpiMethod( int mode )             {m_bpi_method = mode;}
    void setChromaSmooth( int mode )          {m_chroma_smooth = mode;}
    void setPatternNoise( int on )            {m_pattern_noise = on;}
    void setDeflickerTarget( int value )      {m_deflicker_target = value;}
    int exposure( void )   {return m_exposure;}
    int temperature( void ){return m_temperature;}
    int tint( void )       {return m_tint;}
    int saturation( void ) {return m_saturation;}
    int dr( void )         {return m_dr;}
    int ds( void )         {return m_ds;}
    int lr( void )         {return m_lr;}
    int ls( void )         {return m_ls;}
    int lightening( void ) {return m_lightening;}
    int sharpen( void )    {return m_sharpen;}
    bool isHighlightReconstruction( void ){return m_highlightReconstruction;}
    int verticalStripes( void ){return m_vertical_stripes;}
    int focusPixels( void ){return m_focus_pixels;}
    int fpiMethod( void )  {return m_fpi_method;}
    int badPixels( void )  {return m_bad_pixels;}
    int bpiMethod( void )  {return m_bpi_method;}
    int chromaSmooth( void ){return m_chroma_smooth;}
    int patternNoise( void ){return m_pattern_noise;}
    int deflickerTarget( void ){return m_deflicker_target;}
    uint8_t profile( void ){return m_profile;}
    QString fileName( void ){return m_fileName;}
    QString exportFileName( void ){return m_exportFileName;}

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
    int m_sharpen;
    bool m_highlightReconstruction;
    int m_vertical_stripes; // fix vertical stripes, 0 - do not fix", 1 - fix, 2 - calculate for every frame
    int m_focus_pixels;     // fix focus pixels, false - do not fix, true - fix
    int m_fpi_method;       // focus pixel interpolation method: 0 - mlvfs, 1 - raw2dng
    int m_bad_pixels;       // fix bad pixels, 0 - do not fix, 1 - fix, 2 - makes algorithm aggresive to reveal more bad pixels
    int m_bpi_method;       // bad pixel interpolation method: 0 - mlvfs, 1 - raw2dng
    int m_chroma_smooth;    // chroma smooth, 2 - cs2x2, 3 cs3x3, 5 - cs5x5
    int m_pattern_noise;    // fix pattern noise (0, 1)
    int m_deflicker_target; // deflicker value
    uint8_t m_profile;
    QString m_fileName;
    QString m_exportFileName;
};

#endif // RECEIPTSETTINGS_H
