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
    void setLoaded( void )          {m_neverLoaded = false;}

    void setExposure( int value )   {m_exposure = value;}
    void setTemperature( int value ){m_temperature = value;}
    void setTint( int value )       {m_tint = value;}
    void setSaturation( int value ) {m_saturation = value;}
    void setDr( int value )         {m_dr = value;}
    void setDs( int value )         {m_ds = value;}
    void setLr( int value )         {m_lr = value;}
    void setLs( int value )         {m_ls = value;}
    void setLightening( int value ) {m_lightening = value;}
    void setShadows( int value )    {m_shadows = value;}
    void setHighlights( int value ) {m_highlights = value;}
    void setSharpen( int value )    {m_sharpen = value;}
    void setChromaBlur( int value ) {m_chromaBlur = value;}
    void setHighlightReconstruction( bool on ){m_highlightReconstruction = on;}
    void setChromaSeparation( bool on ){m_chromaSeparation = on;}
    void setProfile( uint8_t num )  {m_profile = num;}
    void setFileName( QString fileName )      {m_fileName = fileName;}
    void setExportFileName( QString fileName ){m_exportFileName = fileName;}
    void setRawFixesEnabled( bool on )        {m_rawFixesEnabled = on;}
    void setVerticalStripes( int mode )       {m_vertical_stripes = mode;}
    void setFocusPixels( int mode )           {m_focus_pixels = mode;}
    void setFpiMethod( int mode )             {m_fpi_method = mode;}
    void setBadPixels( int mode )             {m_bad_pixels = mode;}
    void setBpsMethod( int mode )             {m_bps_method = mode;}
    void setBpiMethod( int mode )             {m_bpi_method = mode;}
    void setChromaSmooth( int mode )          {m_chroma_smooth = mode;}
    void setPatternNoise( int on )            {m_pattern_noise = on;}
    void setDeflickerTarget( int value )      {m_deflicker_target = value;}
    void setDualIso( int mode )               {m_dualIsoOn = mode;}
    void setDualIsoInterpolation( int mode )  {m_dualIsoInt = mode;}
    void setDualIsoAliasMap( int on )         {m_dualIsoAliasMap = on;}
    void setDualIsoFrBlending( int on )       {m_dualIsoFrBlending = on;}
    void setDarkFrameEnabled( int on )        {m_darkFrameSubtractionMode = on;}
    void setDarkFrameFileName( QString name ) {m_darkFrameSubtractionName = name;}
    void setStretchFactorX( double factor )   {m_stretchFactorX = factor;}
    void setStretchFactorY( double factor )   {m_stretchFactorY = factor;}
    void setUpsideDown( bool on )             {m_upsideDown = on;}
    void setFilterEnabled( bool on )          {m_filterEnabled = on;}
    void setFilterIndex( uint8_t idx )        {m_filterIndex = idx;}
    void setFilterStrength( int value )       {m_filterStrength = value;}
    void setCutIn( uint32_t frame )           {m_cutIn = frame;}
    void setCutOut( uint32_t frame )          {m_cutOut = frame;}

    bool wasNeverLoaded( void ){return m_neverLoaded;}

    int exposure( void )   {return m_exposure;}
    int temperature( void ){return m_temperature;}
    int tint( void )       {return m_tint;}
    int saturation( void ) {return m_saturation;}
    int dr( void )         {return m_dr;}
    int ds( void )         {return m_ds;}
    int lr( void )         {return m_lr;}
    int ls( void )         {return m_ls;}
    int lightening( void ) {return m_lightening;}
    int shadows( void )    {return m_shadows;}
    int highlights( void ) {return m_highlights;}
    int sharpen( void )    {return m_sharpen;}
    int chromaBlur( void ) {return m_chromaBlur;}
    bool isHighlightReconstruction( void ){return m_highlightReconstruction;}
    bool isChromaSeparation( void ){return m_chromaSeparation;}
    bool rawFixesEnabled( void ){return m_rawFixesEnabled;}
    int verticalStripes( void ){return m_vertical_stripes;}
    int focusPixels( void ){return m_focus_pixels;}
    int fpiMethod( void )  {return m_fpi_method;}
    int badPixels( void )  {return m_bad_pixels;}
    int bpsMethod( void )  {return m_bps_method;}
    int bpiMethod( void )  {return m_bpi_method;}
    int chromaSmooth( void ){return m_chroma_smooth;}
    int patternNoise( void ){return m_pattern_noise;}
    int deflickerTarget( void ){return m_deflicker_target;}
    int dualIso( void )    {return m_dualIsoOn;}
    int dualIsoInterpolation( void ){return m_dualIsoInt;}
    int dualIsoAliasMap( void ){return m_dualIsoAliasMap;}
    int dualIsoFrBlending( void ){return m_dualIsoFrBlending;}
    int darkFrameEnabled( void ) {return m_darkFrameSubtractionMode;}
    QString darkFrameFileName( void ){return m_darkFrameSubtractionName;}
    double stretchFactorX( void ){return m_stretchFactorX;}
    double stretchFactorY( void ){return m_stretchFactorY;}
    bool upsideDown( void ){return m_upsideDown;}
    bool filterEnabled( void ){return m_filterEnabled;}
    uint8_t filterIndex( void ){return m_filterIndex;}
    int filterStrength( void ){return m_filterStrength;}
    uint32_t cutIn( void ) {return m_cutIn;}
    uint32_t cutOut( void ){return m_cutOut;}
    uint8_t profile( void ){return m_profile;}
    QString fileName( void ){return m_fileName;}
    QString exportFileName( void ){return m_exportFileName;}

private:
    bool m_neverLoaded;

    int m_exposure;
    int m_temperature;
    int m_tint;
    int m_saturation;
    int m_dr;
    int m_ds;
    int m_lr;
    int m_ls;
    int m_lightening;
    int m_shadows;
    int m_highlights;
    int m_sharpen;
    int m_chromaBlur;
    bool m_highlightReconstruction;
    bool m_chromaSeparation;
    bool m_rawFixesEnabled; // Enable/Disable all raw fixes
    int m_vertical_stripes; // fix vertical stripes, 0 - do not fix", 1 - fix, 2 - calculate for every frame
    int m_focus_pixels;     // fix focus pixels, false - do not fix, true - fix
    int m_fpi_method;       // focus pixel interpolation method: 0 - mlvfs, 1 - raw2dng
    int m_bad_pixels;       // fix bad pixels, 0 - do not fix, 1 - fix, 2 - makes algorithm aggresive to reveal more bad pixels
    int m_bps_method;       // bad pixel search method: 0 - normal, 1 - force
    int m_bpi_method;       // bad pixel interpolation method: 0 - mlvfs, 1 - raw2dng
    int m_chroma_smooth;    // chroma smooth, 2 - cs2x2, 3 cs3x3, 5 - cs5x5
    int m_pattern_noise;    // fix pattern noise (0, 1)
    int m_deflicker_target; // deflicker value
    int m_dualIsoOn;        // DualISO, 0 = off, 1 = on, 2 = preview
    int m_dualIsoInt;       // DualIsoInterpolation method, 0 - amaze-edge, 1 - mean23
    int m_dualIsoAliasMap;  // flag for Alias Map switchin on/off
    int m_dualIsoFrBlending;// flag for Fullres Blending switching on/off
    int m_darkFrameSubtractionMode; // 0 = off, 1 = External, 2 = Internal
    QString m_darkFrameSubtractionName; // FileName
    double m_stretchFactorX;
    double m_stretchFactorY;
    bool m_upsideDown;
    bool m_filterEnabled;
    uint8_t m_filterIndex;
    int m_filterStrength;
    uint32_t m_cutIn;
    uint32_t m_cutOut;
    uint8_t m_profile;
    QString m_fileName;
    QString m_exportFileName;
};

#endif // RECEIPTSETTINGS_H
