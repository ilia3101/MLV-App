#include "ReceiptApplier.h"
#include "BatchContext.h"
#include "BatchLogger.h"

#include "../../platform/qt/ReceiptSettings.h"

#include <QFileInfo>

/* -----------------------------------------------------------------------
 * applyToMlv()
 *
 * Replicates the NET EFFECT of MainWindow::setSliders() on the
 * mlvObject_t / processingObject_t, bypassing the GUI signal chain.
 *
 * The code below is a faithful extraction of every C API call that
 * setSliders() triggers through its setToolButton*() → toolButton*Changed()
 * signal chain, plus the direct struct assignments for dual ISO.
 *
 * ORDERING matches setSliders() exactly.
 * ----------------------------------------------------------------------- */

void ReceiptApplier::applyToMlv(ReceiptSettings *receipt,
                                 mlvObject_t *mlvObject,
                                 processingObject_t *processingObject)
{
    /* ---- Raw fixes enable/disable ---- */
    llrpSetFixRawMode( mlvObject, (int)receipt->rawFixesEnabled() );

    /* ---- Focus pixels ----
     * -1 = auto-detect (first load), else use receipt value */
    if( receipt->focusPixels() == -1 )
    {
        llrpSetFocusPixelMode( mlvObject, llrpDetectFocusDotFixMode( mlvObject ) );
    }
    else
    {
        llrpSetFocusPixelMode( mlvObject, receipt->focusPixels() );
    }
    llrpResetFpmStatus( mlvObject );
    llrpResetBpmStatus( mlvObject );

    /* ---- Focus pixels interpolation method ---- */
    llrpSetFocusPixelInterpolationMethod( mlvObject, receipt->fpiMethod() );

    /* ---- Bad pixels ---- */
    llrpSetBadPixelMode( mlvObject, receipt->badPixels() );
    llrpResetBpmStatus( mlvObject );

    /* ---- Bad pixels search method ---- */
    llrpSetBadPixelSearchMethod( mlvObject, receipt->bpsMethod() );
    llrpResetBpmStatus( mlvObject );

    /* ---- Bad pixels interpolation method ---- */
    llrpSetBadPixelInterpolationMethod( mlvObject, receipt->bpiMethod() );

    /* ---- Chroma smooth ----
     * Receipt stores toolButton index 0-3 which maps 1:1 to
     * CS_OFF=0, CS_2x2=1, CS_3x3=2, CS_5x5=3 enum values. */
    llrpSetChromaSmoothMode( mlvObject, receipt->chromaSmooth() );

    /* ---- Pattern noise ---- */
    llrpSetPatternNoiseMode( mlvObject, receipt->patternNoise() );

    /* ---- Upside down ---- */
    processingSetTransformation( processingObject, receipt->upsideDown() );

    /* ---- Vertical stripes ----
     * -1 = auto-detect: enable for Canon 5D3 (cameraModel 0x80000285) */
    if( receipt->verticalStripes() == -1 )
    {
        if( getMlvCameraModel( mlvObject ) == 0x80000285 )
            llrpSetVerticalStripeMode( mlvObject, 1 );
        else
            llrpSetVerticalStripeMode( mlvObject, 0 );
    }
    else
    {
        llrpSetVerticalStripeMode( mlvObject, receipt->verticalStripes() );
    }
    llrpComputeStripesOn( mlvObject );
    llrpResetFpmStatus( mlvObject );
    llrpResetBpmStatus( mlvObject );

    /* ==== Dual ISO — complex logic faithfully extracted from setSliders() ==== */

    /* Step 1: Resolve dualIsoForced (-1 = uninitialized) */
    if( receipt->dualIsoForced() == -1 )
    {
        receipt->setDualIsoForced( llrpGetDualIsoValidity( mlvObject ) );
    }
    else if( receipt->dualIsoForced() == DISO_FORCED
             && llrpGetDualIsoValidity( mlvObject ) == DISO_VALID )
    {
        receipt->setDualIsoForced( DISO_VALID );
    }
    else if( receipt->dualIsoForced() == DISO_VALID
             && llrpGetDualIsoValidity( mlvObject ) != DISO_VALID )
    {
        receipt->setDualIsoForced( DISO_FORCED );
    }

    /* Step 2: If forced, set validity flag */
    if( receipt->dualIsoForced() == DISO_FORCED )
    {
        llrpSetDualIsoValidity( mlvObject, 1 );
    }

    /* Step 3: Reset diso_auto_correction sign (matches GUI logic) */
    if( mlvObject->llrawproc->diso_auto_correction > 0 )
    {
        mlvObject->llrawproc->diso_auto_correction =
            -mlvObject->llrawproc->diso_auto_correction;
    }

    /* Step 4: Handle auto-corrected vs non-auto-corrected dual ISO */
    if( !receipt->dualIsoAutoCorrected() )
    {
        receipt->setDualIso( 0 );

        if( receipt->dualIsoForced() == DISO_VALID )
        {
            /* Enable dual ISO if the two ISO levels actually differ */
            if( mlvObject->llrawproc->diso1 != mlvObject->llrawproc->diso2 )
            {
                receipt->setDualIso( 1 );
            }
            mlvObject->llrawproc->diso_pattern = 0;
            mlvObject->llrawproc->diso_auto_correction = -1;
            mlvObject->llrawproc->diso_ev_correction = 1;
            mlvObject->llrawproc->diso_black_delta = -1;
        }
        else
        {
            /* Not VALID — set pattern/ev/black to zeroed defaults
             * (mirrors the GUI setting combobox=0, sliders=0) */
            mlvObject->llrawproc->diso_pattern = 0;
            mlvObject->llrawproc->diso_ev_correction = 0;
            mlvObject->llrawproc->diso_black_delta = 0;
        }

        /* Forced overrides — after the above branches */
        if( receipt->dualIsoForced() == DISO_FORCED )
        {
            mlvObject->llrawproc->diso_pattern = 0;
            mlvObject->llrawproc->diso_auto_correction = -2;
            mlvObject->llrawproc->diso_ev_correction = 1;
            mlvObject->llrawproc->diso_black_delta = -1;
        }
    }
    else
    {
        /* Auto-corrected: apply receipt values directly to struct members
         * (matches on_DualIsoPatternComboBox_currentIndexChanged,
         *  on_horizontalSliderDualIsoEvCorrection_valueChanged,
         *  on_horizontalSliderDualIsoBlackDelta_valueChanged) */
        mlvObject->llrawproc->diso_pattern = receipt->dualIsoPattern();

        /* EV correction: receipt stores the slider int value.
         * Slider value 1 is special (triggers auto-correct toggle),
         * other values are divided by 200.0 for the actual EV offset. */
        int evSliderVal = receipt->dualIsoEvCorrection();
        if( evSliderVal != 1 )
        {
            mlvObject->llrawproc->diso_ev_correction = evSliderVal / 200.0;
        }
        else
        {
            /* Value 1 = toggle auto correction sign */
            mlvObject->llrawproc->diso_auto_correction =
                -mlvObject->llrawproc->diso_auto_correction;
        }

        /* Black delta: -1 is special (triggers auto-correct toggle),
         * other values are used directly. */
        int bdSliderVal = receipt->dualIsoBlackDelta();
        if( bdSliderVal != -1 )
        {
            mlvObject->llrawproc->diso_black_delta = bdSliderVal;
        }
        else
        {
            mlvObject->llrawproc->diso_auto_correction =
                -mlvObject->llrawproc->diso_auto_correction;
        }
    }

    /* Step 5: Set dual ISO mode and reset levels */
    llrpSetDualIsoMode( mlvObject, receipt->dualIso() );
    processingSetBlackAndWhiteLevel( mlvObject->processing,
                                     getMlvBlackLevel( mlvObject ),
                                     getMlvWhiteLevel( mlvObject ),
                                     getMlvBitdepth( mlvObject ) );
    llrpResetDngBWLevels( mlvObject );

    /* Step 6: Dual ISO interpolation / alias map / fullres blending */
    llrpSetDualIsoInterpolationMethod( mlvObject, receipt->dualIsoInterpolation() );
    llrpSetDualIsoAliasMapMode( mlvObject, receipt->dualIsoAliasMap() );
    llrpSetDualIsoFullResBlendingMode( mlvObject, receipt->dualIsoFrBlending() );

    /* ---- Deflicker target ---- */
    llrpSetDeflickerTarget( mlvObject, receipt->deflickerTarget() );

    /* ---- Dark frame ----
     * Load external dark frame file first (if valid), then set mode. */
    {
        QString dfName = receipt->darkFrameFileName();
        if( QFileInfo( dfName ).exists()
            && dfName.endsWith( QStringLiteral(".MLV"), Qt::CaseInsensitive ) )
        {
#ifdef Q_OS_UNIX
            QByteArray dfBytes = dfName.toUtf8();
#else
            QByteArray dfBytes = dfName.toLatin1();
#endif
            char errorMessage[256] = { 0 };
            int ret = llrpValidateExtDarkFrame( mlvObject, dfBytes.data(), errorMessage );
            if( !ret )
            {
                llrpInitDarkFrameExtFileName( mlvObject, dfBytes.data() );
                if( errorMessage[0] )
                {
                    BatchLogger::err( QStringLiteral("[BATCH] WARNING dark frame: %1\n")
                                          .arg( QString(errorMessage) ) );
                }
            }
            else
            {
                BatchLogger::err( QStringLiteral("[BATCH] WARNING dark frame rejected: %1\n")
                                      .arg( QString(errorMessage) ) );
                llrpFreeDarkFrameExtFileName( mlvObject );
            }
        }
        else
        {
            llrpFreeDarkFrameExtFileName( mlvObject );
        }

        /* Set dark frame mode (0=off, 1=ext, 2=int).
         * If ext/int requested but no dark frame available, force off. */
        int dfMode = receipt->darkFrameEnabled();
        if( dfMode == -1 )
        {
            /* Auto-detect: use internal dark frame if available */
            if( llrpGetDarkFrameIntStatus( mlvObject ) )
                dfMode = 2;
            else
                dfMode = 0;
        }
        if( dfMode > 0 && !llrpGetDarkFrameExtStatus( mlvObject )
                       && !llrpGetDarkFrameIntStatus( mlvObject ) )
        {
            dfMode = 0;
        }
        llrpSetDarkFrameMode( mlvObject, dfMode );

        /* Dark frame affects dual ISO correction — match GUI behavior */
        if( mlvObject->llrawproc->diso_auto_correction > 0 )
        {
            mlvObject->llrawproc->diso_auto_correction =
                -mlvObject->llrawproc->diso_auto_correction;
            mlvObject->llrawproc->diso_black_delta = -1;
        }
        llrpResetBpmStatus( mlvObject );
        llrpComputeStripesOn( mlvObject );
    }

    /* ---- Raw black / white levels ----
     * -1 = use file defaults (do not override). */
    if( receipt->rawWhite() != -1 )
    {
        int wl = receipt->rawWhite();
        /* Clamp: white must be above black */
        int bl = (receipt->rawBlack() != -1) ? (int)(receipt->rawBlack() / 10.0)
                                             : getMlvBlackLevel( mlvObject );
        if( wl <= bl + 1 ) wl = bl + 2;

        setMlvWhiteLevel( mlvObject, wl );
        processingSetWhiteLevel( processingObject, wl, getMlvBitdepth( mlvObject ) );
        llrpResetFpmStatus( mlvObject );
        llrpResetBpmStatus( mlvObject );
    }

    if( receipt->rawBlack() != -1 )
    {
        double rawBlack = receipt->rawBlack() / 10.0;
        /* Clamp: black must be below white */
        if( rawBlack >= getMlvWhiteLevel( mlvObject ) - 1 )
            rawBlack = getMlvWhiteLevel( mlvObject ) - 2;

        setMlvBlackLevel( mlvObject, rawBlack );
        processingSetBlackLevel( processingObject, rawBlack, getMlvBitdepth( mlvObject ) );
        llrpResetFpmStatus( mlvObject );
        llrpResetBpmStatus( mlvObject );
    }

    /* Final cache reset — ensures all settings take effect on next frame read */
    resetMlvCache( mlvObject );
    resetMlvCachedFrame( mlvObject );
}


/* -----------------------------------------------------------------------
 * printFingerprint()
 *
 * Reads ACTUAL runtime state from the mlvObject/processingObject and
 * prints a structured line.  This proves settings reached the pipeline,
 * not just the ReceiptSettings parser.
 * ----------------------------------------------------------------------- */

void ReceiptApplier::printFingerprint(mlvObject_t *mlvObject,
                                       processingObject_t * /* processingObject */)
{
    llrawprocObject_t *llr = mlvObject->llrawproc;

    BatchLogger::out( QStringLiteral(
        "[BATCH] FINGERPRINT"
        " fixRaw=%1"
        " focusPixels=%2"
        " fpiMethod=%3"
        " badPixels=%4"
        " bpsMethod=%5"
        " bpiMethod=%6"
        " chromaSmooth=%7"
        " patternNoise=%8"
        " verticalStripes=%9"
        " deflickerTarget=%10"
        " dualIso=%11"
        " disoValidity=%12"
        " disoPattern=%13"
        " disoAutoCorr=%14"
        " disoEvCorr=%15"
        " disoBlackDelta=%16"
        " disoAveraging=%17"
        " disoAliasMap=%18"
        " disoFrBlending=%19"
        " darkFrame=%20"
        " rawBlack=%21"
        " rawWhite=%22"
        "\n")
        .arg( llr->fix_raw )
        .arg( llr->focus_pixels )
        .arg( llr->fpi_method )
        .arg( llr->bad_pixels )
        .arg( llr->bps_method )
        .arg( llr->bpi_method )
        .arg( llr->chroma_smooth )
        .arg( llr->pattern_noise )
        .arg( llr->vertical_stripes )
        .arg( llr->deflicker_target )
        .arg( llr->dual_iso )
        .arg( llr->diso_validity )
        .arg( llr->diso_pattern )
        .arg( llr->diso_auto_correction )
        .arg( llr->diso_ev_correction, 0, 'f', 4 )
        .arg( llr->diso_black_delta )
        .arg( llr->diso_averaging )
        .arg( llr->diso_alias_map )
        .arg( llr->diso_frblending )
        .arg( llr->dark_frame )
        .arg( getMlvBlackLevel( mlvObject ) )
        .arg( getMlvWhiteLevel( mlvObject ) )
    );
}
