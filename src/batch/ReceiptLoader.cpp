#include "ReceiptLoader.h"
#include "BatchLogger.h"

#include "../../platform/qt/ReceiptSettings.h"

#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

/* These constants are defined locally in MainWindow.cpp for receipt
 * version < 2 compatibility scaling.  Duplicated here to avoid
 * pulling in MainWindow.h for three trivial numbers. */
#define FACTOR_DS       22.5
#define FACTOR_LS       11.2
#define FACTOR_LIGHTEN  0.6

/* DISO_FORCED comes from the llrawproc C API enum */
extern "C" {
#include "../../src/mlv/llrawproc/llrawproc.h"
}

/* PROFILE_* and GAMUT_* constants from processing API */
extern "C" {
#include "../../src/processing/raw_processing.h"
}

/* ------------------------------------------------------------------ */

bool ReceiptLoader::loadFromFile(const QString &receiptPath,
                                 ReceiptSettings *receipt,
                                 QString *errorMsg)
{
    QFileInfo fi(receiptPath);
    if( !fi.exists() || !fi.isFile() )
    {
        if( errorMsg )
            *errorMsg = QStringLiteral("Receipt file not found: %1").arg(receiptPath);
        return false;
    }

    QFile file(receiptPath);
    if( !file.open(QIODevice::ReadOnly | QFile::Text) )
    {
        if( errorMsg )
            *errorMsg = QStringLiteral("Cannot open receipt file: %1").arg(receiptPath);
        return false;
    }

    QXmlStreamReader Rxml;
    Rxml.setDevice(&file);

    bool foundReceipt = false;
    int versionReceipt = 0;

    /* Scan for the <receipt> root element — same logic as
     * MainWindow::on_actionImportReceipt_triggered() */
    while( !Rxml.atEnd() )
    {
        Rxml.readNext();
        if( Rxml.isStartElement() && Rxml.name() == QString("receipt") )
        {
            if( Rxml.attributes().size() != 0 )
                versionReceipt = Rxml.attributes().at(0).value().toInt();
            parseXmlElements( &Rxml, receipt, versionReceipt );
            foundReceipt = true;
            break;
        }
    }

    if( Rxml.hasError() )
    {
        if( errorMsg )
            *errorMsg = QStringLiteral("XML parse error in %1: %2")
                            .arg(receiptPath, Rxml.errorString());
        file.close();
        return false;
    }

    file.close();

    if( !foundReceipt )
    {
        if( errorMsg )
            *errorMsg = QStringLiteral("No <receipt> element found in: %1").arg(receiptPath);
        return false;
    }

    receipt->setLoaded();
    return true;
}

/* ------------------------------------------------------------------ */

void ReceiptLoader::printCdngSettings(const ReceiptSettings *receipt)
{
    /* Cast away const — ReceiptSettings getters are non-const (upstream
     * design).  We only call getters here, so this is safe. */
    ReceiptSettings *r = const_cast<ReceiptSettings *>(receipt);

    BatchLogger::out(QStringLiteral("[BATCH] RECEIPT settings (CDNG-relevant):\n"));

    /* --- Raw fixes --- */
    BatchLogger::out(QStringLiteral("[BATCH]   rawFixesEnabled   = %1\n")
                .arg( r->rawFixesEnabled() ? QStringLiteral("true")
                                           : QStringLiteral("false") ));
    BatchLogger::out(QStringLiteral("[BATCH]   verticalStripes   = %1\n").arg( r->verticalStripes() ));
    BatchLogger::out(QStringLiteral("[BATCH]   focusPixels       = %1\n").arg( r->focusPixels() ));
    BatchLogger::out(QStringLiteral("[BATCH]   fpiMethod         = %1\n").arg( r->fpiMethod() ));
    BatchLogger::out(QStringLiteral("[BATCH]   badPixels         = %1\n").arg( r->badPixels() ));
    BatchLogger::out(QStringLiteral("[BATCH]   bpsMethod         = %1\n").arg( r->bpsMethod() ));
    BatchLogger::out(QStringLiteral("[BATCH]   bpiMethod         = %1\n").arg( r->bpiMethod() ));
    BatchLogger::out(QStringLiteral("[BATCH]   chromaSmooth      = %1\n").arg( r->chromaSmooth() ));
    BatchLogger::out(QStringLiteral("[BATCH]   patternNoise      = %1\n").arg( r->patternNoise() ));
    BatchLogger::out(QStringLiteral("[BATCH]   deflickerTarget   = %1\n").arg( r->deflickerTarget() ));

    /* --- Dual ISO --- */
    BatchLogger::out(QStringLiteral("[BATCH]   dualIso           = %1\n").arg( r->dualIso() ));
    BatchLogger::out(QStringLiteral("[BATCH]   dualIsoForced     = %1\n").arg( r->dualIsoForced() ));
    BatchLogger::out(QStringLiteral("[BATCH]   dualIsoPattern    = %1\n").arg( r->dualIsoPattern() ));
    BatchLogger::out(QStringLiteral("[BATCH]   dualIsoEvCorrection = %1\n").arg( r->dualIsoEvCorrection() ));
    BatchLogger::out(QStringLiteral("[BATCH]   dualIsoBlackDelta = %1\n").arg( r->dualIsoBlackDelta() ));
    BatchLogger::out(QStringLiteral("[BATCH]   dualIsoInterpolation = %1\n").arg( r->dualIsoInterpolation() ));
    BatchLogger::out(QStringLiteral("[BATCH]   dualIsoAliasMap   = %1\n").arg( r->dualIsoAliasMap() ));
    BatchLogger::out(QStringLiteral("[BATCH]   dualIsoFrBlending = %1\n").arg( r->dualIsoFrBlending() ));
    BatchLogger::out(QStringLiteral("[BATCH]   dualIsoWhite      = %1\n").arg( r->dualIsoWhite() ));
    BatchLogger::out(QStringLiteral("[BATCH]   dualIsoBlack      = %1\n").arg( r->dualIsoBlack() ));

    /* --- Dark frame --- */
    BatchLogger::out(QStringLiteral("[BATCH]   darkFrameEnabled  = %1\n").arg( r->darkFrameEnabled() ));
    BatchLogger::out(QStringLiteral("[BATCH]   darkFrameFileName = %1\n").arg( r->darkFrameFileName() ));

    /* --- Raw black/white levels --- */
    BatchLogger::out(QStringLiteral("[BATCH]   rawBlack          = %1\n").arg( r->rawBlack() ));
    BatchLogger::out(QStringLiteral("[BATCH]   rawWhite          = %1\n").arg( r->rawWhite() ));

    /* --- Cut in/out --- */
    BatchLogger::out(QStringLiteral("[BATCH]   cutIn             = %1\n").arg( r->cutIn() ));
    BatchLogger::out(QStringLiteral("[BATCH]   cutOut            = %1\n").arg( r->cutOut() ));

    /* --- Stretch / orientation --- */
    BatchLogger::out(QStringLiteral("[BATCH]   stretchFactorX    = %1\n").arg( r->stretchFactorX(), 0, 'f', 4 ));
    BatchLogger::out(QStringLiteral("[BATCH]   stretchFactorY    = %1\n").arg( r->stretchFactorY(), 0, 'f', 4 ));
    BatchLogger::out(QStringLiteral("[BATCH]   upsideDown        = %1\n")
                .arg( r->upsideDown() ? QStringLiteral("true")
                                      : QStringLiteral("false") ));
}

/* ------------------------------------------------------------------ */
/* parseXmlElements — verbatim copy of
 * MainWindow::readXmlElementsFromFile() with no MainWindow dependency.
 * Parses ALL tags so ReceiptSettings is fully populated.
 * Version compatibility (v0-v4) logic preserved exactly. */

void ReceiptLoader::parseXmlElements(QXmlStreamReader *Rxml,
                                     ReceiptSettings *receipt,
                                     int version)
{
    /* Compatibility defaults for old receipts (same as MainWindow) */
    receipt->setCamMatrixUsed( 0 );
    receipt->setDualIsoForced( DISO_FORCED );
    receipt->setDualIsoAutoCorrected( 1 );
    receipt->setDualIsoPattern( 0 );
    receipt->setDualIsoEvCorrection( 1 );
    receipt->setDualIsoBlackDelta( -1 );

    while( !Rxml->atEnd() && !Rxml->isEndElement() )
    {
        Rxml->readNext();

        if( Rxml->isStartElement() && Rxml->name() == QString( "exposure" ) )
        {
            receipt->setExposure( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "contrast" ) )
        {
            receipt->setContrast( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "pivot" ) )
        {
            receipt->setPivot( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "temperature" ) )
        {
            receipt->setTemperature( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "tint" ) )
        {
            receipt->setTint( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "clarity" ) )
        {
            receipt->setClarity( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vibrance" ) )
        {
            receipt->setVibrance( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "saturation" ) )
        {
            if( version < 2 ) receipt->setSaturation( ( Rxml->readElementText().toInt() * 2.0 ) - 100.0 );
            else receipt->setSaturation( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "ls" ) )
        {
            if( version < 2 ) receipt->setLs( Rxml->readElementText().toInt() * 10.0 / FACTOR_LS );
            else receipt->setLs( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lr" ) )
        {
            receipt->setLr( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "ds" ) )
        {
            if( version < 2 ) receipt->setDs( Rxml->readElementText().toInt() * 10.0 / FACTOR_DS );
            else receipt->setDs( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dr" ) )
        {
            receipt->setDr( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lightening" ) )
        {
            if( version < 2 ) receipt->setLightening( Rxml->readElementText().toInt() / FACTOR_LIGHTEN );
            else receipt->setLightening( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "shadows" ) )
        {
            receipt->setShadows( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "highlights" ) )
        {
            receipt->setHighlights( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradationCurve" ) )
        {
            receipt->setGradationCurve( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "hueVsHue" ) )
        {
            receipt->setHueVsHue( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "hueVsSaturation" ) )
        {
            receipt->setHueVsSaturation( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "hueVsLuminance" ) )
        {
            receipt->setHueVsLuminance( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lumaVsSaturation" ) )
        {
            receipt->setLumaVsSaturation( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientEnabled" ) )
        {
            receipt->setGradientEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientExposure" ) )
        {
            receipt->setGradientExposure( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientContrast" ) )
        {
            receipt->setGradientContrast( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientStartX" ) )
        {
            receipt->setGradientStartX( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientStartY" ) )
        {
            receipt->setGradientStartY( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientLength" ) )
        {
            receipt->setGradientLength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientAngle" ) )
        {
            receipt->setGradientAngle( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "sharpen" ) )
        {
            receipt->setSharpen( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "sharpenMasking" ) )
        {
            receipt->setShMasking( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "chromaBlur" ) )
        {
            receipt->setChromaBlur( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "highlightReconstruction" ) )
        {
            receipt->setHighlightReconstruction( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "camMatrixUsed" ) )
        {
            receipt->setCamMatrixUsed( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "chromaSeparation" ) )
        {
            receipt->setChromaSeparation( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "profile" ) )
        {
            uint8_t profile = (uint8_t)Rxml->readElementText().toUInt();
            if( version < 2 && profile > 1 ) receipt->setProfile( profile + 2 );
            else if( version == 2 )
            {
                receipt->setProfile( profile + 1 );
                receipt->setGamut( GAMUT_Rec709 );
                if( ( profile != PROFILE_ALEXA_LOG )
                 && ( profile != PROFILE_CINEON_LOG )
                 && ( profile != PROFILE_SONY_LOG_3 )
                 && ( profile != PROFILE_SRGB )
                 && ( profile != PROFILE_REC709 )
                 && ( profile != PROFILE_DWG_INT ) )
                {
                    receipt->setAllowCreativeAdjustments( true );
                }
                else
                {
                    receipt->setAllowCreativeAdjustments( false );
                }
                switch( profile )
                {
                case PROFILE_STANDARD:
                case PROFILE_TONEMAPPED:
                    receipt->setGamma( 315 );
                    break;
                case PROFILE_FILM:
                    receipt->setGamma( 346 );
                    break;
                default:
                    receipt->setGamma( 100 );
                    break;
                }
            }
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "tonemap" ) )
        {
            receipt->setTonemap( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "transferFunction" ) )
        {
            receipt->setTransferFunction( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gamut" ) )
        {
            receipt->setGamut( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gamma" ) )
        {
            receipt->setGamma( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "allowCreativeAdjustments" ) )
        {
            receipt->setAllowCreativeAdjustments( (bool)Rxml->readElementText().toInt() );
            if( version == 2 )
            {
                int profile = receipt->profile();
                if( ( profile != PROFILE_ALEXA_LOG )
                 && ( profile != PROFILE_CINEON_LOG )
                 && ( profile != PROFILE_SONY_LOG_3 )
                 && ( profile != PROFILE_SRGB )
                 && ( profile != PROFILE_REC709 )
                 && ( profile != PROFILE_DWG_INT ) )
                {
                    receipt->setAllowCreativeAdjustments( true );
                }
            }
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "exrMode" ) )
        {
            receipt->setExrMode( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "agx" ) )
        {
            receipt->setAgx( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "denoiserWindow" ) )
        {
            receipt->setDenoiserWindow( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "denoiserStrength" ) )
        {
            receipt->setDenoiserStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rbfDenoiserLuma" ) )
        {
            receipt->setRbfDenoiserLuma( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rbfDenoiserChroma" ) )
        {
            receipt->setRbfDenoiserChroma( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rbfDenoiserRange" ) )
        {
            receipt->setRbfDenoiserRange( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "grainStrength" ) )
        {
            receipt->setGrainStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "grainLumaWeight" ) )
        {
            receipt->setGrainLumaWeight( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rawFixesEnabled" ) )
        {
            receipt->setRawFixesEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "verticalStripes" ) )
        {
            receipt->setVerticalStripes( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "focusPixels" ) )
        {
            receipt->setFocusPixels( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "fpiMethod" ) )
        {
            receipt->setFpiMethod( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "badPixels" ) )
        {
            receipt->setBadPixels( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "bpsMethod" ) )
        {
            receipt->setBpsMethod( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "bpiMethod" ) )
        {
            receipt->setBpiMethod( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "chromaSmooth" ) )
        {
            receipt->setChromaSmooth( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "patternNoise" ) )
        {
            receipt->setPatternNoise( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "deflickerTarget" ) )
        {
            receipt->setDeflickerTarget( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoForced" ) )
        {
            receipt->setDualIsoForced( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIso" ) )
        {
            receipt->setDualIso( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoPattern" ) )
        {
            receipt->setDualIsoPattern( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoEvCorrection" ) )
        {
            receipt->setDualIsoEvCorrection( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoBlackDelta" ) )
        {
            receipt->setDualIsoBlackDelta( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoInterpolation" ) )
        {
            receipt->setDualIsoInterpolation( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoAliasMap" ) )
        {
            receipt->setDualIsoAliasMap( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoFrBlending" ) )
        {
            receipt->setDualIsoFrBlending( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoWhite" ) )
        {
            receipt->setDualIsoWhite( Rxml->readElementText().toUInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoBlack" ) )
        {
            receipt->setDualIsoBlack( Rxml->readElementText().toUInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "darkFrameFileName" ) )
        {
            receipt->setDarkFrameFileName( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "darkFrameEnabled" ) )
        {
            receipt->setDarkFrameEnabled( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rawBlack" ) )
        {
            if( version < 4 ) receipt->setRawBlack( Rxml->readElementText().toInt() * 10 );
            else receipt->setRawBlack( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rawWhite" ) )
        {
            receipt->setRawWhite( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "tone" ) )
        {
            receipt->setTone( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "toningStrength" ) )
        {
            receipt->setToningStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lutEnabled" ) )
        {
            receipt->setLutEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lutName" ) )
        {
            receipt->setLutName( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lutStrength" ) )
        {
            receipt->setLutStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "filterEnabled" ) )
        {
            receipt->setFilterEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "filterIndex" ) )
        {
            receipt->setFilterIndex( Rxml->readElementText().toUInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "filterStrength" ) )
        {
            receipt->setFilterStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vignetteStrength" ) )
        {
            receipt->setVignetteStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vignetteRadius" ) )
        {
            receipt->setVignetteRadius( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vignetteShape" ) )
        {
            receipt->setVignetteShape( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "caRed" ) )
        {
            receipt->setCaRed( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "caBlue" ) )
        {
            receipt->setCaBlue( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "caDesaturate" ) )
        {
            receipt->setCaDesaturate( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "caRadius" ) )
        {
            receipt->setCaRadius( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "stretchFactorX" ) )
        {
            receipt->setStretchFactorX( Rxml->readElementText().toDouble() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "stretchFactorY" ) )
        {
            receipt->setStretchFactorY( Rxml->readElementText().toDouble() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "upsideDown" ) )
        {
            receipt->setUpsideDown( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabEnable" ) )
        {
            receipt->setVidstabEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabStepsize" ) )
        {
            receipt->setVidstabStepsize( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabShakiness" ) )
        {
            receipt->setVidstabShakiness( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabAccuracy" ) )
        {
            receipt->setVidstabAccuracy( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabZoom" ) )
        {
            receipt->setVidstabZoom( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabSmoothing" ) )
        {
            receipt->setVidstabSmoothing( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabTripod" ) )
        {
            receipt->setVidstabTripod( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "cutIn" ) )
        {
            receipt->setCutIn( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "cutOut" ) )
        {
            receipt->setCutOut( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "debayer" ) )
        {
            receipt->setDebayer( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() ) /* future/unknown tags */
        {
            Rxml->readElementText();
            Rxml->readNext();
        }
    }
}
