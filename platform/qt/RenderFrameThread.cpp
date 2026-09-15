/*!
 * \file RenderFrameThread.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief The render thread
 */

#include "RenderFrameThread.h"

#include "DecodeWorker.h"
#include "GpuDebayer.h"
#include "Phase3Breadcrumbs.h"
#include "PlaybackFrameRange.h"
#include "PlaybackQualityPolicy.h"
#include "ReconWorker.h"

#include "../../src/batch/WorkerThreadCount.h"
#include "../../src/debayer/debayer.h"
#include "../../src/mlv/llrawproc/llrawproc.h"
#include "../../src/processing/raw_processing.h"
#include "debug/FrameChecksum.h"
#include "debug/StageTimingCsvSink.h"
#include "debug/StageTiming.h"
#include "../../src/mlv/pipeline_stage_capture.h"
#include <QByteArray>
#include <QDebug>
#include <QDateTime>
#include <QMutexLocker>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>

extern "C" {
#include "../../src/debayer/wb_conversion.h"
}

namespace {

bool phase3StageCsvSinkEnsureOpen()
{
    if( stage_timing_csv_sink_enabled() != 0 ) return true;

    static std::atomic<bool> attempted{ false };
    bool expected = false;
    if( !attempted.compare_exchange_strong( expected, true, std::memory_order_acq_rel ) )
    {
        return stage_timing_csv_sink_enabled() != 0;
    }

    const QByteArray path = qgetenv( "MLVAPP_PHASE3_TEL_PATH" );
    if( path.isEmpty() ) return false;
    if( stage_timing_csv_sink_open( path.constData() ) == 0 )
    {
        qWarning() << "Could not open Phase 3 TEL CSV sink from MLVAPP_PHASE3_TEL_PATH";
        return false;
    }
    return true;
}

bool gpuTexNrOverlapTraceEnabled()
{
    const QByteArray value =
        qgetenv( "MLVAPP_GPU_TEX_NR_OVERLAP_TRACE" ).trimmed().toLower();
    return !value.isEmpty()
        && value != QByteArrayLiteral("0")
        && value != QByteArrayLiteral("false")
        && value != QByteArrayLiteral("off")
        && value != QByteArrayLiteral("no");
}

QString phase4bPathLabel( int path )
{
    /* Phase A2 (image-pipeline-hardening): injective over ALL render-path
     * codes. Previously only 8/4/3/2 were labelled and every other code
     * (0,5,6,7,9,10,11) collapsed to one "none-or-full-recon-fallback"
     * string, so six distinct reduced-proxy paths were indistinguishable
     * from a genuine full-recon frame. Strings mirror the code comments at
     * video_mlv.c:4062/4237/4266/4385/4419 (x1/x2 proxy cores). */
    switch( path )
    {
    case 8: return QStringLiteral("x8-full-xy-pre-recon");
    case 4: return QStringLiteral("x2-full-xy-pre-recon");
    case 3: return QStringLiteral("full-xy-pre-recon");
    case 2: return QStringLiteral("x-only-pre-recon");
    case 11: return QStringLiteral("x2-quarter-preview-quarter-proc");
    case 5: return QStringLiteral("x2-quarter-preview");
    case 10: return QStringLiteral("x1-quarter-preview-full-proc");
    case 9: return QStringLiteral("x1-quarter-preview-quarter-proc");
    case 7: return QStringLiteral("x1-half-preview-half-proc");
    case 6: return QStringLiteral("x1-half-preview-full-proc");
    case 0: return QStringLiteral("full-recon-or-none");
    default: return QStringLiteral("unknown-path");
    }
}

qint64 stagePixelCount( int width, int height )
{
    if( width <= 0 || height <= 0 ) return 0;
    return static_cast<qint64>( width ) * static_cast<qint64>( height );
}

bool playbackFrameWithinLookaheadWindow( uint32_t frameNumber,
                                         int activePlaybackTarget,
                                         int cutInFrame,
                                         int cutOutFrame,
                                         int lookaheadDepth,
                                         bool loopEnabled )
{
    if( activePlaybackTarget < 0 || lookaheadDepth < 0 ) return true;

    const int frame = static_cast<int>( frameNumber );
    const int depth = std::max( 0, lookaheadDepth );
    if( frame == activePlaybackTarget ) return true;

    if( cutOutFrame < cutInFrame )
    {
        return frame >= activePlaybackTarget
            && frame <= activePlaybackTarget + depth;
    }

    const int cutIn = std::max( 0, cutInFrame );
    const int cutOut = std::max( cutIn, cutOutFrame );
    if( activePlaybackTarget < cutIn || activePlaybackTarget > cutOut
     || frame < cutIn || frame > cutOut )
    {
        return frame >= activePlaybackTarget
            && frame <= activePlaybackTarget + depth;
    }

    if( !loopEnabled )
    {
        return frame >= activePlaybackTarget
            && frame <= std::min( cutOut, activePlaybackTarget + depth );
    }

    const int loopSpan = cutOut - cutIn + 1;
    if( loopSpan <= 0 ) return frame == activePlaybackTarget;

    int distance = frame - activePlaybackTarget;
    if( distance < 0 ) distance += loopSpan;
    return distance >= 0 && distance <= depth;
}

bool isGpuPlaybackReconTimingTelemetryKey( const QString &key )
{
    return key.startsWith( QStringLiteral("llrawproc_") )
        || key.startsWith( QStringLiteral("dual_iso_full20_") )
        || key == QStringLiteral("render_thread_recon_worker_llrawproc_total_ms")
        || key == QStringLiteral("render_thread_recon_worker_llrawproc_dual_iso_ms")
        || key == QStringLiteral("gpu_playback_recon_env_enabled")
        || key == QStringLiteral("gpu_playback_recon_attempted")
        || key == QStringLiteral("gpu_playback_recon_used")
        || key == QStringLiteral("gpu_playback_recon_state_valid")
        || key == QStringLiteral("gpu_playback_recon_rc")
        || key.startsWith(
            QStringLiteral("gpu_playback_recon_async_h2d_") );
}

void preserveGpuPlaybackReconTimingTelemetry( const QJsonObject &source,
                                              QJsonObject &target )
{
    for( auto it = source.begin(); it != source.end(); ++it )
    {
        if( isGpuPlaybackReconTimingTelemetryKey( it.key() ) )
        {
            target.insert( it.key(), it.value() );
        }
    }
}

void restoreGpuPlaybackReconTimingTelemetry( QJsonObject &target,
                                             const QJsonObject &preserved )
{
    if( preserved.isEmpty() ) return;

    QStringList staleKeys;
    for( auto it = target.begin(); it != target.end(); ++it )
    {
        if( isGpuPlaybackReconTimingTelemetryKey( it.key() ) )
        {
            staleKeys.push_back( it.key() );
        }
    }
    for( const QString &key : staleKeys )
    {
        target.remove( key );
    }
    for( auto it = preserved.begin(); it != preserved.end(); ++it )
    {
        target.insert( it.key(), it.value() );
    }
}

bool dualIsoWarmupInstrumentationEnabled()
{
    const QByteArray value =
        qgetenv( "MLVAPP_DISO_WARMUP_INSTRUMENT" ).trimmed().toLower();
    static const bool enabled =
        !value.isEmpty()
        && value != QByteArrayLiteral("0")
        && value != QByteArrayLiteral("false")
        && value != QByteArrayLiteral("off")
        && value != QByteArrayLiteral("no");
    return enabled;
}

const char *dualIsoFull20PathKindName( int pathKind )
{
    switch( pathKind )
    {
    case DUALISO_FULL20_PATH_CPU_FULL20:
        return "cpu_full20";
    case DUALISO_FULL20_PATH_GPU_PREPARE:
        return "gpu_prepare";
    default:
        return "none";
    }
}

const char *dualIsoFull20PatternSourceName( int patternSource )
{
    switch( patternSource )
    {
    case DUALISO_FULL20_PATTERN_SOURCE_FRESH_AUTO:
        return "fresh_auto";
    case DUALISO_FULL20_PATTERN_SOURCE_EXPLICIT_POSITIVE:
        return "explicit_positive";
    case DUALISO_FULL20_PATTERN_SOURCE_CACHED_NEGATIVE:
        return "cached_negative";
    case DUALISO_FULL20_PATTERN_SOURCE_CACHED_NEGATIVE_REDETECTED:
        return "cached_negative_redetected";
    case DUALISO_FULL20_PATTERN_SOURCE_SPECIAL_AUTO_DETECT:
        return "special_auto_detect";
    case DUALISO_FULL20_PATTERN_SOURCE_SPECIAL_AUTO_DEFAULT:
        return "special_auto_default";
    case DUALISO_FULL20_PATTERN_SOURCE_INVALID:
        return "invalid";
    default:
        return "none";
    }
}

void insertDualIsoWarmupInstrumentationTelemetry(
    QJsonObject &target,
    const dualiso_full20bit_timing_t &dualIsoFull20 )
{
    if( !dualIsoWarmupInstrumentationEnabled() ) return;

    target.insert( QStringLiteral("dual_iso_full20_path_kind"),
                   dualIsoFull20.path_kind );
    target.insert( QStringLiteral("dual_iso_full20_path_kind_name"),
                   QString::fromLatin1(
                       dualIsoFull20PathKindName( dualIsoFull20.path_kind ) ) );
    target.insert( QStringLiteral("dual_iso_full20_input_width"),
                   dualIsoFull20.input_width );
    target.insert( QStringLiteral("dual_iso_full20_input_height"),
                   dualIsoFull20.input_height );
    target.insert( QStringLiteral("dual_iso_full20_playback_preview_scale_factor"),
                   dualIsoFull20.playback_preview_scale_factor );
    target.insert( QStringLiteral("dual_iso_full20_pattern_initial"),
                   dualIsoFull20.pattern_initial );
    target.insert( QStringLiteral("dual_iso_full20_pattern_resolved"),
                   dualIsoFull20.pattern_resolved );
    target.insert( QStringLiteral("dual_iso_full20_pattern_source"),
                   dualIsoFull20.pattern_source );
    target.insert( QStringLiteral("dual_iso_full20_pattern_source_name"),
                   QString::fromLatin1(
                       dualIsoFull20PatternSourceName(
                           dualIsoFull20.pattern_source ) ) );
    target.insert( QStringLiteral("dual_iso_full20_pattern_result"),
                   dualIsoFull20.pattern_result );
    target.insert( QStringLiteral("dual_iso_full20_phase_verify_enabled"),
                   dualIsoFull20.phase_verify_enabled != 0 );
    target.insert( QStringLiteral("dual_iso_full20_phase_probe_attempted"),
                   dualIsoFull20.phase_probe_attempted != 0 );
    target.insert( QStringLiteral("dual_iso_full20_phase_probe_succeeded"),
                   dualIsoFull20.phase_probe_succeeded != 0 );
    target.insert( QStringLiteral("dual_iso_full20_phase_probe_decisive"),
                   dualIsoFull20.phase_probe_decisive != 0 );
    target.insert( QStringLiteral("dual_iso_full20_phase_probe_redetected"),
                   dualIsoFull20.phase_probe_redetected != 0 );
    target.insert( QStringLiteral("dual_iso_full20_phase_cached_pattern"),
                   dualIsoFull20.phase_cached_pattern );
    target.insert( QStringLiteral("dual_iso_full20_phase_implied_pattern"),
                   dualIsoFull20.phase_implied_pattern );
}

void insertLlrawprocReconTimingTelemetry( QJsonObject &target )
{
    const double llrawprocTotalMs = llrpGetLastTotalMilliseconds();
    const double llrawprocDarkFrameMs = llrpGetLastDarkFrameMilliseconds();
    const double llrawprocVerticalStripesMs = llrpGetLastVerticalStripesMilliseconds();
    const double llrawprocFocusPixelsMs = llrpGetLastFocusPixelsMilliseconds();
    const double llrawprocBadPixelsMs = llrpGetLastBadPixelsMilliseconds();
    const double llrawprocPatternNoiseMs = llrpGetLastPatternNoiseMilliseconds();
    const double llrawprocDualIsoMs = llrpGetLastDualIsoMilliseconds();
    const double llrawprocChromaSmoothMs = llrpGetLastChromaSmoothMilliseconds();
    const double llrawprocKnownMs =
        llrawprocDarkFrameMs
        + llrawprocVerticalStripesMs
        + llrawprocFocusPixelsMs
        + llrawprocBadPixelsMs
        + llrawprocPatternNoiseMs
        + llrawprocDualIsoMs
        + llrawprocChromaSmoothMs;
    dualiso_full20bit_timing_t dualIsoFull20 = {};
    llrpGetLastDualIsoFull20bitTiming( &dualIsoFull20 );

    target.insert( QStringLiteral("render_thread_recon_worker_llrawproc_total_ms"),
                   llrawprocTotalMs );
    target.insert( QStringLiteral("render_thread_recon_worker_llrawproc_dual_iso_ms"),
                   llrawprocDualIsoMs );
    target.insert( QStringLiteral("llrawproc_ms"), llrawprocTotalMs );
    target.insert( QStringLiteral("llrawproc_total_ms"), llrawprocTotalMs );
    target.insert( QStringLiteral("llrawproc_dark_frame_ms"), llrawprocDarkFrameMs );
    target.insert( QStringLiteral("llrawproc_vertical_stripes_ms"), llrawprocVerticalStripesMs );
    target.insert( QStringLiteral("llrawproc_focus_pixels_ms"), llrawprocFocusPixelsMs );
    target.insert( QStringLiteral("llrawproc_bad_pixels_ms"), llrawprocBadPixelsMs );
    target.insert( QStringLiteral("llrawproc_pattern_noise_ms"), llrawprocPatternNoiseMs );
    target.insert( QStringLiteral("llrawproc_dual_iso_ms"), llrawprocDualIsoMs );
    target.insert( QStringLiteral("llrawproc_chroma_smooth_ms"), llrawprocChromaSmoothMs );
    target.insert( QStringLiteral("llrawproc_other_ms"),
                   qMax( 0.0, llrawprocTotalMs - llrawprocKnownMs ) );
    target.insert( QStringLiteral("dual_iso_full20_valid"),
                   dualIsoFull20.valid != 0 );
    target.insert( QStringLiteral("dual_iso_full20_total_ms"), dualIsoFull20.total_ms );
    target.insert( QStringLiteral("dual_iso_full20_pattern_ms"), dualIsoFull20.pattern_ms );
    target.insert( QStringLiteral("dual_iso_full20_noise_ms"), dualIsoFull20.noise_ms );
    target.insert( QStringLiteral("dual_iso_full20_scratch_ms"), dualIsoFull20.scratch_ms );
    target.insert( QStringLiteral("dual_iso_full20_convert20_ms"), dualIsoFull20.convert20_ms );
    target.insert( QStringLiteral("dual_iso_full20_match_ms"), dualIsoFull20.match_ms );
    target.insert( QStringLiteral("dual_iso_full20_interp_ms"), dualIsoFull20.interp_ms );
    target.insert( QStringLiteral("dual_iso_full20_fullres_ms"), dualIsoFull20.fullres_ms );
    target.insert( QStringLiteral("dual_iso_full20_mix_ms"), dualIsoFull20.mix_ms );
    target.insert( QStringLiteral("dual_iso_full20_mix_curve_select_ms"),
                   dualIsoFull20.mix_curve_select_ms );
    target.insert( QStringLiteral("dual_iso_full20_mix_curve_build_ms"),
                   dualIsoFull20.mix_curve_build_ms );
    target.insert( QStringLiteral("dual_iso_full20_mix_curve_float_ms"),
                   dualIsoFull20.mix_curve_float_ms );
    target.insert( QStringLiteral("dual_iso_full20_mix_ev_lut_ms"),
                   dualIsoFull20.mix_ev_lut_ms );
    target.insert( QStringLiteral("dual_iso_full20_mix_halfres_ms"),
                   dualIsoFull20.mix_halfres_ms );
    target.insert( QStringLiteral("dual_iso_full20_mix_alias_map_ms"),
                   dualIsoFull20.mix_alias_map_ms );
    target.insert( QStringLiteral("dual_iso_full20_mix_overexposed_ms"),
                   dualIsoFull20.mix_overexposed_ms );
    target.insert( QStringLiteral("dual_iso_full20_final_blend_ms"),
                   dualIsoFull20.final_blend_ms );
    target.insert( QStringLiteral("dual_iso_full20_convert16_ms"),
                   dualIsoFull20.convert16_ms );
    target.insert( QStringLiteral("dual_iso_full20_other_ms"), dualIsoFull20.other_ms );
    target.insert( QStringLiteral("dual_iso_full20_interp_method"),
                   dualIsoFull20.interp_method );
    target.insert( QStringLiteral("dual_iso_full20_mix_halfres_probe_mode"),
                   dualIsoFull20.mix_halfres_probe_mode );
    target.insert( QStringLiteral("dual_iso_full20_final_blend_probe_mode"),
                   dualIsoFull20.final_blend_probe_mode );
    target.insert( QStringLiteral("dual_iso_full20_threads"), dualIsoFull20.threads );
    target.insert( QStringLiteral("dual_iso_full20_use_alias_map"),
                   dualIsoFull20.use_alias_map != 0 );
    target.insert( QStringLiteral("dual_iso_full20_use_fullres"),
                   dualIsoFull20.use_fullres != 0 );
    insertDualIsoWarmupInstrumentationTelemetry( target, dualIsoFull20 );
}

std::array<double, 3> normalizedWbMultipliers6500()
{
    std::array<double, 3> wb{{1.0, 1.0, 1.0}};
    get_kelvin_multipliers_rgb(6500, wb.data());
    const double maxWb = std::max(wb[0], std::max(wb[1], wb[2]));
    if( maxWb > 0.0
     && std::isfinite(wb[0])
     && std::isfinite(wb[1])
     && std::isfinite(wb[2]) )
    {
        wb[0] /= maxWb;
        wb[1] /= maxWb;
        wb[2] /= maxWb;
    }
    else
    {
        wb = {{1.0, 1.0, 1.0}};
    }
    return wb;
}

void insertStageResolutionTelemetry( QJsonObject &telemetry,
                                     const QString &prefix,
                                     const QString &domain,
                                     int width,
                                     int height,
                                     qint64 sourcePixels )
{
    const qint64 pixels = stagePixelCount( width, height );
    telemetry.insert( prefix + QStringLiteral("_domain"), domain );
    telemetry.insert( prefix + QStringLiteral("_width"), width );
    telemetry.insert( prefix + QStringLiteral("_height"), height );
    telemetry.insert( prefix + QStringLiteral("_pixels"), pixels );
    telemetry.insert( prefix + QStringLiteral("_pixel_retention_ratio"),
                      (sourcePixels > 0)
                          ? static_cast<double>( pixels ) / static_cast<double>( sourcePixels )
                          : 0.0 );
    telemetry.insert( prefix + QStringLiteral("_preview_resolution"),
                      sourcePixels > 0 && pixels > 0 && pixels < sourcePixels );
}

class GpuPlaybackReconScope
{
public:
    explicit GpuPlaybackReconScope( bool enabled )
    {
        llrpSetGpuPlaybackReconAllowedForCurrentThread( enabled ? 1 : 0 );
    }

    ~GpuPlaybackReconScope()
    {
        llrpSetGpuPlaybackReconAllowedForCurrentThread( 0 );
    }
};

class GpuPlaybackReconTexturePresentScope
{
public:
    explicit GpuPlaybackReconTexturePresentScope( bool enabled )
    {
        llrpSetGpuPlaybackReconTexturePresentPreferredForCurrentThread(
            enabled ? 1 : 0 );
    }

    ~GpuPlaybackReconTexturePresentScope()
    {
        llrpSetGpuPlaybackReconTexturePresentPreferredForCurrentThread( 0 );
    }
};

class GpuPlaybackReconTexturePrepareOnlyScope
{
public:
    explicit GpuPlaybackReconTexturePrepareOnlyScope( bool enabled )
    {
        llrpSetGpuPlaybackReconTexturePrepareOnlyForCurrentThread(
            enabled ? 1 : 0 );
    }

    ~GpuPlaybackReconTexturePrepareOnlyScope()
    {
        llrpSetGpuPlaybackReconTexturePrepareOnlyForCurrentThread( 0 );
    }
};

class GpuPlaybackReconFrameIdScope
{
public:
    explicit GpuPlaybackReconFrameIdScope( uint64_t frameId )
    {
        llrpSetGpuPlaybackReconFrameIdForCurrentThread( frameId );
    }

    ~GpuPlaybackReconFrameIdScope()
    {
        /* The C shim stores frame+1; UINT64_MAX therefore clears the token. */
        llrpSetGpuPlaybackReconFrameIdForCurrentThread( UINT64_MAX );
    }
};

bool gpuPlaybackReconEnvRequested()
{
    const QByteArray value = qgetenv("MLVAPP_GPU_PLAYBACK_RECON");
    if( value.isEmpty() || value == QByteArrayLiteral("0") )
    {
        return false;
    }
    return value.toLower() != QByteArrayLiteral("false");
}

bool gpuPlaybackReconAsyncH2dRequested()
{
    const QByteArray value =
        qgetenv("MLVAPP_GPU_PLAYBACK_RECON_ASYNC_H2D");
    if( value.isEmpty() || value == QByteArrayLiteral("0") )
    {
        return false;
    }
    return value.toLower() != QByteArrayLiteral("false");
}

void insertGpuPlaybackReconRunTelemetry( QJsonObject &target )
{
    target.insert(
        QStringLiteral("gpu_playback_recon_env_enabled"),
        gpuPlaybackReconEnvRequested() );
    target.insert(
        QStringLiteral("gpu_playback_recon_attempted"),
        llrpGpuPlaybackReconLastRunAttemptedForTesting() != 0 );
    target.insert(
        QStringLiteral("gpu_playback_recon_used"),
        llrpGpuPlaybackReconLastUsedForTesting() != 0 );
    target.insert(
        QStringLiteral("gpu_playback_recon_state_valid"),
        llrpGpuPlaybackReconLastStateValidForTesting() != 0 );
    target.insert(
        QStringLiteral("gpu_playback_recon_rc"),
        llrpGpuPlaybackReconLastRunRcForTesting() );
    llrpGpuPlaybackReconPreuploadStatus_t preupload;
    memset( &preupload, 0, sizeof( preupload ) );
    const bool preuploadAvailable =
        llrpGpuPlaybackReconGetLastPreuploadStatus( &preupload ) != 0;
    target.insert(
        QStringLiteral("gpu_playback_recon_async_h2d_env_enabled"),
        gpuPlaybackReconAsyncH2dRequested() );
    target.insert(
        QStringLiteral("gpu_playback_recon_async_h2d_available"),
        preuploadAvailable );
    target.insert(
        QStringLiteral("gpu_playback_recon_async_h2d_accepted"),
        preupload.accepted != 0 );
    target.insert(
        QStringLiteral("gpu_playback_recon_async_h2d_used"),
        preupload.used != 0 );
    target.insert(
        QStringLiteral("gpu_playback_recon_async_h2d_exact_match"),
        preupload.exact_match != 0 );
    target.insert(
        QStringLiteral("gpu_playback_recon_async_h2d_submitted_while_prior_run_active"),
        preupload.submitted_while_prior_run_active != 0 );
    target.insert(
        QStringLiteral("gpu_playback_recon_async_h2d_ready_before_run"),
        preupload.ready_before_run != 0 );
    target.insert(
        QStringLiteral("gpu_playback_recon_async_h2d_host_staging_ms"),
        preupload.host_staging_ms );
    target.insert(
        QStringLiteral("gpu_playback_recon_async_h2d_upload_ms"),
        preupload.upload_ms );
    target.insert(
        QStringLiteral("gpu_playback_recon_async_h2d_upload_wait_ms"),
        preupload.upload_wait_ms );
}

bool gpuPlaybackReconNoReadbackOutputValidationEnabled()
{
    static const bool enabled =
        qEnvironmentVariableIsSet( "MLVAPP_GPU_PLAYBACK_RECON_VALIDATE_OUTPUT" )
        && qEnvironmentVariable( "MLVAPP_GPU_PLAYBACK_RECON_VALIDATE_OUTPUT" )
           != QStringLiteral("0");
    return enabled;
}

bool gpuPlaybackReconReuseShadowsHighlightsFrameStateEnabled()
{
    static const bool enabled =
        qEnvironmentVariableIsSet(
            "MLVAPP_GPU_TEX_NR_REUSE_SHADOWS_HIGHLIGHTS_STATE" )
        && qEnvironmentVariable(
               "MLVAPP_GPU_TEX_NR_REUSE_SHADOWS_HIGHLIGHTS_STATE" )
           != QStringLiteral("0");
    return enabled;
}

bool gpuPlaybackReconFastShadowsHighlightsFrameStateEnabled()
{
    static const bool enabled =
        !qEnvironmentVariableIsSet(
            "MLVAPP_GPU_TEX_NR_FAST_SHADOWS_HIGHLIGHTS_STATE" )
        || qEnvironmentVariable(
               "MLVAPP_GPU_TEX_NR_FAST_SHADOWS_HIGHLIGHTS_STATE" )
           != QStringLiteral("0");
    return enabled;
}

bool gpuPlaybackReconDisplayLutOnlySkipShadowsHighlightsFrameStateEnabled()
{
    static const bool enabled =
        !qEnvironmentVariableIsSet(
            "MLVAPP_GPU_TEX_NR_DISPLAY_LUT_ONLY_SKIP_SH_STATE" )
        || qEnvironmentVariable(
               "MLVAPP_GPU_TEX_NR_DISPLAY_LUT_ONLY_SKIP_SH_STATE" )
           != QStringLiteral("0");
    return enabled;
}

bool playbackSmokeFrameTelemetryEnabled()
{
    static const bool enabled =
        qEnvironmentVariableIsSet( "MLVAPP_PLAYBACK_SMOKE_TELEMETRY" )
        && qEnvironmentVariable( "MLVAPP_PLAYBACK_SMOKE_TELEMETRY" )
           != QStringLiteral("0");
    return enabled;
}

bool playbackSmokeTimelineTelemetryEnabled()
{
    static const bool enabled =
        qEnvironmentVariableIsSet( "MLVAPP_PLAYBACK_SMOKE_TIMELINE_TELEMETRY" )
        && qEnvironmentVariable( "MLVAPP_PLAYBACK_SMOKE_TIMELINE_TELEMETRY" )
           != QStringLiteral("0");
    return enabled;
}

bool gpuPlaybackReconTextureLutSnapshotCacheEnabled()
{
    static const bool enabled =
        !qEnvironmentVariableIsSet( "MLVAPP_GPU_PLAYBACK_RECON_LUT_SNAPSHOT_CACHE" )
        || qEnvironmentVariable( "MLVAPP_GPU_PLAYBACK_RECON_LUT_SNAPSHOT_CACHE" )
           != QStringLiteral("0");
    return enabled;
}

bool gpuPlaybackReconShadowsHighlightsFrameStateAvailable(
    const GpuPreviewProcessingConfig &config,
    const processingObject_t *processing,
    int width,
    int height)
{
    if( !gpuPreviewProcessingNeedsShadowsHighlightsFrameState(config) )
    {
        return true;
    }
    const uint16_t *blurData = nullptr;
    int blurWidth = 0;
    int blurHeight = 0;
    if( !processingGetShadowsHighlightsBlurData(processing,
                                                &blurData,
                                                &blurWidth,
                                                &blurHeight,
                                                nullptr)
     || !blurData )
    {
        return false;
    }
    return blurWidth == width && blurHeight == height;
}

RenderFrameThread::GpuPlaybackReconTextureState::LutSnapshotKey
makeGpuPlaybackReconTextureLutKey( const llrpGpuPlaybackReconState_t &source,
                                   uint64_t processingGeneration )
{
    RenderFrameThread::GpuPlaybackReconTextureState::LutSnapshotKey key;
    key.raw2ev = source.raw2ev;
    key.ev2raw = source.ev2raw;
    key.mixCurve = source.mix_curve;
    key.fullresCurve = source.fullres_curve;
    key.randn05 = source.apply_dither ? source.randn05 : nullptr;
    key.width = source.width;
    key.height = source.height;
    key.blackLevel = source.black_level;
    key.whiteLevel = source.white_level;
    key.whiteDarkened = source.white_darkened;
    key.blackDelta = source.black_delta;
    key.evCorrection = source.ev_correction;
    key.darkNoise = source.dark_noise;
    key.interpMethod = source.interp_method;
    key.useAliasMap = source.use_alias_map != 0;
    key.useFullres = source.use_fullres != 0;
    key.chromaSmoothMethod = source.chroma_smooth_method;
    for( size_t i = 0; i < key.isBright.size(); ++i )
    {
        key.isBright[i] = source.is_bright[i];
    }
    key.applyDither = source.apply_dither != 0;
    key.processingGeneration = processingGeneration;
    return key;
}

template <size_t CacheSlots>
bool assignGpuPlaybackReconTextureState(
    RenderFrameThread::GpuPlaybackReconTextureState &destination,
    const llrpGpuPlaybackReconState_t &source,
    uint64_t processingGeneration,
    std::array<RenderFrameThread::GpuPlaybackReconTextureState::LutCacheEntry,
               CacheSlots> &lutCache,
    uint64_t &lutCacheUseCounter,
    bool *lutCacheHit,
    int *lutCacheEntryCount)
{
    destination = RenderFrameThread::GpuPlaybackReconTextureState();
    if( lutCacheHit )
    {
        *lutCacheHit = false;
    }
    if( lutCacheEntryCount )
    {
        int populatedEntries = 0;
        for( const auto &entry : lutCache )
        {
            if( entry.luts )
            {
                ++populatedEntries;
            }
        }
        *lutCacheEntryCount = populatedEntries;
    }
    if( !source.valid
     || source.width <= 0
     || source.height <= 0
     || !source.raw2ev
     || !source.ev2raw
     || !source.mix_curve
     || !source.fullres_curve
     || ( source.apply_dither && !source.randn05 ) )
    {
        return false;
    }

    const bool useLutCache = gpuPlaybackReconTextureLutSnapshotCacheEnabled();
    auto key = RenderFrameThread::GpuPlaybackReconTextureState::LutSnapshotKey();
    uint64_t useCounter = 0;
    std::shared_ptr<const RenderFrameThread::GpuPlaybackReconTextureState::LutSnapshot> luts;
    int populatedEntries = 0;
    if( useLutCache )
    {
        key = makeGpuPlaybackReconTextureLutKey( source, processingGeneration );
        ++lutCacheUseCounter;
        if( lutCacheUseCounter == 0 )
        {
            lutCacheUseCounter = 1;
        }
        useCounter = lutCacheUseCounter;

        for( auto &entry : lutCache )
        {
            if( entry.luts )
            {
                ++populatedEntries;
                if( !luts && entry.key == key )
                {
                    entry.lastUse = useCounter;
                    luts = entry.luts;
                    if( lutCacheHit )
                    {
                        *lutCacheHit = true;
                    }
                }
            }
        }
        if( lutCacheEntryCount )
        {
            *lutCacheEntryCount = populatedEntries;
        }
    }

    try
    {
        if( !luts )
        {
            std::shared_ptr<RenderFrameThread::GpuPlaybackReconTextureState::LutSnapshot> copiedLuts =
                std::make_shared<RenderFrameThread::GpuPlaybackReconTextureState::LutSnapshot>();
            copiedLuts->raw2ev.assign(
                source.raw2ev,
                source.raw2ev + LLRP_GPU_PLAYBACK_RECON_RAW2EV_COUNT );
            copiedLuts->ev2raw.assign(
                source.ev2raw,
                source.ev2raw + LLRP_GPU_PLAYBACK_RECON_EV2RAW_COUNT );
            copiedLuts->mixCurve.assign(
                source.mix_curve,
                source.mix_curve + LLRP_GPU_PLAYBACK_RECON_RAW2EV_COUNT );
            copiedLuts->fullresCurve.assign(
                source.fullres_curve,
                source.fullres_curve + LLRP_GPU_PLAYBACK_RECON_RAW2EV_COUNT );
            if( source.apply_dither )
            {
                copiedLuts->randn05.assign(
                    source.randn05,
                    source.randn05 + LLRP_GPU_PLAYBACK_RECON_RANDN05_COUNT );
            }

            if( useLutCache )
            {
                size_t targetIndex = 0;
                bool targetSelected = false;
                uint64_t oldestUse = 0;
                for( size_t i = 0; i < CacheSlots; ++i )
                {
                    const auto &entry = lutCache[i];
                    if( !entry.luts )
                    {
                        targetIndex = i;
                        targetSelected = true;
                        break;
                    }
                    if( !targetSelected || entry.lastUse < oldestUse )
                    {
                        targetIndex = i;
                        oldestUse = entry.lastUse;
                        targetSelected = true;
                    }
                }
                auto &target = lutCache[targetIndex];
                const bool wasEmpty = !target.luts;
                target.key = key;
                target.luts = copiedLuts;
                target.lastUse = useCounter;
                if( wasEmpty )
                {
                    ++populatedEntries;
                }
                if( lutCacheEntryCount )
                {
                    *lutCacheEntryCount = populatedEntries;
                }
            }
            luts = copiedLuts;
        }
        destination.luts = luts;
    }
    catch( const std::bad_alloc & )
    {
        destination = RenderFrameThread::GpuPlaybackReconTextureState();
        return false;
    }

    destination.valid = true;
    destination.width = source.width;
    destination.height = source.height;
    destination.blackLevel = source.black_level;
    destination.whiteLevel = source.white_level;
    destination.whiteDarkened = source.white_darkened;
    destination.blackDelta = source.black_delta;
    destination.evCorrection = source.ev_correction;
    destination.darkNoise = source.dark_noise;
    destination.interpMethod = source.interp_method;
    destination.useAliasMap = source.use_alias_map != 0;
    destination.useFullres = source.use_fullres != 0;
    destination.chromaSmoothMethod = source.chroma_smooth_method;
    for( size_t i = 0; i < destination.isBright.size(); ++i )
    {
        destination.isBright[i] = source.is_bright[i];
    }
    destination.applyDither = source.apply_dither != 0;
    destination.processingGeneration = processingGeneration;
    return true;
}

} // namespace

int RenderFrameThread::configuredFrameSlotCount( void )
{
    bool ok = false;
    const int requested =
        qEnvironmentVariableIntValue( "MLVAPP_PHASE3_FRAME_SLOT_COUNT", &ok );
    if( !ok || requested <= 0 ) return kDefaultFrameSlotCount;
    return qBound( 1, requested, kMaxFrameSlotCount );
}

//Constructor
RenderFrameThread::RenderFrameThread()
{
    m_stop = false;
    m_initialized = false;
    m_renderFrame = false;
    m_renderingFrame = false;
    m_frameReady = false;
    m_pMlvObject = nullptr;
    m_activeOutputMode = OutputProcessed8;
    m_activeUseGpuBilinearDebayer = false;
    m_activeUseGpuAmazeDebayer = false;
    m_activeFrameNumber = 0;
    m_activeFrameRequestSerial = 0;
    m_activePresentationContext = ReadyFrame::PresentationContext();
    m_activePresentationPreparationOptions = PresentationPreparationOptions();
    m_activeQueuedPlaybackDropCount = 0;
    m_loggedGpuBilinearSuccess = false;
    m_loggedGpuAmazeSuccess = false;
    m_lastFrameUsedGpuBilinearDebayer = false;
    m_lastFrameUsedGpuAmazeDebayer = false;
    m_lastDualIsoPreviewHistogramMs = 0.0;
    m_lastDualIsoPreviewRegressionMs = 0.0;
    m_lastDualIsoPreviewRowscaleMs = 0.0;
    m_activeFrameRequestStageTime = 0.0;
    m_lastRenderThreadQueueWaitMs = 0.0;
    m_lastRenderThreadWorkMs = 0.0;
    m_lastRenderThreadTotalMs = 0.0;
    m_lastFrameReadyEmitStageTime = 0.0;
    m_imageWidth = 0;
    m_imageHeight = 0;
    m_renderingSlotIndex = -1;
    m_presentingSlotIndex = -1;
    m_phase3Mode.store( Phase3Mode::Disabled, std::memory_order_relaxed );
    m_decodeWorker = nullptr;
    m_reconWorker = nullptr;
    m_decodeWorkerStop = false;
    m_reconWorkerStop = false;
    m_frameSlots.reset( configuredFrameSlotCount() );
}

//Destructor
RenderFrameThread::~RenderFrameThread()
{
    stop();
    if( m_decodeWorker )
    {
        m_decodeWorker->wait();
        delete m_decodeWorker;
        m_decodeWorker = nullptr;
    }
    if( m_reconWorker )
    {
        m_reconWorker->wait();
        delete m_reconWorker;
        m_reconWorker = nullptr;
    }
}

//Init all objects
void RenderFrameThread::init(mlvObject_t *pMlvObject, int imageWidth, int imageHeight)
{
    QMutexLocker locker(&m_mutex);
    m_frameReady = false;
    m_renderRequests.clear();
    m_decodeRequests.clear();
    m_reconRequests.clear();
    m_decodeReadySlots.clear();
    m_processReadySlots.clear();
    m_decodeWorkerStop = false;
    m_reconWorkerStop = false;
    m_pMlvObject = pMlvObject;
    m_imageWidth = imageWidth;
    m_imageHeight = imageHeight;
    m_activeUseGpuBilinearDebayer = false;
    m_activeUseGpuAmazeDebayer = false;
    m_activeFrameNumber = 0;
    m_activeFrameRequestSerial = 0;
    m_activeOutputMode = OutputProcessed8;
    m_activePresentationContext = ReadyFrame::PresentationContext();
    m_activePresentationPreparationOptions = PresentationPreparationOptions();
    m_activeQueuedPlaybackDropCount = 0;
    m_renderingSlotIndex = -1;
    m_presentingSlotIndex = -1;
    m_lastFrameUsedGpuBilinearDebayer = false;
    m_lastGpuBilinearFallbackReason.clear();
    m_lastGpuBilinearRendererDescription.clear();
    m_lastFrameUsedGpuAmazeDebayer = false;
    m_lastGpuAmazeFallbackReason.clear();
    m_lastGpuAmazeRendererDescription.clear();
    m_lastDualIsoPreviewHistogramMs = 0.0;
    m_lastDualIsoPreviewRegressionMs = 0.0;
    m_lastDualIsoPreviewRowscaleMs = 0.0;
    m_activeFrameRequestStageTime = 0.0;
    m_lastRenderThreadQueueWaitMs = 0.0;
    m_lastRenderThreadWorkMs = 0.0;
    m_lastRenderThreadTotalMs = 0.0;
    m_lastFrameReadyEmitStageTime = 0.0;
    const int configuredSlots = configuredFrameSlotCount();
    if( m_frameSlots.size() != configuredSlots )
    {
        m_frameSlots.reset( configuredSlots );
    }
    m_gpuBilinearDebayerRawFrame.clear();
    m_gpuAmazeDebayerRawFrame.clear();
    m_gpuPlaybackReconTextureLutCache = {};
    m_gpuPlaybackReconTextureLutCacheUseCounter = 0;
    m_gpuPlaybackReconTextureLutSnapshotGeneration.store(
        1, std::memory_order_release );
    const size_t pixelCount =
        static_cast<size_t>(qMax(0, imageWidth)) * static_cast<size_t>(qMax(0, imageHeight));
    const size_t rgbPixelCount = pixelCount * 3u;
    for( FrameSlot &slot : m_frameSlots )
    {
        slot.rawImage8.assign( rgbPixelCount, 0u );
        slot.rawImage16.assign( rgbPixelCount, 0u );
        slot.resetMetadata();
        slot.state.store( SlotState::Idle, std::memory_order_release );
    }
}

void RenderFrameThread::setGpuPlaybackReconTextureLutSnapshotGeneration(
    uint64_t generation )
{
    if( generation == 0 )
    {
        generation = 1;
    }
    uint64_t current = m_gpuPlaybackReconTextureLutSnapshotGeneration.load(
        std::memory_order_acquire );
    for( ;; )
    {
        uint64_t next = current + 1;
        if( next == 0 )
        {
            next = 1;
        }
        if( generation > next )
        {
            next = generation;
        }
        if( m_gpuPlaybackReconTextureLutSnapshotGeneration.compare_exchange_weak(
                current,
                next,
                std::memory_order_acq_rel,
                std::memory_order_acquire ) )
        {
            return;
        }
    }
}

//Start rendering
void RenderFrameThread::renderFrame(uint32_t frameNumber,
                                    OutputMode outputMode,
                                    bool useGpuBilinearDebayer,
                                    bool useGpuAmazeDebayer,
                                    uint64_t requestSerial,
                                    const ReadyFrame::PresentationContext &presentationContext,
                                    const PresentationPreparationOptions &presentationPreparation)
{
    const bool detailedTimelineTelemetry =
        playbackSmokeTimelineTelemetryEnabled();
    const bool requestStateTelemetry =
        playbackSmokeFrameTelemetryEnabled();
    const double renderFrameEntryStageTime =
        detailedTimelineTelemetry ? mlv_stage_timing_now() : 0.0;
    QMutexLocker locker(&m_mutex);
    const int totalFrames = m_pMlvObject ? getMlvFrames( m_pMlvObject ) : 0;
    if( !playback_frame_range::isValidFrameNumber( frameNumber, totalFrames ) )
    {
        qWarning() << "RenderFrameThread rejected invalid frame request"
                   << "frame=" << frameNumber
                   << "total_frames=" << totalFrames
                   << "request_serial=" << requestSerial
                   << "playback_active=" << presentationContext.playbackActive
                   << "drop_frame=" << presentationContext.dropFramePlaybackActive;
        return;
    }

    RenderRequest request;
    request.frameNumber = frameNumber;
    request.outputMode = outputMode;
    request.useGpuBilinearDebayer = useGpuBilinearDebayer;
    request.useGpuAmazeDebayer = useGpuAmazeDebayer;
    request.requestSerial = requestSerial;
    request.renderFrameEntryStageTime = renderFrameEntryStageTime;
    request.requestStageTime = mlv_stage_timing_now();
    request.phase3Mode = phase3Mode();
    request.presentationContext = presentationContext;
    request.presentationPreparationOptions = presentationPreparation;
    if( requestStateTelemetry )
    {
        request.renderThreadRenderingAtRequest = m_renderingFrame;
        request.renderThreadQueuedAtRequest = m_renderFrame || !m_renderRequests.empty();
        request.phase3WorkInFlightAtRequest = phase3WorkInFlightLocked();
        request.renderThreadBusyAtRequest =
            request.renderThreadRenderingAtRequest
            || request.renderThreadQueuedAtRequest
            || request.phase3WorkInFlightAtRequest;
        request.renderRequestQueueDepthAtRequest =
            static_cast<int>( m_renderRequests.size() );
        request.decodeRequestCountAtRequest =
            static_cast<int>( m_decodeRequests.size() );
        request.reconRequestCountAtRequest =
            static_cast<int>( m_reconRequests.size() );
        request.decodeReadySlotCountAtRequest =
            static_cast<int>( m_decodeReadySlots.size() );
        request.processReadySlotCountAtRequest =
            static_cast<int>( m_processReadySlots.size() );
        for( int i = 0; i < static_cast<int>( m_frameSlots.size() ); ++i )
        {
            const FrameSlot &slot = m_frameSlots[i];
            const SlotState state = slot.state.load( std::memory_order_acquire );
            if( i != m_renderingSlotIndex
             && !slot.ready
             && !slot.presenting
             && state == SlotState::Idle )
            {
                ++request.freeSlotCountAtRequest;
            }
            if( slot.ready ) ++request.readySlotCountAtRequest;
            if( slot.presenting ) ++request.presentingSlotCountAtRequest;
            if( state == SlotState::Requested
             || state == SlotState::Decoding
             || state == SlotState::Decoded
             || state == SlotState::ReconReady
             || state == SlotState::Recon
             || state == SlotState::ProcessReady
             || state == SlotState::Processing )
            {
                ++request.phase3ActiveSlotCountAtRequest;
            }
        }
    }

    if( request.presentationContext.dropFramePlaybackActive )
    {
        for( auto it = m_renderRequests.begin(); it != m_renderRequests.end(); )
        {
            if( ( it->presentationContext.dropFramePlaybackActive
               || it->presentationContext.playbackLookaheadRequest )
             && it->presentationContext.presentationGeneration
                == request.presentationContext.presentationGeneration )
            {
                it = m_renderRequests.erase( it );
                ++request.queuedPlaybackDropCount;
            }
            else
            {
                ++it;
            }
        }
    }

    if( static_cast<int>(m_renderRequests.size()) >= kRenderRequestQueueDepth )
    {
        if( request.presentationContext.dropFramePlaybackActive )
            ++request.queuedPlaybackDropCount;
        m_renderRequests.pop_front();
    }
    request.requestQueuePushStageTime =
        detailedTimelineTelemetry ? mlv_stage_timing_now() : 0.0;
    m_renderRequests.push_back( request );
    m_renderFrame = true;
    m_waitCondition.wakeAll();
}

//Is rendering finished?
bool RenderFrameThread::isFrameReady()
{
    QMutexLocker locker(&m_mutex);
    return m_frameReady;
}

//Returns if there is a frame in the pipeline...
bool RenderFrameThread::isIdle()
{
    QMutexLocker locker(&m_mutex);
    return !(m_renderFrame || m_renderingFrame || phase3WorkInFlightLocked());
}

bool RenderFrameThread::acquireLatestReadyFrame(ReadyFrame *frame)
{
    QMutexLocker locker(&m_mutex);
    const int readySlotIndex = findLatestReadySlotLocked();
    if( readySlotIndex < 0 )
    {
        m_frameReady = false;
        return false;
    }

    return acquireReadySlotLocked( frame, readySlotIndex, true );
}

bool RenderFrameThread::acquireOldestGpuTextureNoReadbackReadyFrame(ReadyFrame *frame)
{
    QMutexLocker locker(&m_mutex);
    const int readySlotIndex = findOldestReadySlotLocked();
    if( readySlotIndex < 0 )
    {
        m_frameReady = false;
        return false;
    }

    const FrameSlot &slot = m_frameSlots[readySlotIndex];
    const bool orderedGpuTextureNoReadback =
        slot.gpuPlaybackReconTextureNoReadbackCandidate
        && slot.presentationContext.playbackActive
        && slot.presentationContext.gpuPlaybackReconTexturePresentRequested
        && slot.presentationContext.gpuPlaybackReconAmazeTexturePresentAdmitted;
    if( !orderedGpuTextureNoReadback )
    {
        return false;
    }

    return acquireReadySlotLocked( frame, readySlotIndex, false );
}

bool RenderFrameThread::acquireLatestGpuTextureNoReadbackReadyFrame(ReadyFrame *frame)
{
    QMutexLocker locker(&m_mutex);
    const int readySlotIndex = findLatestReadySlotLocked();
    if( readySlotIndex < 0 )
    {
        m_frameReady = false;
        return false;
    }

    const FrameSlot &slot = m_frameSlots[readySlotIndex];
    const bool latestGpuTextureNoReadback =
        slot.gpuPlaybackReconTextureNoReadbackCandidate
        && slot.presentationContext.playbackActive
        && slot.presentationContext.gpuPlaybackReconTexturePresentRequested
        && slot.presentationContext.gpuPlaybackReconAmazeTexturePresentAdmitted
        && slot.stageTimingTelemetry.value(
            QStringLiteral("render_thread_cpu_amaze_debayer_skipped_for_gpu_tex_nr") )
              .toBool( false );
    if( !latestGpuTextureNoReadback )
    {
        return false;
    }

    return acquireReadySlotLocked( frame, readySlotIndex, true );
}

bool RenderFrameThread::acquireOldestGpuTextureNoReadbackReadyFrameForPlaybackTarget(
    ReadyFrame *frame,
    int activePlaybackTarget,
    uint64_t activeGeneration )
{
    QMutexLocker locker(&m_mutex);
    int readySlotIndex = -1;
    uint64_t oldestRequestSerial = 0;
    for( int i = 0; i < static_cast<int>( m_frameSlots.size() ); ++i )
    {
        const FrameSlot &slot = m_frameSlots[i];
        if( !slot.ready ) continue;
        if( !slot.gpuPlaybackReconTextureNoReadbackCandidate ) continue;
        if( !slot.presentationContext.playbackActive
         || !slot.presentationContext.gpuPlaybackReconTexturePresentRequested
         || !slot.presentationContext.gpuPlaybackReconAmazeTexturePresentAdmitted )
        {
            continue;
        }
        if( !slot.stageTimingTelemetry.value(
                QStringLiteral("render_thread_cpu_amaze_debayer_skipped_for_gpu_tex_nr") )
                  .toBool( false ) )
        {
            continue;
        }
        if( slot.presentationContext.playbackLookaheadRequest
         && ( activePlaybackTarget < 0
           || slot.presentationContext.presentationGeneration != activeGeneration
           || static_cast<int>( slot.frameNumber ) != activePlaybackTarget ) )
        {
            continue;
        }
        if( readySlotIndex < 0 || slot.requestSerial < oldestRequestSerial )
        {
            readySlotIndex = i;
            oldestRequestSerial = slot.requestSerial;
        }
    }
    if( readySlotIndex < 0 )
    {
        m_frameReady = (findLatestReadySlotLocked() >= 0);
        return false;
    }

    return acquireReadySlotLocked( frame, readySlotIndex, false );
}

bool RenderFrameThread::acquireLatestReadyFrameForPlaybackTarget(
    ReadyFrame *frame,
    int activePlaybackTarget,
    uint64_t activeGeneration )
{
    QMutexLocker locker(&m_mutex);
    int readySlotIndex = -1;
    uint64_t latestRequestSerial = 0;
    for( int i = 0; i < static_cast<int>( m_frameSlots.size() ); ++i )
    {
        const FrameSlot &slot = m_frameSlots[i];
        if( !slot.ready ) continue;
        if( slot.presentationContext.playbackLookaheadRequest
         && ( activePlaybackTarget < 0
           || slot.presentationContext.presentationGeneration != activeGeneration
           || static_cast<int>( slot.frameNumber ) != activePlaybackTarget ) )
        {
            continue;
        }
        if( readySlotIndex < 0 || slot.requestSerial >= latestRequestSerial )
        {
            readySlotIndex = i;
            latestRequestSerial = slot.requestSerial;
        }
    }
    if( readySlotIndex < 0 )
    {
        m_frameReady = (findLatestReadySlotLocked() >= 0);
        return false;
    }

    return acquireReadySlotLocked( frame, readySlotIndex, false );
}

bool RenderFrameThread::hasGpuTextureNoReadbackReadyFrame()
{
    QMutexLocker locker(&m_mutex);
    const int readySlotIndex = findOldestReadySlotLocked();
    if( readySlotIndex < 0 ) return false;

    const FrameSlot &slot = m_frameSlots[readySlotIndex];
    return slot.gpuPlaybackReconTextureNoReadbackCandidate
        && slot.presentationContext.playbackActive
        && slot.presentationContext.gpuPlaybackReconTexturePresentRequested
        && slot.presentationContext.gpuPlaybackReconAmazeTexturePresentAdmitted
        && slot.stageTimingTelemetry.value(
            QStringLiteral("render_thread_cpu_amaze_debayer_skipped_for_gpu_tex_nr") )
              .toBool( false );
}

bool RenderFrameThread::hasPlaybackLookaheadRequest( uint32_t frameNumber,
                                                     uint64_t activeGeneration )
{
    QMutexLocker locker(&m_mutex);
    for( const RenderRequest &request : m_renderRequests )
    {
        if( request.frameNumber == frameNumber
         && request.presentationContext.playbackLookaheadRequest
         && request.presentationContext.presentationGeneration == activeGeneration )
        {
            return true;
        }
    }
    for( int i = 0; i < static_cast<int>( m_frameSlots.size() ); ++i )
    {
        const FrameSlot &slot = m_frameSlots[i];
        const SlotState state = slot.state.load( std::memory_order_acquire );
        if( state == SlotState::Idle && !slot.ready && !slot.presenting ) continue;
        if( slot.frameNumber == frameNumber
         && slot.presentationContext.playbackLookaheadRequest
         && slot.presentationContext.presentationGeneration == activeGeneration )
        {
            return true;
        }
    }
    return false;
}

bool RenderFrameThread::hasReadyPlaybackLookaheadFrame( uint32_t frameNumber,
                                                        uint64_t activeGeneration )
{
    QMutexLocker locker(&m_mutex);
    for( int i = 0; i < static_cast<int>( m_frameSlots.size() ); ++i )
    {
        const FrameSlot &slot = m_frameSlots[i];
        if( !slot.ready ) continue;
        if( slot.state.load( std::memory_order_acquire ) != SlotState::Ready )
            continue;
        if( slot.frameNumber == frameNumber
         && slot.presentationContext.playbackLookaheadRequest
         && slot.presentationContext.presentationGeneration == activeGeneration )
        {
            return true;
        }
    }
    return false;
}

int RenderFrameThread::prunePlaybackLookaheadOutsideForwardWindow(
    int activePlaybackTarget,
    uint64_t activeGeneration,
    int cutInFrame,
    int cutOutFrame,
    int lookaheadDepth,
    bool loopEnabled )
{
    QMutexLocker locker(&m_mutex);
    int pruned = 0;
    for( auto it = m_renderRequests.begin(); it != m_renderRequests.end(); )
    {
        const ReadyFrame::PresentationContext &context = it->presentationContext;
        if( context.playbackLookaheadRequest
         && context.presentationGeneration == activeGeneration
         && !playbackFrameWithinLookaheadWindow( it->frameNumber,
                                                 activePlaybackTarget,
                                                 cutInFrame,
                                                 cutOutFrame,
                                                 lookaheadDepth,
                                                 loopEnabled ) )
        {
            it = m_renderRequests.erase( it );
            ++pruned;
        }
        else
        {
            ++it;
        }
    }
    m_renderFrame = !m_renderRequests.empty();

    for( int i = 0; i < static_cast<int>( m_frameSlots.size() ); ++i )
    {
        FrameSlot &slot = m_frameSlots[i];
        if( !slot.ready ) continue;
        if( slot.state.load( std::memory_order_acquire ) != SlotState::Ready )
            continue;
        if( !slot.presentationContext.playbackLookaheadRequest
         || slot.presentationContext.presentationGeneration != activeGeneration )
        {
            continue;
        }
        if( playbackFrameWithinLookaheadWindow( slot.frameNumber,
                                                activePlaybackTarget,
                                                cutInFrame,
                                                cutOutFrame,
                                                lookaheadDepth,
                                                loopEnabled ) )
        {
            continue;
        }
        releaseSlotLocked( i );
        ++pruned;
    }

    m_frameReady = (findLatestReadySlotLocked() >= 0);
    if( pruned > 0 ) m_waitCondition.wakeAll();
    return pruned;
}

int RenderFrameThread::cancelPlaybackPresentationRequests( uint64_t presentationGeneration )
{
    QMutexLocker locker(&m_mutex);
    int cancelled = 0;
    for( auto it = m_renderRequests.begin(); it != m_renderRequests.end(); )
    {
        if( it->presentationContext.playbackActive
         && it->presentationContext.presentationGeneration == presentationGeneration )
        {
            it = m_renderRequests.erase( it );
            ++cancelled;
        }
        else
        {
            ++it;
        }
    }
    m_renderFrame = !m_renderRequests.empty();

    for( int i = 0; i < static_cast<int>( m_frameSlots.size() ); ++i )
    {
        FrameSlot &slot = m_frameSlots[i];
        if( !slot.presentationContext.playbackActive
         || slot.presentationContext.presentationGeneration != presentationGeneration )
        {
            continue;
        }
        const SlotState state = slot.state.load( std::memory_order_acquire );
        if( state != SlotState::Ready && state != SlotState::Presenting )
        {
            continue;
        }
        releaseSlotLocked( i );
        if( m_presentingSlotIndex == i ) m_presentingSlotIndex = -1;
        ++cancelled;
    }

    m_frameReady = (findLatestReadySlotLocked() >= 0);
    m_waitCondition.wakeAll();
    return cancelled;
}

uint64_t RenderFrameThread::decodeRequestsIssuedCount() const
{
    return m_decodeRequestsIssuedCount.load( std::memory_order_acquire );
}

int RenderFrameThread::gpuTextureNoReadbackReadyFrameCount()
{
    QMutexLocker locker(&m_mutex);
    int count = 0;
    for( int i = 0; i < static_cast<int>( m_frameSlots.size() ); ++i )
    {
        const FrameSlot &slot = m_frameSlots[i];
        if( !slot.ready ) continue;
        if( slot.state.load( std::memory_order_acquire ) != SlotState::Ready )
            continue;
        if( !slot.gpuPlaybackReconTextureNoReadbackCandidate )
            continue;
        if( !slot.presentationContext.playbackActive
         || !slot.presentationContext.gpuPlaybackReconTexturePresentRequested
         || !slot.presentationContext.gpuPlaybackReconAmazeTexturePresentAdmitted )
        {
            continue;
        }
        if( !slot.stageTimingTelemetry.value(
                QStringLiteral("render_thread_cpu_amaze_debayer_skipped_for_gpu_tex_nr") )
                  .toBool( false ) )
        {
            continue;
        }
        ++count;
    }
    return count;
}

bool RenderFrameThread::acquireReadySlotLocked( ReadyFrame *frame,
                                                int readySlotIndex,
                                                bool discardOlderReady )
{
    if( readySlotIndex < 0 || readySlotIndex >= static_cast<int>(m_frameSlots.size()) )
    {
        m_frameReady = (findLatestReadySlotLocked() >= 0);
        return false;
    }

    FrameSlot &slot = m_frameSlots[readySlotIndex];
    slot.ready = false;
    slot.presenting = true;
    if( slot.state.load( std::memory_order_acquire ) == SlotState::Ready )
    {
        transitionSlotState( readySlotIndex,
                             SlotState::Ready,
                             SlotState::Presenting,
                             slot.phase3Mode,
                             slot.frameNumber,
                             slot.requestSerial,
                             "phase3-presenting" );
    }
    m_presentingSlotIndex = readySlotIndex;
    int preservedReadySlots = 0;
    if( discardOlderReady )
    {
        for( int i = 0; i < static_cast<int>( m_frameSlots.size() ); ++i )
        {
            if( i == readySlotIndex ) continue;
            FrameSlot &older = m_frameSlots[i];
            if( !older.ready ) continue;
            if( older.requestSerial >= slot.requestSerial ) continue;
            if( older.state.load( std::memory_order_acquire ) != SlotState::Ready ) continue;
            transitionSlotState( i,
                                 SlotState::Ready,
                                 SlotState::Idle,
                                 older.phase3Mode,
                                 older.frameNumber,
                                 older.requestSerial,
                                 "phase3-stale-ready" );
            if( older.gpuPlaybackReconTextureRetainedDeviceToken != 0 )
            {
                llrpGpuPlaybackReconReleaseRetainedDeviceBayer16(
                    older.gpuPlaybackReconTextureRetainedDeviceToken );
                older.gpuPlaybackReconTextureRetainedDeviceBayer16 = nullptr;
                older.gpuPlaybackReconTextureRetainedDeviceWidth = 0;
                older.gpuPlaybackReconTextureRetainedDeviceHeight = 0;
                older.gpuPlaybackReconTextureRetainedDeviceToken = 0;
            }
            older.ready = false;
            older.presenting = false;
        }
    }
    for( int i = 0; i < static_cast<int>( m_frameSlots.size() ); ++i )
    {
        if( i == readySlotIndex ) continue;
        if( m_frameSlots[i].ready ) ++preservedReadySlots;
    }
    m_frameReady = (findLatestReadySlotLocked() >= 0);
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_ready_acquire_ordered"),
        !discardOlderReady );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_ready_acquire_preserved_ready_slots"),
        preservedReadySlots );
    copySlotTelemetryLocked( slot );
    m_waitCondition.wakeAll();

    if( frame )
    {
        frame->rawImage8 = slot.rawImage8.empty() ? nullptr : slot.rawImage8.data();
        frame->rawImage16 = slot.rawImage16.empty() ? nullptr : slot.rawImage16.data();
        frame->rawImage16Words = slot.rawImage16.size();
        frame->playbackScaledImage8 =
            slot.playbackScaledImage8.empty() ? nullptr : slot.playbackScaledImage8.data();
        frame->frameNumber = slot.frameNumber;
        frame->requestSerial = slot.requestSerial;
        frame->outputMode = slot.outputMode;
        frame->renderedImageWidth = slot.renderedImageWidth;
        frame->renderedImageHeight = slot.renderedImageHeight;
        frame->playbackScaleFactorActive = slot.playbackScaleFactorActive;
        frame->playbackFastScaleActive = slot.playbackFastScaleActive;
        frame->playbackScaledWidth = slot.playbackScaledWidth;
        frame->playbackScaledHeight = slot.playbackScaledHeight;
        frame->playbackScaledBytesPerLine = slot.playbackScaledBytesPerLine;
        frame->usedGpuBilinearDebayer = slot.usedGpuBilinearDebayer;
        frame->gpuBilinearFallbackReason = slot.gpuBilinearFallbackReason;
        frame->gpuBilinearRendererDescription = slot.gpuBilinearRendererDescription;
        frame->usedGpuAmazeDebayer = slot.usedGpuAmazeDebayer;
        frame->gpuAmazeFallbackReason = slot.gpuAmazeFallbackReason;
        frame->gpuAmazeRendererDescription = slot.gpuAmazeRendererDescription;
        frame->gpuAmazeTexturePresentCandidate = slot.gpuAmazeTexturePresentCandidate;
        frame->gpuAmazeTextureRawFrame =
            slot.gpuAmazeTextureRawFrame.empty() ? nullptr : slot.gpuAmazeTextureRawFrame.data();
        frame->gpuAmazeTextureRawFrameSize = slot.gpuAmazeTextureRawFrame.size();
        frame->gpuAmazeTextureWidth = slot.gpuAmazeTextureWidth;
        frame->gpuAmazeTextureHeight = slot.gpuAmazeTextureHeight;
        frame->gpuAmazeTextureBlackLevel = slot.gpuAmazeTextureBlackLevel;
        frame->gpuAmazeTextureWbMultipliers = slot.gpuAmazeTextureWbMultipliers;
        frame->gpuPlaybackReconTexturePresentCandidate = slot.gpuPlaybackReconTexturePresentCandidate;
        frame->gpuPlaybackReconTextureNoReadbackCandidate =
            slot.gpuPlaybackReconTextureNoReadbackCandidate;
        /* The recon-output bayer oracle must come from the dedicated snapshot
         * taken right after the recon stage (slot.rawImage16 has since been
         * overwritten by the process stage with the processed display frame).
         * Falls back to nullptr when no snapshot was captured. */
        frame->gpuPlaybackReconTextureBayerFrame =
            slot.gpuPlaybackReconTexturePresentCandidate
            && !slot.gpuPlaybackReconTextureBayerFrame.empty()
                ? slot.gpuPlaybackReconTextureBayerFrame.data()
                : nullptr;
        frame->gpuPlaybackReconTextureBayerFrameSize =
            frame->gpuPlaybackReconTextureBayerFrame
                ? slot.gpuPlaybackReconTextureBayerFrame.size()
                : 0;
        frame->gpuPlaybackReconTextureInputBayerFrame =
            slot.gpuPlaybackReconTextureNoReadbackCandidate
                ? ( slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16
                    ? ( slot.rawImage16.empty() ? nullptr : slot.rawImage16.data() )
                    : ( slot.gpuPlaybackReconTextureInputBayerFrame.empty()
                        ? nullptr
                        : slot.gpuPlaybackReconTextureInputBayerFrame.data() ) )
                : nullptr;
        frame->gpuPlaybackReconTextureInputBayerFrameSize =
            frame->gpuPlaybackReconTextureInputBayerFrame
                ? ( slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16
                    ? slot.rawImage16.size()
                    : slot.gpuPlaybackReconTextureInputBayerFrame.size() )
                : 0;
        frame->gpuPlaybackReconTextureRetainedDeviceBayer16 =
            slot.gpuPlaybackReconTextureRetainedDeviceBayer16;
        frame->gpuPlaybackReconTextureRetainedDeviceWidth =
            slot.gpuPlaybackReconTextureRetainedDeviceWidth;
        frame->gpuPlaybackReconTextureRetainedDeviceHeight =
            slot.gpuPlaybackReconTextureRetainedDeviceHeight;
        frame->gpuPlaybackReconTextureRetainedDeviceToken =
            slot.gpuPlaybackReconTextureRetainedDeviceToken;
        frame->gpuPlaybackReconTextureWidth = slot.gpuPlaybackReconTextureWidth;
        frame->gpuPlaybackReconTextureHeight = slot.gpuPlaybackReconTextureHeight;
        frame->gpuPlaybackReconTextureBlackLevel = slot.gpuPlaybackReconTextureBlackLevel;
        frame->gpuPlaybackReconTextureWbMultipliers =
            slot.gpuPlaybackReconTextureWbMultipliers;
        frame->gpuPlaybackReconTextureState = slot.gpuPlaybackReconTextureState;
        frame->dualIsoPreviewHistogramMs = slot.dualIsoPreviewHistogramMs;
        frame->dualIsoPreviewRegressionMs = slot.dualIsoPreviewRegressionMs;
        frame->dualIsoPreviewRowscaleMs = slot.dualIsoPreviewRowscaleMs;
        frame->frameReadyEmitStageTime = slot.frameReadyEmitStageTime;
        frame->processedFrame8Active = slot.processedFrame8Active;
        frame->processedFrame8Signature = slot.processedFrame8Signature;
        frame->processedFrame16Active = slot.processedFrame16Active;
        frame->processedFrame16Signature = slot.processedFrame16Signature;
        frame->dualIsoPattern = slot.dualIsoPattern;
        frame->dualIsoAutoCorrection = slot.dualIsoAutoCorrection;
        frame->dualIsoEvCorrection = slot.dualIsoEvCorrection;
        frame->dualIsoBlackDelta = slot.dualIsoBlackDelta;
        frame->presentationContext = slot.presentationContext;
        frame->stageTimingTelemetry = slot.stageTimingTelemetry;
    }
    return true;
}

void RenderFrameThread::releasePresentedFrame()
{
    QMutexLocker locker(&m_mutex);
    if( m_presentingSlotIndex >= 0 )
    {
        releaseSlotLocked( m_presentingSlotIndex );
        m_presentingSlotIndex = -1;
        m_waitCondition.wakeAll();
    }
}

void RenderFrameThread::releasePresentedFrameForRequestSerial( uint64_t requestSerial )
{
    QMutexLocker locker(&m_mutex);
    for( int i = 0; i < static_cast<int>( m_frameSlots.size() ); ++i )
    {
        FrameSlot &slot = m_frameSlots[i];
        if( !slot.presenting ) continue;
        if( slot.requestSerial != requestSerial ) continue;
        releaseSlotLocked( i );
        if( m_presentingSlotIndex == i ) m_presentingSlotIndex = -1;
        m_waitCondition.wakeAll();
        break;
    }
}

bool RenderFrameThread::lastFrameUsedGpuBilinearDebayer() const
{
    return m_lastFrameUsedGpuBilinearDebayer;
}

QString RenderFrameThread::lastGpuBilinearFallbackReason() const
{
    return m_lastGpuBilinearFallbackReason;
}

QString RenderFrameThread::lastGpuBilinearRendererDescription() const
{
    return m_lastGpuBilinearRendererDescription;
}

bool RenderFrameThread::lastFrameUsedGpuAmazeDebayer() const
{
    return m_lastFrameUsedGpuAmazeDebayer;
}

QString RenderFrameThread::lastGpuAmazeFallbackReason() const
{
    return m_lastGpuAmazeFallbackReason;
}

QString RenderFrameThread::lastGpuAmazeRendererDescription() const
{
    return m_lastGpuAmazeRendererDescription;
}

double RenderFrameThread::lastDualIsoPreviewHistogramMilliseconds() const
{
    return m_lastDualIsoPreviewHistogramMs;
}

double RenderFrameThread::lastDualIsoPreviewRegressionMilliseconds() const
{
    return m_lastDualIsoPreviewRegressionMs;
}

double RenderFrameThread::lastDualIsoPreviewRowscaleMilliseconds() const
{
    return m_lastDualIsoPreviewRowscaleMs;
}

QJsonObject RenderFrameThread::lastStageTimingTelemetry() const
{
    return m_lastStageTimingTelemetry;
}

double RenderFrameThread::lastFrameReadyEmitStageTime() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastFrameReadyEmitStageTime;
}

void RenderFrameThread::setPhase3Mode( Phase3Mode mode ) noexcept
{
    m_phase3Mode.store( mode, std::memory_order_release );
}

Phase3Mode RenderFrameThread::phase3Mode( void ) const noexcept
{
    return m_phase3Mode.load( std::memory_order_acquire );
}

//Stop the thread
void RenderFrameThread::stop()
{
    QMutexLocker locker(&m_mutex);
    m_stop = true;
    stopDecodeWorkerLocked();
    stopReconWorkerLocked();
    m_waitCondition.wakeAll();
}

void RenderFrameThread::lock()
{
    m_mutex.lock();
    while( m_renderFrame || m_renderingFrame || phase3WorkInFlightLocked() )
    {
        m_waitCondition.wait(&m_mutex);
    }
}

void RenderFrameThread::unlock()
{
    m_mutex.unlock();
}

//Main loop of the thread
void RenderFrameThread::run(void)
{
    /* Round-2 item 4: the stage CSV sink used to open only inside the
     * Phase-3-mode telemetry emitter, so MLVAPP_PHASE3_TEL_PATH was inert
     * during normal Sharp/Aggressive playback even though the per-stage
     * enter/leave writes are already wired for it. Opening here is
     * trace-only and default-off: the helper no-ops unless the env is set. */
    (void)phase3StageCsvSinkEnsureOpen();
    runPhase3();
}

bool RenderFrameThread::phase3DecodeAheadActive( Phase3Mode mode ) const
{
    return ( mode == Phase3Mode::DecodeAheadOnly
          || mode == Phase3Mode::DecodeReconProcess )
        && !phase3LiveFallbackActive()
        && !phase3KillSwitchActive( mode );
}

void RenderFrameThread::ensureDecodeWorkerStartedLocked( void )
{
    if( m_decodeWorker ) return;
    m_decodeWorkerStop = false;
    m_decodeWorker = new DecodeWorker( this );
    m_decodeWorker->start();
}

void RenderFrameThread::stopDecodeWorkerLocked( void )
{
    m_decodeWorkerStop = true;
    m_decodeWaitCondition.wakeAll();
}

void RenderFrameThread::ensureReconWorkerStartedLocked( void )
{
    if( m_reconWorker ) return;
    m_reconWorkerStop = false;
    m_reconWorker = new ReconWorker( this );
    m_reconWorker->start();
}

void RenderFrameThread::stopReconWorkerLocked( void )
{
    m_reconWorkerStop = true;
    m_reconWaitCondition.wakeAll();
}

bool RenderFrameThread::phase3WorkInFlightLocked( void ) const
{
    if( !m_decodeRequests.empty()
     || !m_reconRequests.empty()
     || !m_decodeReadySlots.empty()
     || !m_processReadySlots.empty() ) return true;
    for( const FrameSlot &slot : m_frameSlots )
    {
        const SlotState state = slot.state.load( std::memory_order_acquire );
        if( state == SlotState::Requested
         || state == SlotState::Decoding
         || state == SlotState::Decoded
         || state == SlotState::ReconReady
         || state == SlotState::Recon
         || state == SlotState::ProcessReady
         || state == SlotState::Processing )
        {
            return true;
        }
    }
    return false;
}

void RenderFrameThread::queueDecodeRequestLocked( int slotIndex, const RenderRequest &request )
{
    if( slotIndex < 0 || slotIndex >= static_cast<int>( m_frameSlots.size() ) ) return;
    const double decodeQueueStageTime = mlv_stage_timing_now();
    FrameSlot &slot = m_frameSlots[slotIndex];
    slot.resetMetadata();
    slot.queuedRequest = request;
    slot.frameNumber = request.frameNumber;
    slot.requestSerial = request.requestSerial;
    slot.outputMode = request.outputMode;
    slot.phase3Mode = request.phase3Mode;
    slot.presentationContext = request.presentationContext;
    if( playbackSmokeTimelineTelemetryEnabled() )
    {
        slot.stageTimingTelemetry.insert(
            QStringLiteral("phase3_decode_queue_stage_time"),
            decodeQueueStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("phase3_loop_wake_stage_time"),
            request.phase3LoopWakeStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("phase3_render_request_take_stage_time"),
            request.phase3RenderRequestTakeStageTime );
    }
    transitionSlotState( slotIndex,
                         SlotState::Idle,
                         SlotState::Requested,
                         request.phase3Mode,
                         request.frameNumber,
                         request.requestSerial,
                         "phase3-3b-decode-requested" );
    m_decodeRequests.push_back( { slotIndex, request } );
    m_decodeRequestsIssuedCount.fetch_add( 1, std::memory_order_relaxed );
    m_decodeWaitCondition.wakeOne();
}

bool RenderFrameThread::takeDecodeRequestForWorker( DecodeQueueEntry *entry )
{
    QMutexLocker locker( &m_mutex );
    while( !m_decodeWorkerStop && !m_stop && m_decodeRequests.empty() )
    {
        m_decodeWaitCondition.wait( &m_mutex );
    }
    if( m_decodeWorkerStop || m_stop ) return false;
    if( m_decodeRequests.empty() ) return false;
    if( entry ) *entry = m_decodeRequests.front();
    m_decodeRequests.pop_front();
    return true;
}

void RenderFrameThread::decodeFrameForWorker( const DecodeQueueEntry &entry )
{
    if( entry.slotIndex < 0 || entry.slotIndex >= static_cast<int>( m_frameSlots.size() ) )
    {
        return;
    }

    const double decodeStartStageTime = mlv_stage_timing_now();
    const bool stageCsvEnabled = stage_timing_csv_sink_enabled() != 0;
    const uint8_t telemetryMode = static_cast<uint8_t>( entry.request.phase3Mode );
    if( stageCsvEnabled )
    {
        stage_timing_csv_sink_write_event(
            entry.request.frameNumber,
            entry.request.requestSerial,
            static_cast<uint8_t>( entry.slotIndex ),
            MLV_STAGE_DECODE,
            "enter",
            mlv_stage_timing_now_ns(),
            telemetryMode,
            0 );
    }

    transitionSlotState( entry.slotIndex,
                         SlotState::Requested,
                         SlotState::Decoding,
                         entry.request.phase3Mode,
                         entry.request.frameNumber,
                         entry.request.requestSerial,
                         "phase3-3b-decoding" );

    FrameSlot &slot = m_frameSlots[entry.slotIndex];
    if( playbackSmokeTimelineTelemetryEnabled() )
    {
        slot.stageTimingTelemetry.insert(
            QStringLiteral("phase3_decode_start_stage_time"),
            decodeStartStageTime );
    }
    const size_t rawPixelCount =
        static_cast<size_t>( qMax( 0, m_imageWidth ) )
        * static_cast<size_t>( qMax( 0, m_imageHeight ) );
    if( slot.rawImage16.size() < rawPixelCount )
    {
        slot.rawImage16.resize( rawPixelCount );
    }
    if( rawPixelCount > 0 && m_pMlvObject )
    {
        (void)getMlvRawFrameUint16( m_pMlvObject,
                                    entry.request.frameNumber,
                                    slot.rawImage16.data() );
        if( entry.request.phase3Mode == Phase3Mode::DecodeReconProcess
         && entry.request.presentationContext.playbackActive
         && gpuPlaybackReconAsyncH2dRequested() )
        {
            (void)llrpGpuPlaybackReconPreuploadFrame(
                entry.request.frameNumber,
                slot.rawImage16.data(),
                rawPixelCount * sizeof(uint16_t) );
        }
    }
    const double decodeEndStageTime = mlv_stage_timing_now();
    if( playbackSmokeTimelineTelemetryEnabled() )
    {
        slot.stageTimingTelemetry.insert(
            QStringLiteral("phase3_decode_end_stage_time"),
            decodeEndStageTime );
    }

    if( stageCsvEnabled )
    {
        stage_timing_csv_sink_write_event(
            entry.request.frameNumber,
            entry.request.requestSerial,
            static_cast<uint8_t>( entry.slotIndex ),
            MLV_STAGE_DECODE,
            "leave",
            mlv_stage_timing_now_ns(),
            telemetryMode,
            0 );
    }

    transitionSlotState( entry.slotIndex,
                         SlotState::Decoding,
                         SlotState::Decoded,
                         entry.request.phase3Mode,
                         entry.request.frameNumber,
                         entry.request.requestSerial,
                         "phase3-3b-decoded" );
}

void RenderFrameThread::signalDecodeDoneFromWorker( int slotIndex )
{
    QMutexLocker locker( &m_mutex );
    if( !m_stop && slotIndex >= 0 )
    {
        if( playbackSmokeTimelineTelemetryEnabled() )
        {
            m_frameSlots[slotIndex].stageTimingTelemetry.insert(
                QStringLiteral("phase3_decode_done_signal_stage_time"),
                mlv_stage_timing_now() );
        }
        const RenderRequest request = m_frameSlots[slotIndex].queuedRequest;
        if( request.phase3Mode == Phase3Mode::DecodeReconProcess )
        {
            m_reconRequests.push_back( { slotIndex, request } );
            m_reconWaitCondition.wakeOne();
        }
        else
        {
            m_decodeReadySlots.push_back( slotIndex );
        }
    }
    m_waitCondition.wakeAll();
}

bool RenderFrameThread::takeReconRequestForWorker( ReconQueueEntry *entry )
{
    QMutexLocker locker( &m_mutex );
    while( !m_reconWorkerStop && !m_stop && m_reconRequests.empty() )
    {
        m_reconWaitCondition.wait( &m_mutex );
    }
    if( m_reconWorkerStop || m_stop ) return false;
    if( m_reconRequests.empty() ) return false;
    if( entry ) *entry = m_reconRequests.front();
    m_reconRequests.pop_front();
    return true;
}

void RenderFrameThread::reconFrameForWorker( const ReconQueueEntry &entry,
                                             llrawprocWorkerState_t *workerState )
{
    if( entry.slotIndex < 0 || entry.slotIndex >= static_cast<int>( m_frameSlots.size() ) )
    {
        return;
    }

    const double reconStartStageTime = mlv_stage_timing_now();
    const bool stageCsvEnabled = stage_timing_csv_sink_enabled() != 0;
    const uint8_t telemetryMode = static_cast<uint8_t>( entry.request.phase3Mode );
    if( stageCsvEnabled )
    {
        stage_timing_csv_sink_write_event(
            entry.request.frameNumber,
            entry.request.requestSerial,
            static_cast<uint8_t>( entry.slotIndex ),
            MLV_STAGE_RECON,
            "enter",
            mlv_stage_timing_now_ns(),
            telemetryMode,
            0 );
    }

    FrameSlot &slot = m_frameSlots[entry.slotIndex];
    const bool detailedTimelineTelemetry = playbackSmokeTimelineTelemetryEnabled();
    const auto stampReconStage = [&slot, detailedTimelineTelemetry]( const char * key )
    {
        if( detailedTimelineTelemetry )
        {
            slot.stageTimingTelemetry.insert( QString::fromLatin1( key ),
                                              mlv_stage_timing_now() );
        }
    };
    if( detailedTimelineTelemetry )
    {
        slot.stageTimingTelemetry.insert(
            QStringLiteral("phase3_recon_start_stage_time"),
            reconStartStageTime );
    }

    transitionSlotState( entry.slotIndex,
                         SlotState::Decoded,
                         SlotState::ReconReady,
                         entry.request.phase3Mode,
                         entry.request.frameNumber,
                         entry.request.requestSerial,
                         "phase3-3c-recon-ready" );
    transitionSlotState( entry.slotIndex,
                         SlotState::ReconReady,
                         SlotState::Recon,
                         entry.request.phase3Mode,
                         entry.request.frameNumber,
                         entry.request.requestSerial,
                         "phase3-3c-recon" );
    stampReconStage( "phase3_recon_after_state_transitions_stage_time" );

    const size_t rawPixelCount =
        static_cast<size_t>( qMax( 0, m_imageWidth ) )
        * static_cast<size_t>( qMax( 0, m_imageHeight ) );
    if( rawPixelCount > 0 && m_pMlvObject && slot.rawImage16.size() >= rawPixelCount )
    {
        const bool wantsGpuPlaybackReconTextureNoReadback =
            entry.request.presentationContext.gpuPlaybackReconTexturePresentRequested
            && entry.request.presentationContext.playbackActive
            && entry.request.presentationContext.playbackScaleFactor == 1
            && m_imageWidth > 0
            && m_imageHeight > 0;
        const bool allowGpuPlaybackReconTexturePrepareOnly =
            wantsGpuPlaybackReconTextureNoReadback
            && entry.request.presentationContext.gpuPlaybackReconAmazeTexturePresentAdmitted
            && !gpuPlaybackReconNoReadbackOutputValidationEnabled();
        bool gpuPlaybackReconTextureInputAvailable = false;
        bool gpuPlaybackReconTextureInputBorrowedFromRawImage16 = false;
        bool gpuPlaybackReconTextureRetainedDeviceAvailable = false;
        bool gpuPlaybackReconTexturePreparedStateValid = false;
        bool gpuPlaybackReconTextureStateSnapshotOk = false;
        QString gpuPlaybackReconTextureNoReadbackFallbackReason;
        if( !wantsGpuPlaybackReconTextureNoReadback )
        {
            slot.gpuPlaybackReconTextureInputBayerFrame.clear();
            slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16 = false;
            slot.gpuPlaybackReconTextureState = GpuPlaybackReconTextureState();
            slot.gpuPlaybackReconTextureNoReadbackCandidate = false;
            gpuPlaybackReconTextureNoReadbackFallbackReason =
                QStringLiteral("GPU playback recon no-readback request was not armed");
        }
        stampReconStage( "phase3_recon_before_scope_setup_stage_time" );
        const GpuPlaybackReconScope gpuPlaybackReconScope(
            entry.request.presentationContext.playbackActive );
        const GpuPlaybackReconTexturePresentScope gpuPlaybackReconTexturePresentScope(
            wantsGpuPlaybackReconTextureNoReadback );
        const GpuPlaybackReconTexturePrepareOnlyScope
            gpuPlaybackReconTexturePrepareOnlyScope(
                allowGpuPlaybackReconTexturePrepareOnly );
        const GpuPlaybackReconFrameIdScope gpuPlaybackReconFrameIdScope(
            entry.request.frameNumber );
        stampReconStage( "phase3_recon_after_scope_setup_stage_time" );
        stampReconStage( "phase3_recon_before_capture_set_frame_stage_time" );
        mlv_pipeline_capture_set_current_frame( entry.request.frameNumber );
        stampReconStage( "phase3_recon_after_capture_set_frame_stage_time" );
        stampReconStage( "phase3_recon_before_apply_llrawproc_stage_time" );
        applyLLRawProcObjectWorker( m_pMlvObject,
                                    slot.rawImage16.data(),
                                    rawPixelCount * sizeof(uint16_t),
                                    workerState,
                                    0 );
        stampReconStage( "phase3_recon_after_apply_llrawproc_stage_time" );
        insertLlrawprocReconTimingTelemetry( slot.stageTimingTelemetry );
        insertGpuPlaybackReconRunTelemetry( slot.stageTimingTelemetry );
        stampReconStage( "phase3_recon_after_timing_capture_stage_time" );
        bool gpuPlaybackReconTextureBayerSnapshotCopied = false;
        stampReconStage( "phase3_recon_before_no_readback_state_stage_time" );
        if( wantsGpuPlaybackReconTextureNoReadback )
        {
            const bool outputValidationRequired =
                gpuPlaybackReconNoReadbackOutputValidationEnabled();
            gpuPlaybackReconTextureBayerSnapshotCopied =
                !outputValidationRequired;
            stampReconStage( "phase3_recon_before_oracle_snapshot_stage_time" );
            if( outputValidationRequired )
            {
                /* Snapshot the recon-output Dual ISO bayer NOW, before the process
                 * stage (getMlvProcessedFrame16Scaled) overwrites slot.rawImage16
                 * with the fully-processed display frame. This recon bayer is the
                 * GL-vs-CPU parity oracle, so keep it only when output validation
                 * is explicitly requested; normal playback should not copy a
                 * proof-only full-resolution buffer every frame. */
                try
                {
                    slot.gpuPlaybackReconTextureBayerFrame.assign(
                        slot.rawImage16.begin(),
                        slot.rawImage16.begin() + static_cast<std::ptrdiff_t>( rawPixelCount ) );
                    gpuPlaybackReconTextureBayerSnapshotCopied =
                        slot.gpuPlaybackReconTextureBayerFrame.size() == rawPixelCount;
                }
                catch( const std::bad_alloc & )
                {
                    slot.gpuPlaybackReconTextureBayerFrame.clear();
                    gpuPlaybackReconTextureBayerSnapshotCopied = false;
                }
            }
            else
            {
                slot.gpuPlaybackReconTextureBayerFrame.clear();
            }
            stampReconStage( "phase3_recon_after_oracle_snapshot_stage_time" );
            stampReconStage( "phase3_recon_before_prepared_input_snapshot_stage_time" );
            const bool prepareOnlyUsed =
                llrpGpuPlaybackReconLastPrepareOnlyForTesting() != 0;
            const size_t preparedInputWords =
                llrpGpuPlaybackReconGetLastInputBayer16( nullptr, 0 );
            if( preparedInputWords == rawPixelCount )
            {
                try
                {
                    slot.gpuPlaybackReconTextureInputBayerFrame.assign(
                        rawPixelCount,
                        0u );
                    const size_t copiedWords =
                        llrpGpuPlaybackReconGetLastInputBayer16(
                            slot.gpuPlaybackReconTextureInputBayerFrame.data(),
                            slot.gpuPlaybackReconTextureInputBayerFrame.size() );
                    gpuPlaybackReconTextureInputAvailable =
                        copiedWords == rawPixelCount;
                    slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16 = false;
                }
                catch( const std::bad_alloc & )
                {
                    slot.gpuPlaybackReconTextureInputBayerFrame.clear();
                    slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16 = false;
                    slot.gpuPlaybackReconTextureState = GpuPlaybackReconTextureState();
                    slot.gpuPlaybackReconTextureNoReadbackCandidate = false;
                    gpuPlaybackReconTextureNoReadbackFallbackReason =
                        QStringLiteral("GPU playback recon prepared input snapshot allocation failed");
                }
            }
            else if( prepareOnlyUsed
                  && allowGpuPlaybackReconTexturePrepareOnly
                  && !outputValidationRequired
                  && slot.rawImage16.size() >= rawPixelCount )
            {
                slot.gpuPlaybackReconTextureInputBayerFrame.clear();
                slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16 = true;
                gpuPlaybackReconTextureInputBorrowedFromRawImage16 = true;
                gpuPlaybackReconTextureInputAvailable = true;
            }
            else
            {
                slot.gpuPlaybackReconTextureInputBayerFrame.clear();
                slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16 = false;
                gpuPlaybackReconTextureNoReadbackFallbackReason =
                    QStringLiteral("GPU playback recon prepared input snapshot was unavailable");
            }
            stampReconStage( "phase3_recon_after_prepared_input_snapshot_stage_time" );

            stampReconStage( "phase3_recon_before_retained_device_query_stage_time" );
            llrpGpuPlaybackRetainedDeviceBayer16_t retainedDevice;
            memset( &retainedDevice, 0, sizeof( retainedDevice ) );
            if( !outputValidationRequired
             && llrpGpuPlaybackReconGetLastRetainedDeviceBayer16( &retainedDevice ) != 0
             && retainedDevice.device_bayer16
             && retainedDevice.width == m_imageWidth
             && retainedDevice.height == m_imageHeight
             && retainedDevice.token != 0 )
            {
                slot.gpuPlaybackReconTextureRetainedDeviceBayer16 =
                    retainedDevice.device_bayer16;
                slot.gpuPlaybackReconTextureRetainedDeviceWidth =
                    retainedDevice.width;
                slot.gpuPlaybackReconTextureRetainedDeviceHeight =
                    retainedDevice.height;
                slot.gpuPlaybackReconTextureRetainedDeviceToken =
                    retainedDevice.token;
                gpuPlaybackReconTextureRetainedDeviceAvailable = true;
                if( gpuTexNrOverlapTraceEnabled() )
                {
                    qInfo().nospace()
                        << "gpu_tex_nr_overlap_trace event=render_retained"
                        << " frame=" << entry.request.frameNumber
                        << " serial=" << entry.request.requestSerial
                        << " slot=" << entry.slotIndex
                        << " token=" << static_cast<qulonglong>( retainedDevice.token )
                        << " wall_ms=" << QDateTime::currentMSecsSinceEpoch();
                }
            }
            else
            {
                slot.gpuPlaybackReconTextureRetainedDeviceBayer16 = nullptr;
                slot.gpuPlaybackReconTextureRetainedDeviceWidth = 0;
                slot.gpuPlaybackReconTextureRetainedDeviceHeight = 0;
                slot.gpuPlaybackReconTextureRetainedDeviceToken = 0;
            }
            stampReconStage( "phase3_recon_after_retained_device_query_stage_time" );

            stampReconStage( "phase3_recon_before_prepared_state_snapshot_stage_time" );
            llrpGpuPlaybackReconState_t preparedState;
            memset( &preparedState, 0, sizeof( preparedState ) );
            const bool preparedStateAvailable =
                llrpGpuPlaybackReconGetLastPreparedState( &preparedState ) != 0;
            gpuPlaybackReconTexturePreparedStateValid = preparedState.valid != 0;
            bool gpuPlaybackReconTextureLutCacheHit = false;
            int gpuPlaybackReconTextureLutCacheEntries = 0;
            const uint64_t gpuPlaybackReconTextureLutSnapshotAtomicGeneration =
                m_gpuPlaybackReconTextureLutSnapshotGeneration.load(
                    std::memory_order_acquire );
            const uint64_t gpuPlaybackReconTextureLutSnapshotRequestGeneration =
                entry.request.presentationContext.gpuPreviewProcessingConfigGeneration;
            uint64_t gpuPlaybackReconTextureLutSnapshotGeneration =
                gpuPlaybackReconTextureLutSnapshotAtomicGeneration;
            if( entry.request.presentationContext.gpuPreviewProcessingConfigGeneration
             > gpuPlaybackReconTextureLutSnapshotGeneration )
            {
                gpuPlaybackReconTextureLutSnapshotGeneration =
                    entry.request.presentationContext.gpuPreviewProcessingConfigGeneration;
            }
            gpuPlaybackReconTextureStateSnapshotOk =
                preparedStateAvailable
                && assignGpuPlaybackReconTextureState( slot.gpuPlaybackReconTextureState,
                                                       preparedState,
                                                       gpuPlaybackReconTextureLutSnapshotGeneration,
                                                       m_gpuPlaybackReconTextureLutCache,
                                                       m_gpuPlaybackReconTextureLutCacheUseCounter,
                                                       &gpuPlaybackReconTextureLutCacheHit,
                                                       &gpuPlaybackReconTextureLutCacheEntries );
            slot.stageTimingTelemetry.insert(
                QStringLiteral("phase3_recon_lut_snapshot_cache_hit"),
                gpuPlaybackReconTextureLutCacheHit );
            slot.stageTimingTelemetry.insert(
                QStringLiteral("phase3_recon_lut_snapshot_processing_generation"),
                static_cast<double>(
                    gpuPlaybackReconTextureLutSnapshotGeneration ) );
            slot.stageTimingTelemetry.insert(
                QStringLiteral("phase3_recon_lut_snapshot_atomic_generation"),
                static_cast<double>(
                    gpuPlaybackReconTextureLutSnapshotAtomicGeneration ) );
            slot.stageTimingTelemetry.insert(
                QStringLiteral("phase3_recon_lut_snapshot_request_generation"),
                static_cast<double>(
                    gpuPlaybackReconTextureLutSnapshotRequestGeneration ) );
            slot.stageTimingTelemetry.insert(
                QStringLiteral("phase3_recon_lut_snapshot_cache_entries"),
                gpuPlaybackReconTextureLutCacheEntries );
            stampReconStage( "phase3_recon_after_prepared_state_snapshot_stage_time" );
            stampReconStage( "phase3_recon_before_candidate_validation_stage_time" );
            if( gpuPlaybackReconTextureInputAvailable
             && gpuPlaybackReconTextureStateSnapshotOk
             && gpuPlaybackReconTextureBayerSnapshotCopied )
            {
                slot.gpuPlaybackReconTextureNoReadbackCandidate = true;
            }
            else
            {
                slot.gpuPlaybackReconTextureNoReadbackCandidate = false;
                slot.gpuPlaybackReconTextureInputBayerFrame.clear();
                slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16 = false;
                if( slot.gpuPlaybackReconTextureRetainedDeviceToken != 0 )
                {
                    llrpGpuPlaybackReconReleaseRetainedDeviceBayer16(
                        slot.gpuPlaybackReconTextureRetainedDeviceToken );
                }
                slot.gpuPlaybackReconTextureRetainedDeviceBayer16 = nullptr;
                slot.gpuPlaybackReconTextureRetainedDeviceWidth = 0;
                slot.gpuPlaybackReconTextureRetainedDeviceHeight = 0;
                slot.gpuPlaybackReconTextureRetainedDeviceToken = 0;
                slot.gpuPlaybackReconTextureBayerFrame.clear();
                slot.gpuPlaybackReconTextureState = GpuPlaybackReconTextureState();
                if( gpuPlaybackReconTextureNoReadbackFallbackReason.isEmpty() )
                {
                    gpuPlaybackReconTextureNoReadbackFallbackReason =
                        !preparedStateAvailable
                            ? QStringLiteral("GPU playback recon prepared state was unavailable")
                            : ( !gpuPlaybackReconTextureBayerSnapshotCopied
                                ? QStringLiteral("GPU playback recon output bayer snapshot was unavailable")
                                : QStringLiteral("GPU playback recon state snapshot was rejected") );
                }
            }
            stampReconStage( "phase3_recon_after_candidate_validation_stage_time" );
        }
        stampReconStage( "phase3_recon_after_no_readback_state_stage_time" );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_no_readback_recon_requested"),
            wantsGpuPlaybackReconTextureNoReadback );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_prepare_only_allowed"),
            allowGpuPlaybackReconTexturePrepareOnly );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_prepare_only_used"),
            llrpGpuPlaybackReconLastPrepareOnlyForTesting() != 0 );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_no_readback_input_words"),
            static_cast<qint64>(
                slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16
                    ? slot.rawImage16.size()
                    : slot.gpuPlaybackReconTextureInputBayerFrame.size() ) );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_no_readback_input_borrowed"),
            slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16 );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_retained_device_available"),
            gpuPlaybackReconTextureRetainedDeviceAvailable );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_retained_device_token"),
            static_cast<qint64>( slot.gpuPlaybackReconTextureRetainedDeviceToken ) );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_no_readback_oracle_required"),
            gpuPlaybackReconNoReadbackOutputValidationEnabled() );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_no_readback_oracle_words"),
            static_cast<qint64>( slot.gpuPlaybackReconTextureBayerFrame.size() ) );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_no_readback_prepared_state_valid"),
            gpuPlaybackReconTexturePreparedStateValid );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_no_readback_state_snapshot_ok"),
            gpuPlaybackReconTextureStateSnapshotOk );
        if( !gpuPlaybackReconTextureNoReadbackFallbackReason.isEmpty() )
        {
            slot.stageTimingTelemetry.insert(
                QStringLiteral("gpu_playback_recon_texture_present_no_readback_fallback_reason"),
                gpuPlaybackReconTextureNoReadbackFallbackReason );
        }
        stampReconStage( "phase3_recon_after_metadata_insert_stage_time" );
    }

    if( stageCsvEnabled )
    {
        stage_timing_csv_sink_write_event(
            entry.request.frameNumber,
            entry.request.requestSerial,
            static_cast<uint8_t>( entry.slotIndex ),
            MLV_STAGE_RECON,
            "leave",
            mlv_stage_timing_now_ns(),
            telemetryMode,
            0 );
    }

    stampReconStage( "phase3_recon_before_process_ready_transition_stage_time" );
    transitionSlotState( entry.slotIndex,
                         SlotState::Recon,
                         SlotState::ProcessReady,
                         entry.request.phase3Mode,
                         entry.request.frameNumber,
                         entry.request.requestSerial,
                         "phase3-3c-process-ready" );
    stampReconStage( "phase3_recon_after_process_ready_transition_stage_time" );
    if( detailedTimelineTelemetry )
    {
        slot.stageTimingTelemetry.insert(
            QStringLiteral("phase3_recon_end_stage_time"),
            mlv_stage_timing_now() );
    }
}

void RenderFrameThread::signalReconDoneFromWorker( int slotIndex )
{
    QMutexLocker locker( &m_mutex );
    if( !m_stop && slotIndex >= 0 )
    {
        if( playbackSmokeTimelineTelemetryEnabled() )
        {
            m_frameSlots[slotIndex].stageTimingTelemetry.insert(
                QStringLiteral("phase3_process_ready_signal_stage_time"),
                mlv_stage_timing_now() );
        }
        m_processReadySlots.push_back( slotIndex );
    }
    m_waitCondition.wakeAll();
}

int RenderFrameThread::waitForDecodedSlotLocked( void )
{
    while( !m_stop && m_decodeReadySlots.empty() )
    {
        m_waitCondition.wait( &m_mutex );
    }
    if( m_stop ) return -1;
    const int slotIndex = m_decodeReadySlots.front();
    m_decodeReadySlots.pop_front();
    if( slotIndex >= 0 && slotIndex < static_cast<int>( m_frameSlots.size() ) )
    {
        if( playbackSmokeTimelineTelemetryEnabled() )
        {
            m_frameSlots[slotIndex].stageTimingTelemetry.insert(
                QStringLiteral("phase3_decode_ready_take_stage_time"),
                mlv_stage_timing_now() );
        }
    }
    return slotIndex;
}

int RenderFrameThread::waitForProcessReadySlotLocked( void )
{
    while( !m_stop && m_processReadySlots.empty() )
    {
        m_waitCondition.wait( &m_mutex );
    }
    if( m_stop ) return -1;
    const int slotIndex = m_processReadySlots.front();
    m_processReadySlots.pop_front();
    if( slotIndex >= 0 && slotIndex < static_cast<int>( m_frameSlots.size() ) )
    {
        if( playbackSmokeTimelineTelemetryEnabled() )
        {
            m_frameSlots[slotIndex].stageTimingTelemetry.insert(
                QStringLiteral("phase3_process_ready_take_stage_time"),
                mlv_stage_timing_now() );
        }
    }
    return slotIndex;
}

void RenderFrameThread::setupActiveRequestLocked( const RenderRequest &request, int slotIndex )
{
    m_activeFrameNumber = request.frameNumber;
    m_activeOutputMode = request.outputMode;
    m_activeUseGpuBilinearDebayer = request.useGpuBilinearDebayer;
    m_activeUseGpuAmazeDebayer = request.useGpuAmazeDebayer;
    m_activeFrameRequestSerial = request.requestSerial;
    m_activeFrameRequestStageTime = request.requestStageTime;
    m_activePresentationContext = request.presentationContext;
    m_activePresentationPreparationOptions = request.presentationPreparationOptions;
    m_activeRenderRequest = request;
    m_activeQueuedPlaybackDropCount = request.queuedPlaybackDropCount;
    m_renderingFrame = true;
    m_renderingSlotIndex = slotIndex;
}

void RenderFrameThread::renderDecodedSlot( int slotIndex,
                                           const RenderRequest &request,
                                           Phase3Mode activePhase3Mode )
{
    RenderRequest instrumentedRequest = request;
    if( playbackSmokeTimelineTelemetryEnabled() )
    {
        instrumentedRequest.phase3RenderDecodedSlotEntryStageTime =
            mlv_stage_timing_now();
        m_activeRenderRequest = instrumentedRequest;
    }
    const bool overlapTrace = gpuTexNrOverlapTraceEnabled();
    if( overlapTrace )
    {
        qInfo().nospace()
            << "gpu_tex_nr_overlap_trace event=render_start"
            << " frame=" << instrumentedRequest.frameNumber
            << " serial=" << instrumentedRequest.requestSerial
            << " slot=" << slotIndex
            << " wall_ms=" << QDateTime::currentMSecsSinceEpoch();
    }

    const bool phase3Active = activePhase3Mode != Phase3Mode::Disabled;
    const bool stageCsvEnabled =
        !phase3Active && stage_timing_csv_sink_enabled() != 0;
    const uint8_t telemetryMode = static_cast<uint8_t>( activePhase3Mode );
    const uint64_t renderEnterNs =
        stageCsvEnabled ? mlv_stage_timing_now_ns() : 0;
    if( stageCsvEnabled )
    {
        stage_timing_csv_sink_write_event(
            instrumentedRequest.frameNumber,
            instrumentedRequest.requestSerial,
            static_cast<uint8_t>( slotIndex ),
            MLV_STAGE_DECODE,
            "enter",
            renderEnterNs,
            telemetryMode,
            0 );
    }

    const SlotState slotState = m_frameSlots[slotIndex].state.load( std::memory_order_acquire );
    const bool consumeDecodedRaw =
        activePhase3Mode == Phase3Mode::DecodeAheadOnly
     && slotState == SlotState::Decoded
     && !m_frameSlots[slotIndex].rawImage16.empty();
    const bool consumeReconnedRaw =
        activePhase3Mode == Phase3Mode::DecodeReconProcess
     && slotState == SlotState::ProcessReady
     && !m_frameSlots[slotIndex].rawImage16.empty();
    if( playbackSmokeTimelineTelemetryEnabled() )
    {
        instrumentedRequest.phase3BeforeDrawFrameStageTime =
            mlv_stage_timing_now();
        m_activeRenderRequest = instrumentedRequest;
    }
    drawFrame( slotIndex,
               ( consumeDecodedRaw || consumeReconnedRaw )
                   ? m_frameSlots[slotIndex].rawImage16.data()
                   : nullptr,
               consumeReconnedRaw );
    if( phase3Active )
    {
        m_frameSlots[slotIndex].phase3Mode = activePhase3Mode;
    }
    if( frame_checksum_enabled()
     && !m_frameSlots[slotIndex].rawImage8.empty() )
    {
        const uint64_t checksum =
            frame_checksum_compute( m_frameSlots[slotIndex].rawImage8.data(),
                                    m_frameSlots[slotIndex].rawImage8.size() );
        frame_checksum_log_record( static_cast<uint32_t>( request.frameNumber ),
                                   checksum );
    }
    if( phase3Active )
    {
        emitPhase3StageTelemetry( instrumentedRequest,
                                  m_frameSlots[slotIndex],
                                  slotIndex,
                                  activePhase3Mode );
    }
    else if( stageCsvEnabled )
    {
        const uint64_t renderLeaveNs = mlv_stage_timing_now_ns();
        stage_timing_csv_sink_write_event(
            request.frameNumber,
            request.requestSerial,
            static_cast<uint8_t>( slotIndex ),
            MLV_STAGE_DECODE,
            "leave",
            renderLeaveNs,
            telemetryMode,
            0 );
        stage_timing_csv_sink_write_event(
            request.frameNumber,
            request.requestSerial,
            static_cast<uint8_t>( slotIndex ),
            MLV_STAGE_RECON,
            "enter",
            renderEnterNs,
            telemetryMode,
            0 );
        stage_timing_csv_sink_write_event(
            request.frameNumber,
            request.requestSerial,
            static_cast<uint8_t>( slotIndex ),
            MLV_STAGE_RECON,
            "leave",
            renderLeaveNs,
            telemetryMode,
            0 );
        stage_timing_csv_sink_write_event(
            request.frameNumber,
            request.requestSerial,
            static_cast<uint8_t>( slotIndex ),
            MLV_STAGE_PROCESS,
            "enter",
            renderEnterNs,
            telemetryMode,
            0 );
        stage_timing_csv_sink_write_event(
            request.frameNumber,
            request.requestSerial,
            static_cast<uint8_t>( slotIndex ),
            MLV_STAGE_PROCESS,
            "leave",
            renderLeaveNs,
            telemetryMode,
            0 );
        stage_timing_csv_sink_write_event(
            request.frameNumber,
            request.requestSerial,
            static_cast<uint8_t>( slotIndex ),
            MLV_STAGE_DISPLAY,
            "enter",
            renderEnterNs,
            telemetryMode,
            0 );
        stage_timing_csv_sink_write_event(
            request.frameNumber,
            request.requestSerial,
            static_cast<uint8_t>( slotIndex ),
            MLV_STAGE_DISPLAY,
            "leave",
            renderLeaveNs,
            telemetryMode,
            0 );
    }
}

void RenderFrameThread::publishRenderedSlot( int slotIndex,
                                             const RenderRequest &request,
                                             Phase3Mode activePhase3Mode )
{
    Q_UNUSED( request );
    if( slotIndex < 0 || slotIndex >= static_cast<int>( m_frameSlots.size() ) )
    {
        return;
    }
    if( activePhase3Mode != Phase3Mode::Disabled )
    {
        const SlotState processingFrom =
            activePhase3Mode == Phase3Mode::DecodeReconProcess
                ? SlotState::ProcessReady
                : SlotState::Decoded;
        transitionSlotState( slotIndex,
                             processingFrom,
                             SlotState::Processing,
                             activePhase3Mode,
                             m_frameSlots[slotIndex].frameNumber,
                             m_frameSlots[slotIndex].requestSerial,
                             activePhase3Mode == Phase3Mode::DecodeReconProcess
                                ? "phase3-3c-processing"
                                : "phase3-3b-processing" );
        transitionSlotState( slotIndex,
                             SlotState::Processing,
                             SlotState::Ready,
                             activePhase3Mode,
                             m_frameSlots[slotIndex].frameNumber,
                             m_frameSlots[slotIndex].requestSerial,
                             activePhase3Mode == Phase3Mode::DecodeReconProcess
                                ? "phase3-3c-ready"
                                : "phase3-3b-ready" );
    }
    m_renderingFrame = false;
    m_renderingSlotIndex = -1;
    m_frameSlots[slotIndex].ready = true;
    m_frameReady = true;
    m_waitCondition.wakeAll();
}

void RenderFrameThread::runPhase3( void )
{
    m_mutex.lock();
    ensureDecodeWorkerStartedLocked();
    ensureReconWorkerStartedLocked();
    while( !m_stop )
    {
        while( !m_stop
            && m_decodeReadySlots.empty()
            && m_processReadySlots.empty()
            && ( m_renderRequests.empty() || findFreeSlotLocked() < 0 ) )
        {
            m_waitCondition.wait( &m_mutex );
        }
        if( m_stop ) break;
        const double phase3LoopWakeStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;

        bool queuedPhase3Request = false;
        if( !m_renderRequests.empty()
         && findFreeSlotLocked() >= 0
         && phase3DecodeAheadActive( m_renderRequests.front().phase3Mode ) )
        {
            const int slotIndex = findFreeSlotLocked();
            RenderRequest request = m_renderRequests.front();
            m_renderRequests.pop_front();
            request.phase3LoopWakeStageTime = phase3LoopWakeStageTime;
            request.phase3RenderRequestTakeStageTime =
                playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
            m_renderFrame = !m_renderRequests.empty();
            queueDecodeRequestLocked( slotIndex, request );
            queuedPhase3Request = true;
        }

        int slotIndex = -1;
        RenderRequest request;
        Phase3Mode activePhase3Mode = Phase3Mode::Disabled;
        if( !m_processReadySlots.empty() )
        {
            slotIndex = waitForProcessReadySlotLocked();
            if( slotIndex < 0 ) break;
            request = m_frameSlots[slotIndex].queuedRequest;
            activePhase3Mode = request.phase3Mode;
        }
        else if( !m_decodeReadySlots.empty() )
        {
            slotIndex = waitForDecodedSlotLocked();
            if( slotIndex < 0 ) break;
            request = m_frameSlots[slotIndex].queuedRequest;
            activePhase3Mode = request.phase3Mode;
        }
        else if( queuedPhase3Request )
        {
            continue;
        }
        else if( !m_renderRequests.empty() && findFreeSlotLocked() >= 0 )
        {
            slotIndex = findFreeSlotLocked();
            request = m_renderRequests.front();
            m_renderRequests.pop_front();
            request.phase3LoopWakeStageTime = phase3LoopWakeStageTime;
            request.phase3RenderRequestTakeStageTime =
                playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
            request.phase3AfterTakeStampStageTime =
                playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
            m_renderFrame = !m_renderRequests.empty();
            request.phase3AfterRenderFrameFlagStageTime =
                playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;

            const Phase3Mode requestedPhase3Mode = request.phase3Mode;
            request.phase3ModeBranchEntryStageTime =
                playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
            if( requestedPhase3Mode != Phase3Mode::Disabled )
            {
                request.phase3BeforeLiveFallbackCheckStageTime =
                    playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
                const bool liveFallbackActive = phase3LiveFallbackActive();
                request.phase3AfterLiveFallbackCheckStageTime =
                    playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
                if( liveFallbackActive )
                {
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    playbackQualityAutoFallbackEpochWriteToSettings(
                        PlaybackQualityMode::Phase3Fast, nowMs );
                    playbackQualityAutoFallbackEpochWriteToSettings(
                        PlaybackQualityMode::Phase3HQ, nowMs );
                    Phase3Breadcrumbs::push( static_cast<uint8_t>( slotIndex ),
                                             0,
                                             0,
                                             mlv_stage_timing_now_ns(),
                                             request.frameNumber,
                                             request.requestSerial,
                                              static_cast<uint8_t>( requestedPhase3Mode ),
                                              "phase3-live-fallback" );
                }
                else
                {
                    request.phase3BeforeKillSwitchCheckStageTime =
                        playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
                    const bool killSwitchActive =
                        phase3KillSwitchActive( requestedPhase3Mode );
                    request.phase3AfterKillSwitchCheckStageTime =
                        playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
                    if( !killSwitchActive )
                    {
                        activePhase3Mode = requestedPhase3Mode;
                        request.phase3BeforeFirstTransitionStageTime =
                            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
                        transitionSlotState( slotIndex,
                                             SlotState::Idle,
                                             SlotState::Requested,
                                             activePhase3Mode,
                                             request.frameNumber,
                                             request.requestSerial,
                                             "phase3-requested" );
                        request.phase3AfterFirstTransitionStageTime =
                            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
                        request.phase3BeforeSecondTransitionStageTime =
                            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
                        transitionSlotState( slotIndex,
                                             SlotState::Requested,
                                             SlotState::Decoding,
                                             activePhase3Mode,
                                             request.frameNumber,
                                             request.requestSerial,
                                             "phase3-decoding" );
                        request.phase3AfterSecondTransitionStageTime =
                            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
                    }
                }
            }
            request.phase3ModeBranchExitStageTime =
                playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        }
        else
        {
            continue;
        }

        request.phase3PolicyDoneStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        setupActiveRequestLocked( request, slotIndex );
        request.phase3ActiveAssignDoneStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        m_activeRenderRequest = request;

        request.phase3BeforeDecodeAheadStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        m_activeRenderRequest = request;
        if( !m_renderRequests.empty()
         && findFreeSlotLocked() >= 0
         && phase3DecodeAheadActive( m_renderRequests.front().phase3Mode ) )
        {
            const int nextSlotIndex = findFreeSlotLocked();
            RenderRequest nextRequest = m_renderRequests.front();
            m_renderRequests.pop_front();
            nextRequest.phase3LoopWakeStageTime =
                playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
            nextRequest.phase3RenderRequestTakeStageTime =
                playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
            m_renderFrame = !m_renderRequests.empty();
            queueDecodeRequestLocked( nextSlotIndex, nextRequest );
        }
        request.phase3AfterDecodeAheadStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        request.phase3BeforeUnlockStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        m_activeRenderRequest = request;

        m_mutex.unlock();
        request.phase3AfterUnlockStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        m_activeRenderRequest = request;
        renderDecodedSlot( slotIndex, request, activePhase3Mode );
        m_mutex.lock();

        if( activePhase3Mode != Phase3Mode::Disabled
         && m_frameSlots[slotIndex].state.load( std::memory_order_acquire ) == SlotState::Decoding )
        {
            transitionSlotState( slotIndex,
                                 SlotState::Decoding,
                                 SlotState::Decoded,
                                 activePhase3Mode,
                                 request.frameNumber,
                                 request.requestSerial,
                                 "phase3-decoded" );
        }
        publishRenderedSlot( slotIndex, request, activePhase3Mode );
        m_mutex.unlock();
        emit frameReady();
        m_mutex.lock();
    }
    stopDecodeWorkerLocked();
    stopReconWorkerLocked();
    m_stop = false;
    m_mutex.unlock();
    if( m_decodeWorker )
    {
        m_decodeWorker->wait();
        delete m_decodeWorker;
        m_decodeWorker = nullptr;
    }
    if( m_reconWorker )
    {
        m_reconWorker->wait();
        delete m_reconWorker;
        m_reconWorker = nullptr;
    }
}

void RenderFrameThread::transitionSlotState( int slotIndex,
                                             SlotState from,
                                             SlotState to,
                                             Phase3Mode mode,
                                             uint32_t frameNumber,
                                             uint64_t requestSerial,
                                             const char * context )
{
    if( slotIndex < 0 || slotIndex >= static_cast<int>( m_frameSlots.size() ) ) return;

    SlotState expected = from;
    const bool changed =
        m_frameSlots[slotIndex].state.compare_exchange_strong(
            expected,
            to,
            std::memory_order_acq_rel,
            std::memory_order_acquire );
    Q_ASSERT_X( changed,
                "RenderFrameThread::transitionSlotState",
                "unexpected Phase 3 slot state transition" );
    if( !changed )
    {
        return;
    }

    Phase3Breadcrumbs::push(
        static_cast<uint8_t>( slotIndex ),
        static_cast<uint8_t>( from ),
        static_cast<uint8_t>( to ),
        mlv_stage_timing_now_ns(),
        frameNumber,
        requestSerial,
        static_cast<uint8_t>( mode ),
        context );
}

void RenderFrameThread::emitPhase3StageTelemetry( const RenderRequest &request,
                                                  const FrameSlot &slot,
                                                  int slotIndex,
                                                  Phase3Mode mode ) const
{
    if( !phase3StageCsvSinkEnsureOpen() ) return;

    const QJsonObject &telemetry = slot.stageTimingTelemetry;
    const auto fieldMs = [&telemetry]( const char * key ) -> double {
        return telemetry.value( QLatin1String( key ) ).toDouble();
    };
    const auto durationNs = []( double ms ) -> uint64_t {
        if( !std::isfinite( ms ) || ms <= 0.0 ) return 0;
        return static_cast<uint64_t>( ms * 1000000.0 );
    };
    double reconMs = fieldMs( "llrawproc_total_ms" );
    if( reconMs <= 0.0 ) reconMs = fieldMs( "llrawproc_ms" );

    double processMs = fieldMs( "processed8_total_ms" );
    if( processMs <= 0.0 ) processMs = fieldMs( "processed16_total_ms" );
    if( processMs <= 0.0 ) processMs = fieldMs( "processing_ms" );

    uint64_t ns = mlv_stage_timing_now_ns();
    const uint8_t slotId = static_cast<uint8_t>( slotIndex );
    const uint8_t phase3 = static_cast<uint8_t>( mode );
    const auto writeEvent = [&]( const char * stage, const char * event, uint64_t eventNs ) {
        stage_timing_csv_sink_write_event( request.frameNumber,
                                           request.requestSerial,
                                           slotId,
                                           stage,
                                           event,
                                           eventNs,
                                           phase3,
                                           0 );
    };
    const auto emitStage = [&]( const char * stage, uint64_t duration ) {
        writeEvent( stage, "enter", ns );
        ns += duration;
        writeEvent( stage, "leave", ns );
    };

    const double decodeMs = fieldMs( "raw_uint16_ms" );
    if( !( ( mode == Phase3Mode::DecodeAheadOnly
          || mode == Phase3Mode::DecodeReconProcess )
        && decodeMs <= 0.0 ) )
    {
        emitStage( MLV_STAGE_DECODE, durationNs( decodeMs ) );
    }
    if( !( mode == Phase3Mode::DecodeReconProcess && reconMs <= 0.0 ) )
    {
        emitStage( MLV_STAGE_RECON, durationNs( reconMs ) );
    }
    emitStage( MLV_STAGE_PROCESS, durationNs( processMs ) );
    emitStage( MLV_STAGE_DISPLAY,
               durationNs( fieldMs( "render_thread_playback_scale_ms" ) ) );
}

void RenderFrameThread::runSerial(void)
{
    m_mutex.lock();
    while( !m_stop )
    {
        while( !m_stop
            && ( m_renderRequests.empty() || findFreeSlotLocked() < 0 ) )
        {
            m_waitCondition.wait(&m_mutex);
        }
        if( m_stop ) break;

        const int slotIndex = findFreeSlotLocked();
        if( slotIndex < 0 )
        {
            continue;
        }

        const RenderRequest request = m_renderRequests.front();
        const double requestTakeStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        m_renderRequests.pop_front();
        RenderRequest instrumentedRequest = request;
        instrumentedRequest.phase3RenderRequestTakeStageTime =
            requestTakeStageTime;
        m_renderFrame = !m_renderRequests.empty();
        m_activeFrameNumber = instrumentedRequest.frameNumber;
        m_activeOutputMode = instrumentedRequest.outputMode;
        m_activeUseGpuBilinearDebayer = instrumentedRequest.useGpuBilinearDebayer;
        m_activeUseGpuAmazeDebayer = instrumentedRequest.useGpuAmazeDebayer;
        m_activeFrameRequestSerial = instrumentedRequest.requestSerial;
        m_activeFrameRequestStageTime = instrumentedRequest.requestStageTime;
        m_activePresentationContext = instrumentedRequest.presentationContext;
        m_activePresentationPreparationOptions = instrumentedRequest.presentationPreparationOptions;
        m_activeQueuedPlaybackDropCount = instrumentedRequest.queuedPlaybackDropCount;
        m_activeRenderRequest = instrumentedRequest;
        m_renderingFrame = true;
        m_renderingSlotIndex = slotIndex;
        instrumentedRequest.runSerialActiveAssignDoneStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;

        const Phase3Mode requestedPhase3Mode = instrumentedRequest.phase3Mode;
        Phase3Mode activePhase3Mode = Phase3Mode::Disabled;
        if( requestedPhase3Mode != Phase3Mode::Disabled )
        {
            if( phase3LiveFallbackActive() )
            {
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                playbackQualityAutoFallbackEpochWriteToSettings(
                    PlaybackQualityMode::Phase3Fast, nowMs );
                playbackQualityAutoFallbackEpochWriteToSettings(
                    PlaybackQualityMode::Phase3HQ, nowMs );
                Phase3Breadcrumbs::push( static_cast<uint8_t>( slotIndex ),
                                         0,
                                         0,
                                         mlv_stage_timing_now_ns(),
                                         instrumentedRequest.frameNumber,
                                         instrumentedRequest.requestSerial,
                                         static_cast<uint8_t>( requestedPhase3Mode ),
                                         "phase3-live-fallback" );
            }
            else if( !phase3KillSwitchActive( requestedPhase3Mode ) )
            {
                activePhase3Mode = requestedPhase3Mode;
                transitionSlotState( slotIndex,
                                     SlotState::Idle,
                                     SlotState::Requested,
                                     activePhase3Mode,
                                     instrumentedRequest.frameNumber,
                                     instrumentedRequest.requestSerial,
                                     "phase3-requested" );
                transitionSlotState( slotIndex,
                                     SlotState::Requested,
                                     SlotState::Decoding,
                                     activePhase3Mode,
                                     instrumentedRequest.frameNumber,
                                     instrumentedRequest.requestSerial,
                                     "phase3-decoding" );
            }
        }
        instrumentedRequest.runSerialPhasePolicyDoneStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        instrumentedRequest.runSerialBeforeUnlockStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        m_activeRenderRequest = instrumentedRequest;
        m_mutex.unlock();
        instrumentedRequest.runSerialAfterUnlockStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        const bool phase3Active = activePhase3Mode != Phase3Mode::Disabled;
        const bool stageCsvEnabled =
            !phase3Active && stage_timing_csv_sink_enabled() != 0;
        const uint8_t telemetryMode = static_cast<uint8_t>( activePhase3Mode );
        const uint64_t renderEnterNs =
            stageCsvEnabled ? mlv_stage_timing_now_ns() : 0;
        if( stageCsvEnabled )
        {
            stage_timing_csv_sink_write_event(
                request.frameNumber,
                request.requestSerial,
                static_cast<uint8_t>( slotIndex ),
                MLV_STAGE_DECODE,
                "enter",
                renderEnterNs,
                telemetryMode,
                0 );
        }
        instrumentedRequest.runSerialBeforeDrawFrameStageTime =
            playbackSmokeTimelineTelemetryEnabled() ? mlv_stage_timing_now() : 0.0;
        m_activeRenderRequest = instrumentedRequest;
        drawFrame( slotIndex );
        if( phase3Active )
        {
            m_frameSlots[slotIndex].phase3Mode = activePhase3Mode;
        }
        if( frame_checksum_enabled()
         && !m_frameSlots[slotIndex].rawImage8.empty() )
        {
            const uint64_t checksum =
                frame_checksum_compute( m_frameSlots[slotIndex].rawImage8.data(),
                                        m_frameSlots[slotIndex].rawImage8.size() );
            frame_checksum_log_record( static_cast<uint32_t>( request.frameNumber ),
                                       checksum );
        }
        if( phase3Active )
        {
            emitPhase3StageTelemetry( instrumentedRequest,
                                      m_frameSlots[slotIndex],
                                      slotIndex,
                                      activePhase3Mode );
        }
        else if( stageCsvEnabled )
        {
            const uint64_t renderLeaveNs = mlv_stage_timing_now_ns();
            stage_timing_csv_sink_write_event(
                instrumentedRequest.frameNumber,
                instrumentedRequest.requestSerial,
                static_cast<uint8_t>( slotIndex ),
                MLV_STAGE_DECODE,
                "leave",
                renderLeaveNs,
                telemetryMode,
                0 );
            stage_timing_csv_sink_write_event(
                instrumentedRequest.frameNumber,
                instrumentedRequest.requestSerial,
                static_cast<uint8_t>( slotIndex ),
                MLV_STAGE_RECON,
                "enter",
                renderEnterNs,
                telemetryMode,
                0 );
            stage_timing_csv_sink_write_event(
                instrumentedRequest.frameNumber,
                instrumentedRequest.requestSerial,
                static_cast<uint8_t>( slotIndex ),
                MLV_STAGE_RECON,
                "leave",
                renderLeaveNs,
                telemetryMode,
                0 );
            stage_timing_csv_sink_write_event(
                instrumentedRequest.frameNumber,
                instrumentedRequest.requestSerial,
                static_cast<uint8_t>( slotIndex ),
                MLV_STAGE_PROCESS,
                "enter",
                renderEnterNs,
                telemetryMode,
                0 );
            stage_timing_csv_sink_write_event(
                request.frameNumber,
                request.requestSerial,
                static_cast<uint8_t>( slotIndex ),
                MLV_STAGE_PROCESS,
                "leave",
                renderLeaveNs,
                telemetryMode,
                0 );
            stage_timing_csv_sink_write_event(
                request.frameNumber,
                request.requestSerial,
                static_cast<uint8_t>( slotIndex ),
                MLV_STAGE_DISPLAY,
                "enter",
                renderEnterNs,
                telemetryMode,
                0 );
            stage_timing_csv_sink_write_event(
                request.frameNumber,
                request.requestSerial,
                static_cast<uint8_t>( slotIndex ),
                MLV_STAGE_DISPLAY,
                "leave",
                renderLeaveNs,
                telemetryMode,
                0 );
        }
        m_mutex.lock();
        if( phase3Active )
        {
            /* Phase 3A keeps execution serial: drawFrame() is still the
             * monolithic decode/recon/process call. These lifecycle markers
             * validate slot ownership and breadcrumb wiring; later sub-phases
             * move the stage transitions to the real worker boundaries. */
            transitionSlotState( slotIndex,
                                 SlotState::Decoding,
                                 SlotState::Decoded,
                                 activePhase3Mode,
                                 request.frameNumber,
                                 request.requestSerial,
                                 "phase3-decoded" );
            transitionSlotState( slotIndex,
                                 SlotState::Decoded,
                                 SlotState::Processing,
                                 activePhase3Mode,
                                 request.frameNumber,
                                 request.requestSerial,
                                 "phase3-processing" );
            transitionSlotState( slotIndex,
                                 SlotState::Processing,
                                 SlotState::Ready,
                                 activePhase3Mode,
                                 request.frameNumber,
                                 request.requestSerial,
                                 "phase3-ready" );
        }
        m_renderingFrame = false;
        m_renderingSlotIndex = -1;
        m_frameSlots[slotIndex].ready = true;
        m_frameReady = true;
        m_waitCondition.wakeAll();
        m_mutex.unlock();
        emit frameReady();
        m_mutex.lock();
    }
    m_stop = false;
    m_mutex.unlock();
}

int RenderFrameThread::findLatestReadySlotLocked() const
{
    int readySlotIndex = -1;
    uint64_t latestRequestSerial = 0;
    for( int i = 0; i < static_cast<int>(m_frameSlots.size()); ++i )
    {
        const FrameSlot &slot = m_frameSlots[i];
        if( !slot.ready ) continue;
        if( readySlotIndex < 0 || slot.requestSerial >= latestRequestSerial )
        {
            readySlotIndex = i;
            latestRequestSerial = slot.requestSerial;
        }
    }
    return readySlotIndex;
}

int RenderFrameThread::findOldestReadySlotLocked() const
{
    int readySlotIndex = -1;
    uint64_t oldestRequestSerial = 0;
    for( int i = 0; i < static_cast<int>(m_frameSlots.size()); ++i )
    {
        const FrameSlot &slot = m_frameSlots[i];
        if( !slot.ready ) continue;
        if( readySlotIndex < 0 || slot.requestSerial < oldestRequestSerial )
        {
            readySlotIndex = i;
            oldestRequestSerial = slot.requestSerial;
        }
    }
    return readySlotIndex;
}

int RenderFrameThread::findFreeSlotLocked() const
{
    for( int i = 0; i < static_cast<int>(m_frameSlots.size()); ++i )
    {
        const FrameSlot &slot = m_frameSlots[i];
        if( i == m_renderingSlotIndex ) continue;
        if( slot.ready || slot.presenting ) continue;
        if( slot.state.load( std::memory_order_acquire ) != SlotState::Idle ) continue;
        return i;
    }
    return -1;
}

void RenderFrameThread::releaseSlotLocked( int slotIndex )
{
    if( slotIndex < 0 || slotIndex >= static_cast<int>(m_frameSlots.size()) ) return;
    FrameSlot &slot = m_frameSlots[slotIndex];
    if( slot.gpuPlaybackReconTextureRetainedDeviceToken != 0 )
    {
        llrpGpuPlaybackReconReleaseRetainedDeviceBayer16(
            slot.gpuPlaybackReconTextureRetainedDeviceToken );
        slot.gpuPlaybackReconTextureRetainedDeviceBayer16 = nullptr;
        slot.gpuPlaybackReconTextureRetainedDeviceWidth = 0;
        slot.gpuPlaybackReconTextureRetainedDeviceHeight = 0;
        slot.gpuPlaybackReconTextureRetainedDeviceToken = 0;
    }
    const SlotState state = slot.state.load( std::memory_order_acquire );
    if( state == SlotState::Presenting )
    {
        transitionSlotState( slotIndex,
                             SlotState::Presenting,
                             SlotState::Idle,
                             slot.phase3Mode,
                             slot.frameNumber,
                             slot.requestSerial,
                             "phase3-released" );
    }
    else if( state == SlotState::Ready )
    {
        transitionSlotState( slotIndex,
                             SlotState::Ready,
                             SlotState::Idle,
                             slot.phase3Mode,
                             slot.frameNumber,
                             slot.requestSerial,
                             "phase3-released-ready" );
    }
    else if( state != SlotState::Idle )
    {
        Q_ASSERT_X( false,
                    "RenderFrameThread::releaseSlotLocked",
                    "releasing a slot from an unexpected Phase 3 state" );
        slot.state.store( SlotState::Idle, std::memory_order_release );
    }
    slot.ready = false;
    slot.presenting = false;
    slot.phase3Mode = Phase3Mode::Disabled;
}

void RenderFrameThread::copySlotTelemetryLocked( const FrameSlot &slot )
{
    m_lastFrameUsedGpuBilinearDebayer = slot.usedGpuBilinearDebayer;
    m_lastGpuBilinearFallbackReason = slot.gpuBilinearFallbackReason;
    m_lastGpuBilinearRendererDescription = slot.gpuBilinearRendererDescription;
    m_lastFrameUsedGpuAmazeDebayer = slot.usedGpuAmazeDebayer;
    m_lastGpuAmazeFallbackReason = slot.gpuAmazeFallbackReason;
    m_lastGpuAmazeRendererDescription = slot.gpuAmazeRendererDescription;
    m_lastDualIsoPreviewHistogramMs = slot.dualIsoPreviewHistogramMs;
    m_lastDualIsoPreviewRegressionMs = slot.dualIsoPreviewRegressionMs;
    m_lastDualIsoPreviewRowscaleMs = slot.dualIsoPreviewRowscaleMs;
    m_lastFrameReadyEmitStageTime = slot.frameReadyEmitStageTime;
    m_lastStageTimingTelemetry = slot.stageTimingTelemetry;
    m_lastRenderThreadQueueWaitMs =
        slot.stageTimingTelemetry.value( QStringLiteral("render_thread_queue_wait_ms") ).toDouble();
    m_lastRenderThreadWorkMs =
        slot.stageTimingTelemetry.value( QStringLiteral("render_thread_work_ms") ).toDouble();
    m_lastRenderThreadTotalMs =
        slot.stageTimingTelemetry.value( QStringLiteral("render_thread_total_ms") ).toDouble();
}

//render the picture
void RenderFrameThread::drawFrame( int slotIndex,
                                   const uint16_t *decodedRawFrame,
                                   bool decodedRawFrameAlreadyReconned )
{
    FrameSlot &slot = m_frameSlots[slotIndex];
    const bool detailedTimelineTelemetry =
        playbackSmokeTimelineTelemetryEnabled();
    const bool requestStateTelemetry =
        playbackSmokeFrameTelemetryEnabled();
    const double drawFrameEntryStageTime =
        detailedTimelineTelemetry ? mlv_stage_timing_now() : 0.0;
    const bool preserveGpuPlaybackReconTextureSnapshot =
        decodedRawFrameAlreadyReconned
        && slot.gpuPlaybackReconTextureNoReadbackCandidate;
    std::vector<uint16_t> preservedGpuPlaybackReconTextureInputBayerFrame;
    bool preservedGpuPlaybackReconTextureInputBorrowedFromRawImage16 = false;
    const uint16_t *preservedGpuPlaybackReconTextureRetainedDeviceBayer16 = nullptr;
    int preservedGpuPlaybackReconTextureRetainedDeviceWidth = 0;
    int preservedGpuPlaybackReconTextureRetainedDeviceHeight = 0;
    uint64_t preservedGpuPlaybackReconTextureRetainedDeviceToken = 0;
    std::vector<uint16_t> preservedGpuPlaybackReconTextureBayerFrame;
    GpuPlaybackReconTextureState preservedGpuPlaybackReconTextureState;
    int preservedGpuPlaybackReconTextureBlackLevel = 0;
    std::array<double, 3> preservedGpuPlaybackReconTextureWbMultipliers{{1.0, 1.0, 1.0}};
    QJsonObject preservedGpuPlaybackReconTextureTelemetry;
    const auto preserveGpuPlaybackReconTextureTelemetry =
        [&slot, &preservedGpuPlaybackReconTextureTelemetry]( const char * key )
    {
        const QString field = QString::fromLatin1( key );
        if( slot.stageTimingTelemetry.contains( field ) )
        {
            preservedGpuPlaybackReconTextureTelemetry.insert(
                field,
                slot.stageTimingTelemetry.value( field ) );
        }
    };
    if( decodedRawFrameAlreadyReconned )
    {
        preserveGpuPlaybackReconTimingTelemetry(
            slot.stageTimingTelemetry,
            preservedGpuPlaybackReconTextureTelemetry );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_no_readback_recon_requested" );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_prepare_only_allowed" );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_prepare_only_used" );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_no_readback_input_words" );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_no_readback_input_borrowed" );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_retained_device_available" );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_retained_device_token" );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_no_readback_oracle_required" );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_no_readback_oracle_words" );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_no_readback_prepared_state_valid" );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_no_readback_state_snapshot_ok" );
        preserveGpuPlaybackReconTextureTelemetry(
            "gpu_playback_recon_texture_present_no_readback_fallback_reason" );
    }
    if( preserveGpuPlaybackReconTextureSnapshot )
    {
        preservedGpuPlaybackReconTextureInputBayerFrame =
            std::move( slot.gpuPlaybackReconTextureInputBayerFrame );
        preservedGpuPlaybackReconTextureInputBorrowedFromRawImage16 =
            slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16;
        preservedGpuPlaybackReconTextureRetainedDeviceBayer16 =
            slot.gpuPlaybackReconTextureRetainedDeviceBayer16;
        preservedGpuPlaybackReconTextureRetainedDeviceWidth =
            slot.gpuPlaybackReconTextureRetainedDeviceWidth;
        preservedGpuPlaybackReconTextureRetainedDeviceHeight =
            slot.gpuPlaybackReconTextureRetainedDeviceHeight;
        preservedGpuPlaybackReconTextureRetainedDeviceToken =
            slot.gpuPlaybackReconTextureRetainedDeviceToken;
        slot.gpuPlaybackReconTextureRetainedDeviceBayer16 = nullptr;
        slot.gpuPlaybackReconTextureRetainedDeviceWidth = 0;
        slot.gpuPlaybackReconTextureRetainedDeviceHeight = 0;
        slot.gpuPlaybackReconTextureRetainedDeviceToken = 0;
        preservedGpuPlaybackReconTextureBayerFrame =
            std::move( slot.gpuPlaybackReconTextureBayerFrame );
        preservedGpuPlaybackReconTextureState =
            slot.gpuPlaybackReconTextureState;
        preservedGpuPlaybackReconTextureBlackLevel =
            slot.gpuPlaybackReconTextureBlackLevel;
        preservedGpuPlaybackReconTextureWbMultipliers =
            slot.gpuPlaybackReconTextureWbMultipliers;
    }
    const double prologueBeforeResetMetadataStageTime =
        detailedTimelineTelemetry ? mlv_stage_timing_now() : 0.0;
    const QJsonObject preservedPhase3StageTimingTelemetry =
        detailedTimelineTelemetry ? slot.stageTimingTelemetry : QJsonObject();
    slot.resetMetadata();
    const double prologueAfterResetMetadataStageTime =
        detailedTimelineTelemetry ? mlv_stage_timing_now() : 0.0;
    slot.frameNumber = m_activeFrameNumber;
    slot.requestSerial = m_activeFrameRequestSerial;
    slot.outputMode = m_activeOutputMode;
    slot.presentationContext = m_activePresentationContext;
    if( preserveGpuPlaybackReconTextureSnapshot )
    {
        slot.gpuPlaybackReconTextureNoReadbackCandidate = true;
        slot.gpuPlaybackReconTextureInputBayerFrame =
            std::move( preservedGpuPlaybackReconTextureInputBayerFrame );
        slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16 =
            preservedGpuPlaybackReconTextureInputBorrowedFromRawImage16;
        slot.gpuPlaybackReconTextureRetainedDeviceBayer16 =
            preservedGpuPlaybackReconTextureRetainedDeviceBayer16;
        slot.gpuPlaybackReconTextureRetainedDeviceWidth =
            preservedGpuPlaybackReconTextureRetainedDeviceWidth;
        slot.gpuPlaybackReconTextureRetainedDeviceHeight =
            preservedGpuPlaybackReconTextureRetainedDeviceHeight;
        slot.gpuPlaybackReconTextureRetainedDeviceToken =
            preservedGpuPlaybackReconTextureRetainedDeviceToken;
        slot.gpuPlaybackReconTextureBayerFrame =
            std::move( preservedGpuPlaybackReconTextureBayerFrame );
        slot.gpuPlaybackReconTextureState =
            preservedGpuPlaybackReconTextureState;
        slot.gpuPlaybackReconTextureBlackLevel =
            preservedGpuPlaybackReconTextureBlackLevel;
        slot.gpuPlaybackReconTextureWbMultipliers =
            preservedGpuPlaybackReconTextureWbMultipliers;
    }
    const double prologueAfterSnapshotRestoreStageTime =
        detailedTimelineTelemetry ? mlv_stage_timing_now() : 0.0;
    for( auto it = preservedPhase3StageTimingTelemetry.begin();
         it != preservedPhase3StageTimingTelemetry.end();
         ++it )
    {
        slot.stageTimingTelemetry.insert( it.key(), it.value() );
    }
    for( auto it = preservedGpuPlaybackReconTextureTelemetry.begin();
         it != preservedGpuPlaybackReconTextureTelemetry.end();
         ++it )
    {
        slot.stageTimingTelemetry.insert( it.key(), it.value() );
    }
    const double prologueAfterPreservedTelemetryStageTime =
        detailedTimelineTelemetry ? mlv_stage_timing_now() : 0.0;
    const RenderRequest requestTelemetry = m_activeRenderRequest;
    const double requestIssueStageTime =
        requestTelemetry.presentationContext.renderRequestIssueStageTime;
    if( detailedTimelineTelemetry )
    {
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_draw_frame_entry_stage_time"),
            drawFrameEntryStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_prologue_before_reset_metadata_stage_time"),
            prologueBeforeResetMetadataStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_prologue_after_reset_metadata_stage_time"),
            prologueAfterResetMetadataStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_prologue_after_snapshot_restore_stage_time"),
            prologueAfterSnapshotRestoreStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_prologue_after_preserved_telemetry_stage_time"),
            prologueAfterPreservedTelemetryStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_request_issue_stage_time"),
            requestIssueStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_render_frame_entry_stage_time"),
            requestTelemetry.renderFrameEntryStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_request_stage_time"),
            requestTelemetry.requestStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_request_queue_push_stage_time"),
            requestTelemetry.requestQueuePushStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("phase3_loop_wake_stage_time"),
            requestTelemetry.phase3LoopWakeStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("phase3_render_request_take_stage_time"),
            requestTelemetry.phase3RenderRequestTakeStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_after_take_stamp_stage_time"),
            requestTelemetry.phase3AfterTakeStampStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_after_render_frame_flag_stage_time"),
            requestTelemetry.phase3AfterRenderFrameFlagStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_mode_branch_entry_stage_time"),
            requestTelemetry.phase3ModeBranchEntryStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_before_live_fallback_check_stage_time"),
            requestTelemetry.phase3BeforeLiveFallbackCheckStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_after_live_fallback_check_stage_time"),
            requestTelemetry.phase3AfterLiveFallbackCheckStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_before_kill_switch_check_stage_time"),
            requestTelemetry.phase3BeforeKillSwitchCheckStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_after_kill_switch_check_stage_time"),
            requestTelemetry.phase3AfterKillSwitchCheckStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_before_first_transition_stage_time"),
            requestTelemetry.phase3BeforeFirstTransitionStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_after_first_transition_stage_time"),
            requestTelemetry.phase3AfterFirstTransitionStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_before_second_transition_stage_time"),
            requestTelemetry.phase3BeforeSecondTransitionStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_after_second_transition_stage_time"),
            requestTelemetry.phase3AfterSecondTransitionStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_mode_branch_exit_stage_time"),
            requestTelemetry.phase3ModeBranchExitStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_policy_done_stage_time"),
            requestTelemetry.phase3PolicyDoneStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_active_assign_done_stage_time"),
            requestTelemetry.phase3ActiveAssignDoneStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_before_decode_ahead_stage_time"),
            requestTelemetry.phase3BeforeDecodeAheadStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_after_decode_ahead_stage_time"),
            requestTelemetry.phase3AfterDecodeAheadStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_before_unlock_stage_time"),
            requestTelemetry.phase3BeforeUnlockStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_after_unlock_stage_time"),
            requestTelemetry.phase3AfterUnlockStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_render_decoded_slot_entry_stage_time"),
            requestTelemetry.phase3RenderDecodedSlotEntryStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_before_draw_frame_stage_time"),
            requestTelemetry.phase3BeforeDrawFrameStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_run_serial_active_assign_done_stage_time"),
            requestTelemetry.runSerialActiveAssignDoneStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_run_serial_phase_policy_done_stage_time"),
            requestTelemetry.runSerialPhasePolicyDoneStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_run_serial_before_unlock_stage_time"),
            requestTelemetry.runSerialBeforeUnlockStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_run_serial_after_unlock_stage_time"),
            requestTelemetry.runSerialAfterUnlockStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_run_serial_before_draw_frame_stage_time"),
            requestTelemetry.runSerialBeforeDrawFrameStageTime );
        if( requestTelemetry.renderFrameEntryStageTime > 0.0
         && requestIssueStageTime > 0.0
         && requestTelemetry.renderFrameEntryStageTime >= requestIssueStageTime )
        {
            slot.stageTimingTelemetry.insert(
                QStringLiteral("render_thread_request_issue_to_entry_ms"),
                ( requestTelemetry.renderFrameEntryStageTime - requestIssueStageTime )
                    * 1000.0 );
        }
        if( requestTelemetry.requestStageTime > 0.0
         && requestIssueStageTime > 0.0
         && requestTelemetry.requestStageTime >= requestIssueStageTime )
        {
            slot.stageTimingTelemetry.insert(
                QStringLiteral("render_thread_request_issue_to_request_ms"),
                ( requestTelemetry.requestStageTime - requestIssueStageTime )
                    * 1000.0 );
        }
        if( requestTelemetry.requestQueuePushStageTime > 0.0
         && requestIssueStageTime > 0.0
         && requestTelemetry.requestQueuePushStageTime >= requestIssueStageTime )
        {
            slot.stageTimingTelemetry.insert(
                QStringLiteral("render_thread_request_issue_to_queue_push_ms"),
                ( requestTelemetry.requestQueuePushStageTime - requestIssueStageTime )
                    * 1000.0 );
        }
        slot.stageTimingTelemetry.insert(
            QStringLiteral("playback_timeline_advance_request"),
            requestTelemetry.presentationContext.playbackTimelineAdvanceRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("playback_timeline_advance_early"),
            requestTelemetry.presentationContext.playbackTimelineAdvanceEarly );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("playback_timeline_predictive_gpu_tex_nr"),
            requestTelemetry.presentationContext.playbackTimelinePredictiveGpuTexNr );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("playback_timeline_source_request_serial"),
            static_cast<qint64>(
                requestTelemetry.presentationContext.playbackTimelineSourceRequestSerial ) );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("playback_timeline_source_frame"),
            static_cast<qint64>(
                requestTelemetry.presentationContext.playbackTimelineSourceFrame ) );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("playback_timeline_advance_issue_stage_time"),
            requestTelemetry.presentationContext.playbackTimelineAdvanceIssueStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("playback_timeline_source_frame_ready_emit_stage_time"),
            requestTelemetry.presentationContext.playbackTimelineSourceFrameReadyEmitStageTime );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("playback_timeline_source_draw_begin_stage_time"),
            requestTelemetry.presentationContext.playbackTimelineSourceDrawBeginStageTime );
    }
    if( requestStateTelemetry )
    {
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_busy_at_request"),
            requestTelemetry.renderThreadBusyAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_rendering_at_request"),
            requestTelemetry.renderThreadRenderingAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_queued_at_request"),
            requestTelemetry.renderThreadQueuedAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_work_in_flight_at_request"),
            requestTelemetry.phase3WorkInFlightAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_request_queue_depth_at_request"),
            requestTelemetry.renderRequestQueueDepthAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_free_slot_count_at_request"),
            requestTelemetry.freeSlotCountAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_ready_slot_count_at_request"),
            requestTelemetry.readySlotCountAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_presenting_slot_count_at_request"),
            requestTelemetry.presentingSlotCountAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_phase3_active_slot_count_at_request"),
            requestTelemetry.phase3ActiveSlotCountAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_decode_request_count_at_request"),
            requestTelemetry.decodeRequestCountAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_recon_request_count_at_request"),
            requestTelemetry.reconRequestCountAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_decode_ready_slot_count_at_request"),
            requestTelemetry.decodeReadySlotCountAtRequest );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_process_ready_slot_count_at_request"),
            requestTelemetry.processReadySlotCountAtRequest );
    }

    const double render_start = mlv_stage_timing_now();
    if( detailedTimelineTelemetry )
    {
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_start_stage_time"),
            render_start );
        const auto telemetryStageTime =
            [&slot]( const char * key ) -> double
        {
            return slot.stageTimingTelemetry.value(
                QString::fromLatin1( key ) ).toDouble();
        };
        const auto insertStageDeltaMs =
            [&slot]( const char * key, double start, double end )
        {
            if( start > 0.0 && end >= start )
            {
                slot.stageTimingTelemetry.insert(
                    QString::fromLatin1( key ),
                    ( end - start ) * 1000.0 );
            }
        };
        const double phase3DecodeQueueStageTime =
            telemetryStageTime( "phase3_decode_queue_stage_time" );
        const double phase3LoopWakeStageTime =
            telemetryStageTime( "phase3_loop_wake_stage_time" );
        const double phase3RenderRequestTakeStageTime =
            telemetryStageTime( "phase3_render_request_take_stage_time" );
        const double phase3DecodeStartStageTime =
            telemetryStageTime( "phase3_decode_start_stage_time" );
        const double phase3DecodeEndStageTime =
            telemetryStageTime( "phase3_decode_end_stage_time" );
        const double phase3DecodeDoneSignalStageTime =
            telemetryStageTime( "phase3_decode_done_signal_stage_time" );
        const double phase3DecodeReadyTakeStageTime =
            telemetryStageTime( "phase3_decode_ready_take_stage_time" );
        const double phase3ReconStartStageTime =
            telemetryStageTime( "phase3_recon_start_stage_time" );
        const double phase3ReconEndStageTime =
            telemetryStageTime( "phase3_recon_end_stage_time" );
        const double phase3ProcessReadySignalStageTime =
            telemetryStageTime( "phase3_process_ready_signal_stage_time" );
        const double phase3ProcessReadyTakeStageTime =
            telemetryStageTime( "phase3_process_ready_take_stage_time" );
        insertStageDeltaMs( "render_thread_request_to_draw_frame_entry_ms",
                            requestTelemetry.requestStageTime,
                            drawFrameEntryStageTime );
        insertStageDeltaMs( "render_thread_queue_push_to_draw_frame_entry_ms",
                            requestTelemetry.requestQueuePushStageTime,
                            drawFrameEntryStageTime );
        insertStageDeltaMs( "render_thread_run_serial_take_to_active_assign_ms",
                            requestTelemetry.phase3RenderRequestTakeStageTime,
                            requestTelemetry.runSerialActiveAssignDoneStageTime );
        insertStageDeltaMs( "render_thread_run_serial_active_assign_to_phase_policy_ms",
                            requestTelemetry.runSerialActiveAssignDoneStageTime,
                            requestTelemetry.runSerialPhasePolicyDoneStageTime );
        insertStageDeltaMs( "render_thread_run_serial_phase_policy_to_before_unlock_ms",
                            requestTelemetry.runSerialPhasePolicyDoneStageTime,
                            requestTelemetry.runSerialBeforeUnlockStageTime );
        insertStageDeltaMs( "render_thread_run_serial_before_unlock_to_after_unlock_ms",
                            requestTelemetry.runSerialBeforeUnlockStageTime,
                            requestTelemetry.runSerialAfterUnlockStageTime );
        insertStageDeltaMs( "render_thread_run_serial_after_unlock_to_before_draw_frame_ms",
                            requestTelemetry.runSerialAfterUnlockStageTime,
                            requestTelemetry.runSerialBeforeDrawFrameStageTime );
        insertStageDeltaMs( "render_thread_run_serial_take_to_before_draw_frame_ms",
                            requestTelemetry.phase3RenderRequestTakeStageTime,
                            requestTelemetry.runSerialBeforeDrawFrameStageTime );
        insertStageDeltaMs( "render_thread_run_serial_before_draw_frame_to_draw_entry_ms",
                            requestTelemetry.runSerialBeforeDrawFrameStageTime,
                            drawFrameEntryStageTime );
        insertStageDeltaMs( "render_thread_phase3_take_to_policy_done_ms",
                            requestTelemetry.phase3RenderRequestTakeStageTime,
                            requestTelemetry.phase3PolicyDoneStageTime );
        insertStageDeltaMs( "render_thread_phase3_take_to_after_take_stamp_ms",
                            requestTelemetry.phase3RenderRequestTakeStageTime,
                            requestTelemetry.phase3AfterTakeStampStageTime );
        insertStageDeltaMs( "render_thread_phase3_after_take_to_render_frame_flag_ms",
                            requestTelemetry.phase3AfterTakeStampStageTime,
                            requestTelemetry.phase3AfterRenderFrameFlagStageTime );
        insertStageDeltaMs( "render_thread_phase3_render_frame_flag_to_mode_branch_ms",
                            requestTelemetry.phase3AfterRenderFrameFlagStageTime,
                            requestTelemetry.phase3ModeBranchEntryStageTime );
        insertStageDeltaMs( "render_thread_phase3_mode_branch_to_live_fallback_ms",
                            requestTelemetry.phase3ModeBranchEntryStageTime,
                            requestTelemetry.phase3BeforeLiveFallbackCheckStageTime );
        insertStageDeltaMs( "render_thread_phase3_live_fallback_check_ms",
                            requestTelemetry.phase3BeforeLiveFallbackCheckStageTime,
                            requestTelemetry.phase3AfterLiveFallbackCheckStageTime );
        insertStageDeltaMs( "render_thread_phase3_after_live_fallback_to_kill_switch_ms",
                            requestTelemetry.phase3AfterLiveFallbackCheckStageTime,
                            requestTelemetry.phase3BeforeKillSwitchCheckStageTime );
        insertStageDeltaMs( "render_thread_phase3_kill_switch_check_ms",
                            requestTelemetry.phase3BeforeKillSwitchCheckStageTime,
                            requestTelemetry.phase3AfterKillSwitchCheckStageTime );
        insertStageDeltaMs( "render_thread_phase3_after_kill_switch_to_first_transition_ms",
                            requestTelemetry.phase3AfterKillSwitchCheckStageTime,
                            requestTelemetry.phase3BeforeFirstTransitionStageTime );
        insertStageDeltaMs( "render_thread_phase3_first_transition_ms",
                            requestTelemetry.phase3BeforeFirstTransitionStageTime,
                            requestTelemetry.phase3AfterFirstTransitionStageTime );
        insertStageDeltaMs( "render_thread_phase3_between_transitions_ms",
                            requestTelemetry.phase3AfterFirstTransitionStageTime,
                            requestTelemetry.phase3BeforeSecondTransitionStageTime );
        insertStageDeltaMs( "render_thread_phase3_second_transition_ms",
                            requestTelemetry.phase3BeforeSecondTransitionStageTime,
                            requestTelemetry.phase3AfterSecondTransitionStageTime );
        insertStageDeltaMs( "render_thread_phase3_second_transition_to_branch_exit_ms",
                            requestTelemetry.phase3AfterSecondTransitionStageTime,
                            requestTelemetry.phase3ModeBranchExitStageTime );
        insertStageDeltaMs( "render_thread_phase3_branch_exit_to_policy_done_ms",
                            requestTelemetry.phase3ModeBranchExitStageTime,
                            requestTelemetry.phase3PolicyDoneStageTime );
        insertStageDeltaMs( "render_thread_phase3_take_to_mode_branch_exit_ms",
                            requestTelemetry.phase3RenderRequestTakeStageTime,
                            requestTelemetry.phase3ModeBranchExitStageTime );
        insertStageDeltaMs( "render_thread_phase3_policy_to_active_assign_ms",
                            requestTelemetry.phase3PolicyDoneStageTime,
                            requestTelemetry.phase3ActiveAssignDoneStageTime );
        insertStageDeltaMs( "render_thread_phase3_active_assign_to_before_decode_ahead_ms",
                            requestTelemetry.phase3ActiveAssignDoneStageTime,
                            requestTelemetry.phase3BeforeDecodeAheadStageTime );
        insertStageDeltaMs( "render_thread_phase3_decode_ahead_ms",
                            requestTelemetry.phase3BeforeDecodeAheadStageTime,
                            requestTelemetry.phase3AfterDecodeAheadStageTime );
        insertStageDeltaMs( "render_thread_phase3_after_decode_ahead_to_before_unlock_ms",
                            requestTelemetry.phase3AfterDecodeAheadStageTime,
                            requestTelemetry.phase3BeforeUnlockStageTime );
        insertStageDeltaMs( "render_thread_phase3_before_unlock_to_after_unlock_ms",
                            requestTelemetry.phase3BeforeUnlockStageTime,
                            requestTelemetry.phase3AfterUnlockStageTime );
        insertStageDeltaMs( "render_thread_phase3_after_unlock_to_render_decoded_slot_entry_ms",
                            requestTelemetry.phase3AfterUnlockStageTime,
                            requestTelemetry.phase3RenderDecodedSlotEntryStageTime );
        insertStageDeltaMs( "render_thread_phase3_render_decoded_slot_entry_to_before_draw_frame_ms",
                            requestTelemetry.phase3RenderDecodedSlotEntryStageTime,
                            requestTelemetry.phase3BeforeDrawFrameStageTime );
        insertStageDeltaMs( "render_thread_phase3_before_draw_frame_to_draw_entry_ms",
                            requestTelemetry.phase3BeforeDrawFrameStageTime,
                            drawFrameEntryStageTime );
        insertStageDeltaMs( "render_thread_phase3_take_to_before_draw_frame_ms",
                            requestTelemetry.phase3RenderRequestTakeStageTime,
                            requestTelemetry.phase3BeforeDrawFrameStageTime );
        insertStageDeltaMs( "render_thread_phase3_take_to_draw_frame_entry_ms",
                            requestTelemetry.phase3RenderRequestTakeStageTime,
                            drawFrameEntryStageTime );
        insertStageDeltaMs( "render_thread_draw_frame_entry_to_start_ms",
                            drawFrameEntryStageTime,
                            render_start );
        insertStageDeltaMs( "render_thread_prologue_snapshot_preserve_ms",
                            drawFrameEntryStageTime,
                            prologueBeforeResetMetadataStageTime );
        insertStageDeltaMs( "render_thread_prologue_reset_metadata_ms",
                            prologueBeforeResetMetadataStageTime,
                            prologueAfterResetMetadataStageTime );
        insertStageDeltaMs( "render_thread_prologue_snapshot_restore_ms",
                            prologueAfterResetMetadataStageTime,
                            prologueAfterSnapshotRestoreStageTime );
        insertStageDeltaMs( "render_thread_prologue_preserved_telemetry_ms",
                            prologueAfterSnapshotRestoreStageTime,
                            prologueAfterPreservedTelemetryStageTime );
        insertStageDeltaMs( "render_thread_prologue_telemetry_setup_ms",
                            prologueAfterPreservedTelemetryStageTime,
                            render_start );
        insertStageDeltaMs( "phase3_request_to_decode_queue_ms",
                            requestTelemetry.requestStageTime,
                            phase3DecodeQueueStageTime );
        insertStageDeltaMs( "phase3_request_queue_push_to_loop_wake_ms",
                            requestTelemetry.requestQueuePushStageTime,
                            phase3LoopWakeStageTime );
        insertStageDeltaMs( "phase3_loop_wake_to_render_request_take_ms",
                            phase3LoopWakeStageTime,
                            phase3RenderRequestTakeStageTime );
        insertStageDeltaMs( "phase3_request_queue_push_to_take_ms",
                            requestTelemetry.requestQueuePushStageTime,
                            phase3RenderRequestTakeStageTime );
        insertStageDeltaMs( "phase3_render_request_take_to_decode_queue_ms",
                            phase3RenderRequestTakeStageTime,
                            phase3DecodeQueueStageTime );
        insertStageDeltaMs( "phase3_decode_queue_wait_ms",
                            phase3DecodeQueueStageTime,
                            phase3DecodeStartStageTime );
        insertStageDeltaMs( "phase3_decode_worker_ms",
                            phase3DecodeStartStageTime,
                            phase3DecodeEndStageTime );
        insertStageDeltaMs( "phase3_decode_done_to_recon_start_ms",
                            phase3DecodeDoneSignalStageTime,
                            phase3ReconStartStageTime );
        insertStageDeltaMs( "phase3_decode_done_to_process_take_ms",
                            phase3DecodeDoneSignalStageTime,
                            phase3DecodeReadyTakeStageTime );
        insertStageDeltaMs( "phase3_recon_worker_ms",
                            phase3ReconStartStageTime,
                            phase3ReconEndStageTime );
        insertStageDeltaMs( "phase3_recon_initial_state_transitions_ms",
                            phase3ReconStartStageTime,
                            telemetryStageTime( "phase3_recon_after_state_transitions_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_scope_setup_ms",
                            telemetryStageTime( "phase3_recon_before_scope_setup_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_scope_setup_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_capture_set_frame_ms",
                            telemetryStageTime( "phase3_recon_before_capture_set_frame_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_capture_set_frame_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_apply_llrawproc_wall_ms",
                            telemetryStageTime( "phase3_recon_before_apply_llrawproc_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_apply_llrawproc_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_timing_capture_ms",
                            telemetryStageTime( "phase3_recon_after_apply_llrawproc_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_timing_capture_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_oracle_snapshot_ms",
                            telemetryStageTime( "phase3_recon_before_oracle_snapshot_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_oracle_snapshot_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_prepared_input_snapshot_ms",
                            telemetryStageTime( "phase3_recon_before_prepared_input_snapshot_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_prepared_input_snapshot_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_retained_device_query_ms",
                            telemetryStageTime( "phase3_recon_before_retained_device_query_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_retained_device_query_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_prepared_state_snapshot_ms",
                            telemetryStageTime( "phase3_recon_before_prepared_state_snapshot_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_prepared_state_snapshot_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_candidate_validation_ms",
                            telemetryStageTime( "phase3_recon_before_candidate_validation_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_candidate_validation_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_no_readback_state_ms",
                            telemetryStageTime( "phase3_recon_before_no_readback_state_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_no_readback_state_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_metadata_insert_ms",
                            telemetryStageTime( "phase3_recon_after_no_readback_state_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_metadata_insert_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_process_ready_transition_ms",
                            telemetryStageTime( "phase3_recon_before_process_ready_transition_stage_time" ),
                            telemetryStageTime( "phase3_recon_after_process_ready_transition_stage_time" ) );
        insertStageDeltaMs( "phase3_recon_end_to_process_ready_signal_ms",
                            phase3ReconEndStageTime,
                            phase3ProcessReadySignalStageTime );
        insertStageDeltaMs( "phase3_process_ready_signal_to_take_ms",
                            phase3ProcessReadySignalStageTime,
                            phase3ProcessReadyTakeStageTime );
        insertStageDeltaMs( "phase3_process_ready_take_to_render_start_ms",
                            phase3ProcessReadyTakeStageTime,
                            render_start );
        insertStageDeltaMs( "phase3_process_ready_take_to_draw_frame_entry_ms",
                            phase3ProcessReadyTakeStageTime,
                            drawFrameEntryStageTime );
        insertStageDeltaMs( "phase3_process_ready_signal_to_render_start_ms",
                            phase3ProcessReadySignalStageTime,
                            render_start );
        insertStageDeltaMs( "phase3_request_to_process_ready_signal_ms",
                            requestTelemetry.requestStageTime,
                            phase3ProcessReadySignalStageTime );
        if( requestIssueStageTime > 0.0 && render_start >= requestIssueStageTime )
        {
            slot.stageTimingTelemetry.insert(
                QStringLiteral("render_thread_request_issue_to_start_ms"),
                ( render_start - requestIssueStageTime ) * 1000.0 );
        }
    }
    const uint32_t frameNumber = slot.frameNumber;
    const OutputMode outputMode = slot.outputMode;
    const bool useGpuBilinearDebayer = m_activeUseGpuBilinearDebayer;
    const bool useGpuAmazeDebayer = m_activeUseGpuAmazeDebayer;
    const int playbackScaleFactor = m_activePresentationContext.playbackScaleFactor;
    const bool playbackPreviewFastPathActive =
        outputMode == OutputProcessed8
        && m_activePresentationContext.playbackActive
        && m_activePresentationContext.renderThreadUsingPlaybackPreviewProcessing;
    struct PlaybackPreviewModeGuard
    {
        PlaybackPreviewModeGuard( bool enabled, int scaleFactor )
            : previousPreviewMode( processingPlaybackPreviewModeEnabled() )
            , previousAggressivePreviewMode( processingPlaybackAggressivePreviewModeEnabled() )
            , previousScaleFactor( processingPlaybackPreviewScaleFactor() )
        {
            processingSetPlaybackPreviewMode( enabled ? 1 : 0 );
            processingSetPlaybackAggressivePreviewMode(
                ( enabled && mlvPlaybackAggressivePreviewMode() != 0 ) ? 1 : 0 );
            processingSetPlaybackPreviewScaleFactor( enabled ? scaleFactor : 1 );
        }

        ~PlaybackPreviewModeGuard()
        {
            processingSetPlaybackPreviewScaleFactor( previousScaleFactor );
            processingSetPlaybackAggressivePreviewMode( previousAggressivePreviewMode );
            processingSetPlaybackPreviewMode( previousPreviewMode );
        }

        int previousPreviewMode;
        int previousAggressivePreviewMode;
        int previousScaleFactor;
    } playbackPreviewModeGuard( playbackPreviewFastPathActive, playbackScaleFactor );
    const GpuPlaybackReconScope gpuPlaybackReconScope(
        m_activePresentationContext.playbackActive );
    const double frameRequestStageTime = m_activeFrameRequestStageTime;
    const double renderThreadQueueWaitMs =
        (frameRequestStageTime > 0.0 && render_start >= frameRequestStageTime)
            ? (render_start - frameRequestStageTime) * 1000.0
            : 0.0;

    mlv_stage_timing_reset_snapshot();
    if ( !useGpuBilinearDebayer )
    {
        slot.gpuBilinearFallbackReason.clear();
    }
    if ( !useGpuAmazeDebayer )
    {
        slot.gpuAmazeFallbackReason.clear();
    }

    /* Read the requested scale factor. The MLV core can still reject invalid
     * alignment, so request and active values are logged separately. */
    int renderedImageWidth = m_imageWidth;
    int renderedImageHeight = m_imageHeight;
    if( m_pMlvObject
     && ( outputMode == OutputProcessed8 || outputMode == OutputProcessed16 ) )
    {
        mlvFrameOutputDimensions( m_pMlvObject,
                                  playbackScaleFactor,
                                  &renderedImageWidth,
                                  &renderedImageHeight );
    }
    if( renderedImageWidth <= 0 ) renderedImageWidth = m_imageWidth;
    if( renderedImageHeight <= 0 ) renderedImageHeight = m_imageHeight;
    slot.renderedImageWidth = renderedImageWidth;
    slot.renderedImageHeight = renderedImageHeight;
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_playback_scale_factor_request"),
        playbackScaleFactor );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_playback_active"),
        m_activePresentationContext.playbackActive );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_drop_frame_playback_active"),
        m_activePresentationContext.dropFramePlaybackActive );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_queued_playback_drops_before_start"),
        m_activeQueuedPlaybackDropCount );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_rendered_width"),
        renderedImageWidth );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_rendered_height"),
        renderedImageHeight );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_playback_preview_fast_path"),
        playbackPreviewFastPathActive );

    const int generalWorkerThreads = mlvappEffectiveWorkerThreadCount();
    const int workerThreads = mlvappEffectivePlaybackWorkerThreadCount();
    const int openMpThreads = mlvappApplyPlaybackOpenMpThreadCount(workerThreads);
    if( m_pMlvObject )
    {
        setMlvCpuCores( m_pMlvObject, workerThreads );
    }
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_worker_threads"),
        workerThreads );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_worker_thread_cap_active"),
        workerThreads < generalWorkerThreads );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_openmp_threads"),
        openMpThreads );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_openmp_thread_cap_active"),
        openMpThreads < generalWorkerThreads );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_frame_slot_count"),
        m_frameSlots.size() );

    GpuAmazeDebayerBackendAvailability r16AmazeTextureAvailability;
    r16AmazeTextureAvailability.available =
        m_activePresentationContext.gpuPlaybackReconAmazeTexturePresentAdmitted;
    r16AmazeTextureAvailability.reason =
        m_activePresentationContext.gpuPlaybackReconAmazeTexturePresentFallbackReason;
    r16AmazeTextureAvailability.rendererDescription =
        m_activePresentationContext.gpuPlaybackReconAmazeTexturePresentRenderer;
    bool skipCpuDebayerForGpuTextureNoReadback = false;
    const bool gpuTexNrSkipOutputModeEligible =
        ( outputMode == OutputDebayered16 || outputMode == OutputProcessed8 );
    const bool gpuTexNrSkipRawAvailable = !slot.rawImage16.empty();
    const bool gpuTexNrSkipTextureRequested =
        m_activePresentationContext.gpuPlaybackReconTexturePresentRequested;
    const bool gpuTexNrSkipScaleEligible = playbackScaleFactor == 1;
    const bool gpuTexNrSkipCandidate =
        slot.gpuPlaybackReconTextureNoReadbackCandidate;
    const size_t gpuTexNrInputWords =
        slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16
            ? slot.rawImage16.size()
            : slot.gpuPlaybackReconTextureInputBayerFrame.size();
    const bool gpuTexNrSkipInputAvailable =
        gpuTexNrInputWords > 0;
    const bool gpuTexNrSkipStateMatches =
        slot.gpuPlaybackReconTextureState.valid
        && slot.gpuPlaybackReconTextureState.width == m_imageWidth
        && slot.gpuPlaybackReconTextureState.height == m_imageHeight;
    const bool gpuTexNrSkipNeedsPreviewFrameState =
        gpuPreviewProcessingNeedsShadowsHighlightsFrameState(
            m_activePresentationContext.gpuPreviewProcessingConfig);
    const bool gpuTexNrDisplayLutOnlyShStateBypass =
        gpuPlaybackReconDisplayLutOnlySkipShadowsHighlightsFrameStateEnabled()
        && gpuTexNrSkipNeedsPreviewFrameState
        && !gpuPreviewProcessingDisplayShaderUsesShadowsHighlightsFrameState(
            m_activePresentationContext.gpuPreviewProcessingConfig)
        && gpuTexNrSkipOutputModeEligible
        && gpuTexNrSkipTextureRequested
        && gpuTexNrSkipScaleEligible
        && gpuTexNrSkipCandidate;
    const bool gpuTexNrSkipCanReusePreviewFrameState =
        gpuTexNrSkipNeedsPreviewFrameState
        && !gpuTexNrDisplayLutOnlyShStateBypass
        && gpuPlaybackReconReuseShadowsHighlightsFrameStateEnabled()
        && gpuPlaybackReconShadowsHighlightsFrameStateAvailable(
            m_activePresentationContext.gpuPreviewProcessingConfig,
            m_pMlvObject ? m_pMlvObject->processing : nullptr,
            m_imageWidth,
            m_imageHeight);
    bool gpuTexNrFastShFrameStateAttempted = false;
    bool gpuTexNrFastShFrameStateReady = false;
    bool gpuTexNrFastShFrameStateAllocated = false;
    QString gpuTexNrFastShFrameStateReason;
    double gpuTexNrFastShDebayerMs = 0.0;
    double gpuTexNrFastShRefreshMs = 0.0;
    const size_t fullResPixelCountForGpuTexNr =
        static_cast<size_t>( qMax( 0, m_imageWidth ) )
        * static_cast<size_t>( qMax( 0, m_imageHeight ) );
    const bool gpuTexNrFastShFrameStateEligible =
        gpuPlaybackReconFastShadowsHighlightsFrameStateEnabled()
        && gpuTexNrSkipNeedsPreviewFrameState
        && !gpuTexNrDisplayLutOnlyShStateBypass
        && !gpuTexNrSkipCanReusePreviewFrameState
        && gpuTexNrSkipOutputModeEligible
        && gpuTexNrSkipRawAvailable
        && gpuTexNrSkipTextureRequested
        && gpuTexNrSkipScaleEligible
        && gpuTexNrSkipCandidate
        && gpuTexNrSkipInputAvailable
        && gpuTexNrSkipStateMatches
        && fullResPixelCountForGpuTexNr > 0
        && slot.rawImage16.size() >= fullResPixelCountForGpuTexNr
        && m_pMlvObject
        && m_pMlvObject->processing;
    if( gpuTexNrFastShFrameStateEligible )
    {
        gpuTexNrFastShFrameStateAttempted = true;
        try
        {
            m_gpuPlaybackReconStateRgb16.resize(
                fullResPixelCountForGpuTexNr * 3u );
            gpuTexNrFastShFrameStateAllocated = true;
        }
        catch( const std::bad_alloc & )
        {
            m_gpuPlaybackReconStateRgb16.clear();
            gpuTexNrFastShFrameStateReason =
                QStringLiteral("fast S/H frame-state RGB allocation failed");
        }

        if( gpuTexNrFastShFrameStateAllocated )
        {
            const int bitShift =
                llrpHQDualIso( m_pMlvObject )
                    ? 0
                    : ( 16 - m_pMlvObject->RAWI.raw_info.bits_per_pixel );
            const double debayerStart = mlv_stage_timing_now();
            debayerBasicU16( m_gpuPlaybackReconStateRgb16.data(),
                             slot.rawImage16.data(),
                             m_imageWidth,
                             m_imageHeight,
                             workerThreads,
                             bitShift );
            gpuTexNrFastShDebayerMs =
                ( mlv_stage_timing_now() - debayerStart ) * 1000.0;

            const double refreshStart = mlv_stage_timing_now();
            const int previousPreviewMode =
                processingPlaybackPreviewModeEnabled();
            const int previousAggressivePreviewMode =
                processingPlaybackAggressivePreviewModeEnabled();
            const int previousPreviewScaleFactor =
                processingPlaybackPreviewScaleFactor();
            processingSetPlaybackPreviewMode( 1 );
            processingSetPlaybackAggressivePreviewMode(
                mlvPlaybackAggressivePreviewMode() != 0 ? 1 : 0 );
            processingSetPlaybackPreviewScaleFactor( playbackScaleFactor );
            const int refreshed =
                processingRefreshShadowsHighlightsBlurFromRgb16(
                    m_pMlvObject->processing,
                    m_gpuPlaybackReconStateRgb16.data(),
                    m_imageWidth,
                    m_imageHeight,
                    workerThreads,
                    0 );
            processingSetPlaybackPreviewScaleFactor(
                previousPreviewScaleFactor );
            processingSetPlaybackAggressivePreviewMode(
                previousAggressivePreviewMode );
            processingSetPlaybackPreviewMode( previousPreviewMode );
            gpuTexNrFastShRefreshMs =
                ( mlv_stage_timing_now() - refreshStart ) * 1000.0;
            gpuTexNrFastShFrameStateReady =
                refreshed != 0
                && gpuPlaybackReconShadowsHighlightsFrameStateAvailable(
                    m_activePresentationContext.gpuPreviewProcessingConfig,
                    m_pMlvObject->processing,
                    m_imageWidth,
                    m_imageHeight);
            if( !gpuTexNrFastShFrameStateReady )
            {
                gpuTexNrFastShFrameStateReason =
                    QStringLiteral("fast S/H frame-state refresh did not produce matching blur data");
            }
        }
    }
    const bool gpuTexNrSkipHasRequiredPreviewFrameState =
        !gpuTexNrSkipNeedsPreviewFrameState
        || gpuTexNrDisplayLutOnlyShStateBypass
        || gpuTexNrSkipCanReusePreviewFrameState
        || gpuTexNrFastShFrameStateReady;
    if( gpuTexNrSkipOutputModeEligible
     && gpuTexNrSkipRawAvailable
     && gpuTexNrSkipTextureRequested
     && gpuTexNrSkipScaleEligible
     && gpuTexNrSkipCandidate
     && gpuTexNrSkipInputAvailable
     && gpuTexNrSkipStateMatches
     && gpuTexNrSkipHasRequiredPreviewFrameState )
    {
        skipCpuDebayerForGpuTextureNoReadback =
            r16AmazeTextureAvailability.available;
    }
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_output_mode"),
        static_cast<int>( outputMode ) );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_output_mode_eligible"),
        gpuTexNrSkipOutputModeEligible );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_raw_available"),
        gpuTexNrSkipRawAvailable );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_texture_requested"),
        gpuTexNrSkipTextureRequested );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_scale_eligible"),
        gpuTexNrSkipScaleEligible );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_no_readback_candidate"),
        gpuTexNrSkipCandidate );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_input_words"),
        static_cast<qint64>( gpuTexNrInputWords ) );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_input_borrowed"),
        slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16 );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_state_matches"),
        gpuTexNrSkipStateMatches );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_preview_frame_state_needed"),
        gpuTexNrSkipNeedsPreviewFrameState );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_display_lut_only_sh_frame_state_bypass"),
        gpuTexNrDisplayLutOnlyShStateBypass );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_reuse_sh_frame_state_enabled"),
        gpuPlaybackReconReuseShadowsHighlightsFrameStateEnabled() );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_reuse_sh_frame_state_ready"),
        gpuTexNrSkipCanReusePreviewFrameState );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_fast_sh_frame_state_enabled"),
        gpuPlaybackReconFastShadowsHighlightsFrameStateEnabled() );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_fast_sh_frame_state_eligible"),
        gpuTexNrFastShFrameStateEligible );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_fast_sh_frame_state_attempted"),
        gpuTexNrFastShFrameStateAttempted );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_fast_sh_frame_state_ready"),
        gpuTexNrFastShFrameStateReady );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_fast_sh_debayer_ms"),
        gpuTexNrFastShDebayerMs );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_fast_sh_refresh_ms"),
        gpuTexNrFastShRefreshMs );
    if( !gpuTexNrFastShFrameStateReason.isEmpty() )
    {
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_fast_sh_frame_state_reason"),
            gpuTexNrFastShFrameStateReason );
    }
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_skip_gate_gui_admitted"),
        m_activePresentationContext.gpuPlaybackReconAmazeTexturePresentAdmitted );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("gpu_playback_recon_amaze_texture_present_preflight_available"),
        r16AmazeTextureAvailability.available );
    if( !r16AmazeTextureAvailability.reason.isEmpty() )
    {
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_amaze_texture_present_preflight_reason"),
            r16AmazeTextureAvailability.reason );
    }
    if( !r16AmazeTextureAvailability.rendererDescription.isEmpty() )
    {
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_amaze_texture_present_preflight_renderer"),
            r16AmazeTextureAvailability.rendererDescription );
    }

    if( skipCpuDebayerForGpuTextureNoReadback )
    {
        slot.usedGpuAmazeDebayer = true;
        slot.gpuAmazeFallbackReason.clear();
        slot.gpuAmazeRendererDescription =
            r16AmazeTextureAvailability.rendererDescription.isEmpty()
                ? QStringLiteral("pending CUDA AMaZE R16 texture present")
                : r16AmazeTextureAvailability.rendererDescription;
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_amaze_debayer_active"),
            true );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_amaze_texture_present_candidate"),
            true );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_cpu_amaze_debayer_skipped_for_gpu_tex_nr"),
            true );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_processed8_skipped_for_gpu_tex_nr"),
            outputMode == OutputProcessed8 );
        mlv_stage_timing_note_elapsed(outputMode == OutputProcessed8
                                          ? "render_thread_draw"
                                          : "render_thread_draw16_debayered",
                                      frameNumber,
                                      0.0);
    }
    else if ( outputMode == OutputProcessed16 && !slot.rawImage16.empty() )
    {
        getMlvProcessedFrame16Scaled( m_pMlvObject,
                                      frameNumber,
                                      slot.rawImage16.data(),
                                      workerThreads,
                                      playbackScaleFactor );
        slot.dualIsoPreviewHistogramMs = llrpGetLastDualIsoPreviewHistogramMilliseconds();
        slot.dualIsoPreviewRegressionMs = llrpGetLastDualIsoPreviewRegressionMilliseconds();
        slot.dualIsoPreviewRowscaleMs = llrpGetLastDualIsoPreviewRowscaleMilliseconds();
        mlv_stage_timing_note("render_thread_draw16", frameNumber, render_start);
    }
    else if ( outputMode == OutputDebayered16 && !slot.rawImage16.empty() )
    {
        bool usedGpuBilinearDebayer = false;
        bool usedGpuAmazeDebayer = false;
        bool renderedDebayeredFrame = false;
        GpuAmazeDebayerBackendTiming gpuAmazeTiming;
        const bool useGpuAmazeTexturePresent =
            m_activePresentationContext.gpuAmazeTexturePresentRequested;
        if ( useGpuAmazeDebayer && m_pMlvObject )
        {
            const int width = getMlvWidth( m_pMlvObject );
            const int height = getMlvHeight( m_pMlvObject );
            const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            m_gpuAmazeDebayerRawFrame.resize( pixelCount );
            getMlvRawFrameFloat( m_pMlvObject,
                                 frameNumber,
                                 m_gpuAmazeDebayerRawFrame.data() );

            QString gpuReason;
            QString rendererDescription;
            if ( m_pMlvObject->ca_red <= -0.1 || m_pMlvObject->ca_red >= 0.1
              || m_pMlvObject->ca_blue <= -0.1 || m_pMlvObject->ca_blue >= 0.1 )
            {
                gpuReason = QStringLiteral(
                    "GPU AMaZE debayer does not support CA correction yet; falling back to CPU AMaZE");
            }
            else
            {
                wb_convert_info_t wbInfo;
                wb_convert( &wbInfo,
                            m_gpuAmazeDebayerRawFrame.data(),
                            width,
                            height,
                            getMlvBlackLevel( m_pMlvObject ) );
                if ( useGpuAmazeTexturePresent )
                {
                    const GpuAmazeDebayerBackendAvailability availability =
                        gpuAmazeDebayerProbeBackend();
                    slot.stageTimingTelemetry.insert(
                        QStringLiteral("gpu_amaze_texture_present_preflight_available"),
                        availability.available );
                    if ( !availability.rendererDescription.isEmpty() )
                    {
                        slot.stageTimingTelemetry.insert(
                            QStringLiteral("gpu_amaze_texture_present_preflight_renderer"),
                            availability.rendererDescription );
                    }
                    if ( availability.available )
                    {
                        slot.gpuAmazeTextureRawFrame = m_gpuAmazeDebayerRawFrame;
                        slot.gpuAmazeTexturePresentCandidate = true;
                        slot.gpuAmazeTextureWidth = width;
                        slot.gpuAmazeTextureHeight = height;
                        slot.gpuAmazeTextureBlackLevel = getMlvBlackLevel( m_pMlvObject );
                        slot.gpuAmazeTextureWbMultipliers = normalizedWbMultipliers6500();
                        rendererDescription =
                            QStringLiteral("pending CUDA AMaZE post-WB GL texture present");
                        usedGpuAmazeDebayer = true;
                        slot.stageTimingTelemetry.insert(
                            QStringLiteral("gpu_amaze_texture_present_candidate"),
                            true );
                    }
                    else
                    {
                        gpuReason =
                            availability.reason.isEmpty()
                                ? QStringLiteral("GPU AMaZE texture-present backend preflight failed")
                                : QStringLiteral("GPU AMaZE texture-present backend unavailable before GL handoff: %1")
                                      .arg(availability.reason);
                        rendererDescription = availability.rendererDescription;
                        slot.stageTimingTelemetry.insert(
                            QStringLiteral("gpu_amaze_texture_present_candidate"),
                            false );
                    }
                }
                else
                {
                    usedGpuAmazeDebayer =
                        gpuAmazeDebayerApplyGpuOffscreen( m_gpuAmazeDebayerRawFrame.data(),
                                                          slot.rawImage16.data(),
                                                          width,
                                                          height,
                                                          &gpuReason,
                                                          &rendererDescription,
                                                          &gpuAmazeTiming );
                    if ( usedGpuAmazeDebayer )
                    {
                        wb_undo( &wbInfo,
                                 slot.rawImage16.data(),
                                 width,
                                 height,
                                 getMlvBlackLevel( m_pMlvObject ) );
                    }
                }
            }

            if ( usedGpuAmazeDebayer )
            {
                renderedDebayeredFrame = true;
                slot.usedGpuAmazeDebayer = true;
                slot.gpuAmazeFallbackReason.clear();
                slot.gpuAmazeRendererDescription = rendererDescription;
                if ( !m_loggedGpuAmazeSuccess )
                {
                    qInfo() << "GPU AMaZE debayer enabled for the debayered-16 preview path"
                            << "(renderer:"
                            << (rendererDescription.isEmpty() ? QStringLiteral("unknown") : rendererDescription)
                            << ").";
                    m_loggedGpuAmazeSuccess = true;
                }
            }
            else
            {
                slot.usedGpuAmazeDebayer = false;
                slot.gpuAmazeRendererDescription = rendererDescription;
                const QString previousFallbackReason = m_lastGpuAmazeFallbackReason;
                if ( !gpuReason.isEmpty()
                  && gpuReason != previousFallbackReason )
                {
                    qWarning().nospace()
                        << "GPU AMaZE debayer fell back to CPU: "
                        << gpuReason
                        << " (renderer="
                        << (rendererDescription.isEmpty() ? QStringLiteral("unknown") : rendererDescription)
                        << ").";
                }
                slot.gpuAmazeFallbackReason = gpuReason;
            }
        }
        else if ( useGpuBilinearDebayer && m_pMlvObject )
        {
            const int width = getMlvWidth( m_pMlvObject );
            const int height = getMlvHeight( m_pMlvObject );
            const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            m_gpuBilinearDebayerRawFrame.resize( pixelCount );
            getMlvRawFrameFloat( m_pMlvObject,
                                 frameNumber,
                                 m_gpuBilinearDebayerRawFrame.data() );

            QString gpuReason;
            QString rendererDescription;
            usedGpuBilinearDebayer =
                gpuBilinearDebayerApplyGpuOffscreen( m_gpuBilinearDebayerRawFrame.data(),
                                                     slot.rawImage16.data(),
                                                     width,
                                                     height,
                                                     &gpuReason,
                                                     &rendererDescription );
            if ( usedGpuBilinearDebayer )
            {
                renderedDebayeredFrame = true;
                slot.usedGpuBilinearDebayer = true;
                slot.gpuBilinearFallbackReason.clear();
                slot.gpuBilinearRendererDescription = rendererDescription;
                if ( !m_loggedGpuBilinearSuccess )
                {
                    qInfo() << "Experimental GPU bilinear debayer enabled for the debayered-16 preview path"
                            << "(renderer:"
                            << (rendererDescription.isEmpty() ? QStringLiteral("unknown") : rendererDescription)
                            << ").";
                    m_loggedGpuBilinearSuccess = true;
                }
            }
            else
            {
                debayerBasic( slot.rawImage16.data(),
                              m_gpuBilinearDebayerRawFrame.data(),
                              width,
                              height,
                              1 );
                renderedDebayeredFrame = true;
                slot.usedGpuBilinearDebayer = false;
                slot.gpuBilinearRendererDescription = rendererDescription;
                const QString previousFallbackReason = m_lastGpuBilinearFallbackReason;
                if ( !gpuReason.isEmpty()
                  && gpuReason != previousFallbackReason )
                {
                    qWarning().nospace()
                        << "Experimental GPU bilinear debayer fell back to CPU: "
                        << gpuReason
                        << " (renderer="
                        << (rendererDescription.isEmpty() ? QStringLiteral("unknown") : rendererDescription)
                        << ").";
                }
                slot.gpuBilinearFallbackReason = gpuReason;
            }
        }

        if ( !renderedDebayeredFrame )
        {
            getMlvRawFrameDebayered( m_pMlvObject, frameNumber, slot.rawImage16.data() );
        }
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_bilinear_debayer_active"),
            usedGpuBilinearDebayer );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_amaze_debayer_active"),
            usedGpuAmazeDebayer );
        if ( !slot.gpuAmazeFallbackReason.isEmpty() )
        {
            slot.stageTimingTelemetry.insert(
                QStringLiteral("gpu_amaze_debayer_fallback_reason"),
                slot.gpuAmazeFallbackReason );
        }
        if ( !slot.gpuAmazeRendererDescription.isEmpty() )
        {
            slot.stageTimingTelemetry.insert(
                QStringLiteral("gpu_amaze_debayer_renderer"),
                slot.gpuAmazeRendererDescription );
        }
        if ( usedGpuAmazeDebayer && gpuAmazeTiming.available )
        {
            slot.stageTimingTelemetry.insert(
                QStringLiteral("gpu_amaze_debayer_upload_ms"),
                gpuAmazeTiming.uploadMs );
            slot.stageTimingTelemetry.insert(
                QStringLiteral("gpu_amaze_debayer_kernel_ms"),
                gpuAmazeTiming.kernelMs );
            slot.stageTimingTelemetry.insert(
                QStringLiteral("gpu_amaze_debayer_download_ms"),
                gpuAmazeTiming.downloadMs );
            slot.stageTimingTelemetry.insert(
                QStringLiteral("gpu_amaze_debayer_total_ms"),
                gpuAmazeTiming.totalMs );
        }
        mlv_stage_timing_note("render_thread_draw16_debayered", frameNumber, render_start);
    }
    else if( !slot.rawImage8.empty() )
    {
        bool renderedFromPhase3Raw = false;
        bool renderedFromGpuTextureNoReadbackStatePath = false;
        const bool allowGpuTextureNoReadbackScale1StatePath =
            decodedRawFrameAlreadyReconned
            && m_activePresentationContext.gpuPlaybackReconTexturePresentRequested
            && m_activePresentationContext.gpuPlaybackReconAmazeTexturePresentAdmitted
            && playbackScaleFactor == 1
            && slot.gpuPlaybackReconTextureNoReadbackCandidate
            && slot.gpuPlaybackReconTextureState.valid;
        if( decodedRawFrame )
        {
            if( decodedRawFrameAlreadyReconned )
            {
                renderedFromPhase3Raw =
                    getMlvProcessedFrame8ScaledFromReconnedRaw16(
                        m_pMlvObject,
                        frameNumber,
                        decodedRawFrame,
                        slot.rawImage8.data(),
                        workerThreads,
                        playbackScaleFactor,
                        allowGpuTextureNoReadbackScale1StatePath ? 1 : 0 ) != 0;
                renderedFromGpuTextureNoReadbackStatePath =
                    renderedFromPhase3Raw
                    && allowGpuTextureNoReadbackScale1StatePath;
            }
            else
            {
                renderedFromPhase3Raw =
                    getMlvProcessedFrame8ScaledFromRaw16( m_pMlvObject,
                                                          frameNumber,
                                                          decodedRawFrame,
                                                          slot.rawImage8.data(),
                                                          workerThreads,
                                                          playbackScaleFactor ) != 0;
            }
        }
        if( !renderedFromPhase3Raw )
        {
            getMlvProcessedFrame8Scaled( m_pMlvObject,
                                         frameNumber,
                                         slot.rawImage8.data(),
                                         workerThreads,
                                         playbackScaleFactor );
        }
        slot.stageTimingTelemetry.insert(
            QStringLiteral("phase3_decoded_raw_consumed"),
            decodedRawFrame && !decodedRawFrameAlreadyReconned && renderedFromPhase3Raw );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("phase3_reconned_raw_consumed"),
            decodedRawFrame && decodedRawFrameAlreadyReconned && renderedFromPhase3Raw );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_x1_state_debayer_allowed"),
            allowGpuTextureNoReadbackScale1StatePath );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_x1_state_debayer_used"),
            renderedFromGpuTextureNoReadbackStatePath );
        slot.dualIsoPreviewHistogramMs = llrpGetLastDualIsoPreviewHistogramMilliseconds();
        slot.dualIsoPreviewRegressionMs = llrpGetLastDualIsoPreviewRegressionMilliseconds();
        slot.dualIsoPreviewRowscaleMs = llrpGetLastDualIsoPreviewRowscaleMilliseconds();
        mlv_stage_timing_note("render_thread_draw", frameNumber, render_start);
    }

    {
        GpuPreviewProcessingConfig & previewConfig =
            slot.presentationContext.gpuPreviewProcessingConfig;
        const bool shadowsHighlightsFrameStateRequested =
            gpuPreviewProcessingNeedsShadowsHighlightsFrameState(previewConfig);
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_preview_processing_shadows_highlights_requested"),
            shadowsHighlightsFrameStateRequested );
        if ( shadowsHighlightsFrameStateRequested
          && gpuTexNrDisplayLutOnlyShStateBypass )
        {
            slot.stageTimingTelemetry.insert(
                QStringLiteral("gpu_preview_processing_shadows_highlights_frame_state_bypassed_for_display_lut_only"),
                true );
        }
        else if ( shadowsHighlightsFrameStateRequested )
        {
            QString shadowsHighlightsFrameStateReason;
            const bool shadowsHighlightsFrameStateReady =
                gpuPreviewProcessingAttachFrameState(
                    &previewConfig,
                    m_pMlvObject ? m_pMlvObject->processing : nullptr,
                    renderedImageWidth,
                    renderedImageHeight,
                    &shadowsHighlightsFrameStateReason);
            slot.stageTimingTelemetry.insert(
                QStringLiteral("gpu_preview_processing_shadows_highlights_frame_state_ready"),
                shadowsHighlightsFrameStateReady );
            if ( !shadowsHighlightsFrameStateReason.isEmpty() )
            {
                slot.stageTimingTelemetry.insert(
                    QStringLiteral("gpu_preview_processing_shadows_highlights_frame_state_reason"),
                    shadowsHighlightsFrameStateReason );
            }
            slot.presentationContext.gpuPresentationOptions.previewProcessing =
                previewConfig;
        }
    }

    insertGpuPlaybackReconRunTelemetry( slot.stageTimingTelemetry );
    {
        const size_t fullResPixelCount =
            static_cast<size_t>( qMax( 0, m_imageWidth ) )
            * static_cast<size_t>( qMax( 0, m_imageHeight ) );
        const bool readbackBayerCandidate =
            m_activePresentationContext.gpuPlaybackReconTexturePresentRequested
            && decodedRawFrameAlreadyReconned
            && playbackScaleFactor == 1
            && m_imageWidth > 0
            && m_imageHeight > 0
            && slot.rawImage16.size() >= fullResPixelCount;
        /* The no-readback GL R16 texture is the RECON-ONLY Dual ISO bayer; the
         * CUDA backend has no focus/bad-pixel code. Eligibility is decided
         * AUTHORITATIVELY in the C worker on an EFFECTIVENESS basis: it stores the
         * no-readback input bayer, then RETRACTS it if the post-recon focus/bad-pixel
         * interpolation actually mutated the recon output (llrawproc.c ~2553). When
         * retracted, slot.gpuPlaybackReconTextureInputBayerFrame is empty below, so
         * noReadbackCandidate becomes false and we fall back to the readback bayer
         * (slot.rawImage16, which DOES include the fixes -> display stays correct).
         * postReconRawFixActive here is a DIAGNOSTIC of the mode flags only
         * (bad_pixels defaults to 1); it is NOT the gate, because a mode that is On
         * but fixes no pixels is still no-readback-eligible. */
        const bool postReconRawFixActive =
            m_pMlvObject
            && m_pMlvObject->llrawproc
            && ( m_pMlvObject->llrawproc->focus_pixels != 0
              || m_pMlvObject->llrawproc->bad_pixels != 0 );
        const bool outputValidationRequired =
            gpuPlaybackReconNoReadbackOutputValidationEnabled();
        const bool outputOracleAvailable =
            !outputValidationRequired
            || slot.gpuPlaybackReconTextureBayerFrame.size() >= fullResPixelCount;
        const bool noReadbackCandidate =
            m_activePresentationContext.gpuPlaybackReconTexturePresentRequested
            && playbackScaleFactor == 1
            && m_imageWidth > 0
            && m_imageHeight > 0
            && slot.gpuPlaybackReconTextureNoReadbackCandidate
            && ( slot.gpuPlaybackReconTextureInputBorrowedFromRawImage16
                 ? slot.rawImage16.size()
                 : slot.gpuPlaybackReconTextureInputBayerFrame.size() ) >= fullResPixelCount
            && outputOracleAvailable
            && slot.gpuPlaybackReconTextureState.valid
            && slot.gpuPlaybackReconTextureState.width == m_imageWidth
            && slot.gpuPlaybackReconTextureState.height == m_imageHeight;
        slot.gpuPlaybackReconTextureNoReadbackCandidate = noReadbackCandidate;
        slot.gpuPlaybackReconTexturePresentCandidate =
            readbackBayerCandidate || noReadbackCandidate;
        slot.gpuPlaybackReconTextureWidth =
            slot.gpuPlaybackReconTexturePresentCandidate ? m_imageWidth : 0;
        slot.gpuPlaybackReconTextureHeight =
            slot.gpuPlaybackReconTexturePresentCandidate ? m_imageHeight : 0;
        slot.gpuPlaybackReconTextureBlackLevel =
            ( slot.gpuPlaybackReconTexturePresentCandidate && m_pMlvObject )
                ? getMlvBlackLevel( m_pMlvObject )
                : 0;
        slot.gpuPlaybackReconTextureWbMultipliers =
            slot.gpuPlaybackReconTexturePresentCandidate
                ? normalizedWbMultipliers6500()
                : std::array<double, 3>{{1.0, 1.0, 1.0}};
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_readback_bayer_candidate"),
            readbackBayerCandidate );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_no_readback_candidate"),
            noReadbackCandidate );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_no_readback_oracle_required"),
            outputValidationRequired );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_no_readback_oracle_words"),
            static_cast<qint64>( slot.gpuPlaybackReconTextureBayerFrame.size() ) );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("gpu_playback_recon_texture_present_post_recon_raw_fix_active"),
            postReconRawFixActive );
        if( slot.gpuPlaybackReconTexturePresentCandidate )
        {
            slot.stageTimingTelemetry.insert(
                QStringLiteral("gpu_playback_recon_texture_present_source"),
                noReadbackCandidate
                    && slot.gpuPlaybackReconTextureRetainedDeviceToken != 0
                    ? QStringLiteral("cuda_retained_device_bayer16_pending")
                : noReadbackCandidate
                    ? QStringLiteral("cuda_gl_r16_pending")
                    : QStringLiteral("cpu16_reconstructed_bayer") );
        }
    }

    int playbackScaleFactorActive = playbackScaleFactor;
    if( m_pMlvObject
     && ( outputMode == OutputProcessed8 || outputMode == OutputProcessed16 ) )
    {
        const int coreActiveScale = m_pMlvObject->playback_scale_factor_active;
        if( coreActiveScale == 1 || coreActiveScale == 2 || coreActiveScale == 4 || coreActiveScale == 8 )
        {
            playbackScaleFactorActive = coreActiveScale;
        }
    }
    slot.playbackScaleFactorActive = playbackScaleFactorActive;
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_playback_scale_factor_effective"),
        playbackScaleFactorActive );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_playback_scale_factor_clamped"),
        playbackScaleFactorActive != playbackScaleFactor );
    const bool processed8CacheHit = getMlvLastProcessed8CacheHit() != 0;
    const bool processed8PrefetchHit = getMlvLastProcessed8PrefetchHit() != 0;
    const int processed8CacheHitScale = getMlvLastProcessed8CacheHitScaleFactor();
    /* Phase A1 (image-pipeline-hardening): read the genuine render-path tag
     * UNCONDITIONALLY. The previous scale<=1 ternary forced this to 0, hiding
     * the x1 half/quarter proxy paths (codes 5/6/7/9/10/11, set at scale=1 in
     * video_mlv.c:4385/4419/4266/4237) behind a value identical to a true
     * full-res frame -- the root cause of repeated path mis-diagnoses. The tag
     * is freshly set on this render thread on every render (reset video_mlv.c:6021,
     * set by the path that runs); the cache-hit case replays the cached tag
     * (video_mlv.c:6528) and is disambiguated by render_thread_phase4b_path_source. */
    const int phase4bPath = mlv_phase4bv2_last_path_taken();
    const int phase4bCropRows = mlv_phase4bv3_last_y_crop_rows();
    const int sourceWidth = qMax( 0, m_imageWidth );
    const int sourceHeight = qMax( 0, m_imageHeight );
    const int renderedWidth = qMax( 0, renderedImageWidth );
    const int renderedHeight = qMax( 0, renderedImageHeight );
    const qint64 sourcePixels = stagePixelCount( sourceWidth, sourceHeight );
    const qint64 renderedPixels = stagePixelCount( renderedWidth, renderedHeight );
    /* Phase A3 (image-pipeline-hardening): emit the SOURCE dims too. Only the
     * rendered dims were exported (render_thread_rendered_width/height at 2748),
     * so a consumer could not tell whether the rendered frame was a reduction
     * of the source -- the exact discriminator the period-4 hunt needed. The
     * render_manifest line (MainWindow) joins src vs rendered to mark reduced
     * frames, and the A6 runtime assert keys off rendered<source => tag!=0. */
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_source_width"),
        sourceWidth );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_source_height"),
        sourceHeight );
    const double llrawprocPreDualIsoFixMs =
        ( m_pMlvObject && m_pMlvObject->llrawproc )
            ? m_pMlvObject->llrawproc->playback_pre_dualiso_fix_ms
            : 0.0;
    const bool preDualIsoFixActive = llrawprocPreDualIsoFixMs > 0.0;
    const int preDualIsoFixWidth = preDualIsoFixActive ? sourceWidth : 0;
    const int preDualIsoFixHeight = preDualIsoFixActive ? sourceHeight : 0;
    QString phase4bFallbackReason = QStringLiteral("none");
    const bool aggressivePreview = ( mlvPlaybackAggressivePreviewMode() != 0 );
    const bool phase4bReducedPath =
        phase4bPath == 8 || phase4bPath == 4 || phase4bPath == 3 || phase4bPath == 2;
    bool skipFocusPixels = false;
    bool skipBadPixels = false;
    bool skipVerticalStripes = false;
    bool skipPatternNoise = false;
    if( m_pMlvObject && m_pMlvObject->llrawproc )
    {
        skipFocusPixels = m_pMlvObject->llrawproc->focus_pixels != 0;
        skipBadPixels = m_pMlvObject->llrawproc->bad_pixels != 0;
        skipVerticalStripes = m_pMlvObject->llrawproc->vertical_stripes != 0;
        skipPatternNoise = m_pMlvObject->llrawproc->pattern_noise != 0;
    }
    const bool skippedScaledRawCoordinateFixes =
        aggressivePreview
        && phase4bReducedPath
        && ( skipFocusPixels
          || skipBadPixels
          || skipVerticalStripes
          || skipPatternNoise );
    /* Phase A1: surface the fallback reason whenever the path fell to
     * full-recon/none (code 0) at ANY scale, not only scale>1 -- a scale<=1
     * fallback was previously silent. Reason defaults to "none" when no
     * fallback occurred. */
    if( phase4bPath == 0 )
    {
        const char *reason = mlv_phase4bv2_last_fallback_reason();
        phase4bFallbackReason =
            reason && *reason
                ? QString::fromUtf8( reason )
                : QStringLiteral("unknown");
    }
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_phase4b_path"),
        phase4bPath );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_phase4b_path_label"),
        phase4bPathLabel( phase4bPath ) );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_phase4b_path_source"),
        processed8CacheHit
            ? QStringLiteral("processed8_cache")
            : QStringLiteral("render_thread") );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_phase4b_y_crop_rows"),
        phase4bCropRows );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_phase4b_fallback_reason"),
        phase4bFallbackReason );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_scaled_raw_coordinate_fixes_skipped"),
        skippedScaledRawCoordinateFixes );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_scaled_skip_focus_pixels"),
        skippedScaledRawCoordinateFixes && skipFocusPixels );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_scaled_skip_bad_pixels"),
        skippedScaledRawCoordinateFixes && skipBadPixels );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_scaled_skip_vertical_stripes"),
        skippedScaledRawCoordinateFixes && skipVerticalStripes );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_scaled_skip_pattern_noise"),
        skippedScaledRawCoordinateFixes && skipPatternNoise );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_preview_mode"),
        aggressivePreview
            ? QStringLiteral("aggressive_performance")
            : QStringLiteral("sharp_smooth") );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_aggressive_preview"),
        aggressivePreview );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_playback_scale_sensor_pixels"),
        sourcePixels );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_playback_scale_output_pixels"),
        renderedPixels );
    slot.stageTimingTelemetry.insert(
        QStringLiteral("render_thread_playback_scale_pixel_retention_ratio"),
        (sourcePixels > 0)
            ? static_cast<double>( renderedPixels ) / static_cast<double>( sourcePixels )
            : 0.0 );

    const bool processedOutputMode =
        outputMode == OutputProcessed8 || outputMode == OutputProcessed16;
    int bayerReductionInputWidth = 0;
    int bayerReductionInputHeight = 0;
    int bayerReductionOutputWidth = 0;
    int bayerReductionOutputHeight = 0;
    int llrawprocWidth = processedOutputMode ? sourceWidth : 0;
    int llrawprocHeight = processedOutputMode ? sourceHeight : 0;
    int rgbStageInputWidth = processedOutputMode ? sourceWidth : 0;
    int rgbStageInputHeight = processedOutputMode ? sourceHeight : 0;
    const int rgbStageOutputWidth = processedOutputMode ? renderedWidth : 0;
    const int rgbStageOutputHeight = processedOutputMode ? renderedHeight : 0;
    const int processingWidth = processedOutputMode ? renderedWidth : 0;
    const int processingHeight = processedOutputMode ? renderedHeight : 0;

    if( processedOutputMode )
    {
        if( phase4bPath == 8 )
        {
            const int effectiveHeight = qMax( 0, sourceHeight - phase4bCropRows );
            bayerReductionInputWidth = sourceWidth;
            bayerReductionInputHeight = effectiveHeight;
            bayerReductionOutputWidth = sourceWidth / 8;
            bayerReductionOutputHeight = effectiveHeight / 8;
        }
        else if( phase4bPath == 4 )
        {
            const int effectiveHeight = qMax( 0, sourceHeight - phase4bCropRows );
            bayerReductionInputWidth = sourceWidth;
            bayerReductionInputHeight = effectiveHeight;
            bayerReductionOutputWidth = sourceWidth / 2;
            bayerReductionOutputHeight = effectiveHeight / 2;
        }
        else if( phase4bPath == 3 )
        {
            const int effectiveHeight = qMax( 0, sourceHeight - phase4bCropRows );
            bayerReductionInputWidth = sourceWidth;
            bayerReductionInputHeight = effectiveHeight;
            bayerReductionOutputWidth = sourceWidth / 4;
            bayerReductionOutputHeight = effectiveHeight / 4;
        }
        else if( phase4bPath == 2 )
        {
            bayerReductionInputWidth = sourceWidth;
            bayerReductionInputHeight = sourceHeight;
            bayerReductionOutputWidth = sourceWidth / 4;
            bayerReductionOutputHeight = sourceHeight;
        }

        if( phase4bPath == 8 || phase4bPath == 4 || phase4bPath == 3 || phase4bPath == 2 )
        {
            llrawprocWidth = bayerReductionOutputWidth;
            llrawprocHeight = bayerReductionOutputHeight;
            rgbStageInputWidth = bayerReductionOutputWidth;
            rgbStageInputHeight = bayerReductionOutputHeight;
        }
    }

    insertStageResolutionTelemetry(
        slot.stageTimingTelemetry,
        QStringLiteral("render_thread_stage_raw_decode"),
        QStringLiteral("raw_bayer"),
        processedOutputMode ? sourceWidth : 0,
        processedOutputMode ? sourceHeight : 0,
        sourcePixels );
    insertStageResolutionTelemetry(
        slot.stageTimingTelemetry,
        QStringLiteral("render_thread_stage_pre_dualiso_fix"),
        QStringLiteral("raw_bayer"),
        preDualIsoFixWidth,
        preDualIsoFixHeight,
        sourcePixels );
    insertStageResolutionTelemetry(
        slot.stageTimingTelemetry,
        QStringLiteral("render_thread_stage_bayer_reduction_input"),
        QStringLiteral("raw_bayer"),
        bayerReductionInputWidth,
        bayerReductionInputHeight,
        sourcePixels );
    insertStageResolutionTelemetry(
        slot.stageTimingTelemetry,
        QStringLiteral("render_thread_stage_bayer_reduction_output"),
        QStringLiteral("raw_bayer"),
        bayerReductionOutputWidth,
        bayerReductionOutputHeight,
        sourcePixels );
    insertStageResolutionTelemetry(
        slot.stageTimingTelemetry,
        QStringLiteral("render_thread_stage_llrawproc"),
        QStringLiteral("reconstructed_bayer"),
        llrawprocWidth,
        llrawprocHeight,
        sourcePixels );
    insertStageResolutionTelemetry(
        slot.stageTimingTelemetry,
        QStringLiteral("render_thread_stage_rgb_input"),
        QStringLiteral("reconstructed_bayer"),
        rgbStageInputWidth,
        rgbStageInputHeight,
        sourcePixels );
    insertStageResolutionTelemetry(
        slot.stageTimingTelemetry,
        QStringLiteral("render_thread_stage_rgb_output"),
        QStringLiteral("rgb"),
        rgbStageOutputWidth,
        rgbStageOutputHeight,
        sourcePixels );
    insertStageResolutionTelemetry(
        slot.stageTimingTelemetry,
        QStringLiteral("render_thread_stage_processing"),
        QStringLiteral("processed_rgb"),
        processingWidth,
        processingHeight,
        sourcePixels );
    insertStageResolutionTelemetry(
        slot.stageTimingTelemetry,
        QStringLiteral("render_thread_stage_presentation_input"),
        QStringLiteral("processed_rgb"),
        renderedWidth,
        renderedHeight,
        sourcePixels );
    if( m_lastLoggedPlaybackScaleFactorRequest != playbackScaleFactor
     || m_lastLoggedPlaybackScaleFactorActive != playbackScaleFactorActive )
    {
        qInfo().nospace()
            << "Playback scale effective: requested=x"
            << playbackScaleFactor
            << " active=x"
            << playbackScaleFactorActive
            << " rendered="
            << renderedImageWidth
            << "x"
            << renderedImageHeight
            << " target="
            << m_activePresentationPreparationOptions.targetWidth
            << "x"
            << m_activePresentationPreparationOptions.targetHeight
            << " clamped="
            << (playbackScaleFactorActive != playbackScaleFactor)
            << " phase4b_path="
            << phase4bPath
            << " outputMode="
            << static_cast<int>( outputMode );
        m_lastLoggedPlaybackScaleFactorRequest = playbackScaleFactor;
        m_lastLoggedPlaybackScaleFactorActive = playbackScaleFactorActive;
    }

    slot.playbackFastScaleActive = false;
    slot.playbackScaledWidth = 0;
    slot.playbackScaledHeight = 0;
    slot.playbackScaledBytesPerLine = 0;
    if( outputMode == OutputProcessed8
     && m_activePresentationPreparationOptions.fastPlaybackScale
     && !slot.rawImage8.empty()
     && m_activePresentationPreparationOptions.targetWidth > 0
     && m_activePresentationPreparationOptions.targetHeight > 0 )
    {
        const double playbackScaleStart = mlv_stage_timing_now();

        /* Phase 4D: when the render path returns a downsampled buffer
         * (Phase 4B's scale=2/4/8 path), nearest-neighbour presentation makes
         * the lower-res preview look blocky and aliased. Use bilinear for any
         * playback-scaled buffer that needs a final presentation resize; keep
         * nearest only for the scaleFactor==1 path so full-res playback stays
         * byte-identical. */
        const int sourceWidthForScaler = renderedImageWidth;
        const int sourceHeightForScaler = renderedImageHeight;
        const bool upscaling =
            playbackScaleFactorActive > 1
         && sourceWidthForScaler < m_activePresentationPreparationOptions.targetWidth
         && sourceHeightForScaler < m_activePresentationPreparationOptions.targetHeight;
        const bool presentationResize =
            sourceWidthForScaler != m_activePresentationPreparationOptions.targetWidth
         || sourceHeightForScaler != m_activePresentationPreparationOptions.targetHeight;
        const PlaybackPresentationScaleResampler presentationScaleResampler =
            playbackChoosePresentationScaleResampler( playbackScaleFactorActive,
                                                      phase4bPath,
                                                      upscaling,
                                                      presentationResize,
                                                      aggressivePreview );
        const double stretchX =
            (sourceWidthForScaler > 0)
                ? static_cast<double>( m_activePresentationPreparationOptions.targetWidth )
                  / static_cast<double>( sourceWidthForScaler )
                : 0.0;
        const double stretchY =
            (sourceHeightForScaler > 0)
                ? static_cast<double>( m_activePresentationPreparationOptions.targetHeight )
                  / static_cast<double>( sourceHeightForScaler )
                : 0.0;
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_playback_scale_target_width"),
            m_activePresentationPreparationOptions.targetWidth );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_playback_scale_target_height"),
            m_activePresentationPreparationOptions.targetHeight );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_playback_scale_upscaling"),
            upscaling );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_playback_scale_source_to_target_x"),
            stretchX );
        slot.stageTimingTelemetry.insert(
            QStringLiteral("render_thread_playback_scale_source_to_target_y"),
            stretchY );
        bool bilinearUsed = false;
        bool cubicUsed = false;
        const int targetBytesPerLine =
            ((m_activePresentationPreparationOptions.targetWidth * 3) + 3) & ~3;

        if( presentationScaleResampler == PlaybackPresentationScaleResampler::Cubic )
        {
            slot.playbackFastScaleActive =
                playbackBuildCubicScaledRgb8( slot.rawImage8.data(),
                                              sourceWidthForScaler,
                                              sourceHeightForScaler,
                                              m_activePresentationPreparationOptions.targetWidth,
                                              m_activePresentationPreparationOptions.targetHeight,
                                              slot.playbackScaledImage8,
                                              m_playbackCubicScaleCache,
                                              targetBytesPerLine );
            cubicUsed = slot.playbackFastScaleActive;
        }
        if( !slot.playbackFastScaleActive
         && presentationScaleResampler == PlaybackPresentationScaleResampler::Bilinear )
        {
            slot.playbackFastScaleActive =
                playbackBuildBilinearScaledRgb8( slot.rawImage8.data(),
                                                 sourceWidthForScaler,
                                                 sourceHeightForScaler,
                                                 m_activePresentationPreparationOptions.targetWidth,
                                                 m_activePresentationPreparationOptions.targetHeight,
                                                 slot.playbackScaledImage8,
                                                 m_playbackBilinearScaleCache,
                                                 targetBytesPerLine );
            bilinearUsed = slot.playbackFastScaleActive;
        }
        if( !slot.playbackFastScaleActive )
        {
            slot.playbackFastScaleActive =
                playbackBuildFastScaledRgb8( slot.rawImage8.data(),
                                             sourceWidthForScaler,
                                             sourceHeightForScaler,
                                             m_activePresentationPreparationOptions.targetWidth,
                                             m_activePresentationPreparationOptions.targetHeight,
                                             slot.playbackScaledImage8,
                                             m_playbackScaleCache,
                                             targetBytesPerLine );
        }
        if( slot.playbackFastScaleActive )
        {
            slot.playbackScaledWidth = m_activePresentationPreparationOptions.targetWidth;
            slot.playbackScaledHeight = m_activePresentationPreparationOptions.targetHeight;
            slot.playbackScaledBytesPerLine = targetBytesPerLine;
        }
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_active"),
                                          slot.playbackFastScaleActive );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_bilinear"),
                                          bilinearUsed );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_cubic"),
                                          cubicUsed );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_resampler"),
                                          cubicUsed
                                              ? QStringLiteral("cubic")
                                              : (bilinearUsed
                                                     ? QStringLiteral("bilinear")
                                                     : (slot.playbackFastScaleActive
                                                            ? QStringLiteral("nearest")
                                                            : QStringLiteral("none"))) );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_ms"),
                                          (mlv_stage_timing_now() - playbackScaleStart) * 1000.0 );
    }
    else
    {
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_target_width"),
                                          0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_target_height"),
                                          0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_upscaling"),
                                          false );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_source_to_target_x"),
                                          0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_source_to_target_y"),
                                          0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_active"),
                                          false );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_bilinear"),
                                          false );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_cubic"),
                                          false );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_resampler"),
                                          QStringLiteral("none") );
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_playback_scale_ms"),
                                          0.0 );
    }

    const double rawUint16Ms = getMlvLastRawUint16Milliseconds();
    const double rawUint16DiskReadMs = getMlvLastRawUint16DiskReadMilliseconds();
    const double rawUint16DecompressMs = getMlvLastRawUint16DecompressMilliseconds();
    const double rawUint16DecompressPrepareMs =
        getMlvLastRawUint16DecompressPrepareMilliseconds();
    const double rawUint16DecompressExecuteMs =
        getMlvLastRawUint16DecompressExecuteMilliseconds();
    const int rawUint16Lj92Pred6SplitActive =
        getMlvLastRawUint16Lj92Pred6SplitActive();
    const int rawUint16Lj92Pred6SplitRequested =
        getMlvLastRawUint16Lj92Pred6SplitRequested();
    const int rawUint16Lj92GenericSplitActive =
        getMlvLastRawUint16Lj92GenericSplitActive();
    const int rawUint16Lj92GenericSplitRequested =
        getMlvLastRawUint16Lj92GenericSplitRequested();
    const int rawUint16Lj92Pred1FastPathActive =
        getMlvLastRawUint16Lj92Pred1FastPathActive();
    const int rawUint16Lj92Pred1FastPathMeasurementRequested =
        getMlvLastRawUint16Lj92Pred1FastPathMeasurementRequested();
    const int rawUint16Lj92Pred1FastPathMeasurementActive =
        getMlvLastRawUint16Lj92Pred1FastPathMeasurementActive();
    const int rawUint16Lj92Pred1FastPathEligible =
        getMlvLastRawUint16Lj92Pred1FastPathEligible();
    const int rawUint16Lj92ScanComponentCount =
        getMlvLastRawUint16Lj92ScanComponentCount();
    const int rawUint16Lj92WriteLength =
        getMlvLastRawUint16Lj92WriteLength();
    const int rawUint16Lj92ExpectedWriteLength =
        getMlvLastRawUint16Lj92ExpectedWriteLength();
    const int rawUint16Lj92SkipLength =
        getMlvLastRawUint16Lj92SkipLength();
    const int rawUint16Lj92LinearizeActive =
        getMlvLastRawUint16Lj92LinearizeActive();
    const int rawUint16Lj92ComponentCount =
        getMlvLastRawUint16Lj92ComponentCount();
    const int rawUint16Lj92Predictor =
        getMlvLastRawUint16Lj92Predictor();
    const double rawUint16Lj92Pred6TotalMs =
        getMlvLastRawUint16Lj92Pred6TotalMilliseconds();
    const double rawUint16Lj92Pred6BitstreamMs =
        getMlvLastRawUint16Lj92Pred6BitstreamMilliseconds();
    const double rawUint16Lj92Pred6PredictorMs =
        getMlvLastRawUint16Lj92Pred6PredictorMilliseconds();
    const double rawUint16Lj92GenericTotalMs =
        getMlvLastRawUint16Lj92GenericTotalMilliseconds();
    const double rawUint16Lj92GenericBitstreamMs =
        getMlvLastRawUint16Lj92GenericBitstreamMilliseconds();
    const double rawUint16Lj92GenericPredictorMs =
        getMlvLastRawUint16Lj92GenericPredictorMilliseconds();
    const double rawUint16Lj92Pred1FastPathTotalMs =
        getMlvLastRawUint16Lj92Pred1FastPathTotalMilliseconds();
    const double rawUint16Lj92Pred1FastPathBitstreamMs =
        getMlvLastRawUint16Lj92Pred1FastPathBitstreamMilliseconds();
    const double rawUint16Lj92Pred1FastPathPredictorMs =
        getMlvLastRawUint16Lj92Pred1FastPathPredictorMilliseconds();
    const double rawUint16UnpackMs = getMlvLastRawUint16UnpackMilliseconds();
    const double rawUint16CopyMs = getMlvLastRawUint16CopyMilliseconds();
    const int rawUint16PrefetchHit = getMlvLastRawUint16PrefetchHit();
    const quint64 rawUint16PrefetchDecodeFailures =
        static_cast<quint64>( getMlvRawUint16PrefetchDecodeFailures( m_pMlvObject ) );
    const double llrawprocMs = getMlvLastLlrawprocMilliseconds();
    const double llrawprocDarkFrameMs = llrpGetLastDarkFrameMilliseconds();
    const double llrawprocVerticalStripesMs = llrpGetLastVerticalStripesMilliseconds();
    const double llrawprocFocusPixelsMs = llrpGetLastFocusPixelsMilliseconds();
    const double llrawprocBadPixelsMs = llrpGetLastBadPixelsMilliseconds();
    const double llrawprocPatternNoiseMs = llrpGetLastPatternNoiseMilliseconds();
    const double llrawprocDualIsoMs = llrpGetLastDualIsoMilliseconds();
    dualiso_full20bit_timing_t dualIsoFull20 = {};
    llrpGetLastDualIsoFull20bitTiming( &dualIsoFull20 );
    const double llrawprocChromaSmoothMs = llrpGetLastChromaSmoothMilliseconds();
    const double llrawprocKnownMs =
        llrawprocDarkFrameMs +
        llrawprocVerticalStripesMs +
        llrawprocFocusPixelsMs +
        llrawprocBadPixelsMs +
        llrawprocPatternNoiseMs +
        llrawprocDualIsoMs +
        llrawprocChromaSmoothMs;
    const double llrawprocOtherMs = qMax( 0.0, llrawprocMs - llrawprocKnownMs );
    const double dualIsoPreviewTotalMs =
        slot.dualIsoPreviewHistogramMs +
        slot.dualIsoPreviewRegressionMs +
        slot.dualIsoPreviewRowscaleMs;
    const double rawFloatConvertMs = getMlvLastRawFloatConvertMilliseconds();
    const double debayerWbPrepareMs = getMlvLastDebayerWbPrepareMilliseconds();
    const double debayerCaMs = getMlvLastDebayerCaMilliseconds();
    const double debayerKernelMs = getMlvLastDebayerKernelMilliseconds();
    const double debayerWbUndoMs = getMlvLastDebayerWbUndoMilliseconds();
    const double debayerExclusiveMs = qMax( 0.0,
                                            getMlvLastDebayeredFrameMilliseconds()
                                                - rawUint16Ms
                                                - llrawprocMs );
    const double debayerKnownMs =
        rawFloatConvertMs +
        debayerWbPrepareMs +
        debayerCaMs +
        debayerKernelMs +
        debayerWbUndoMs;
    const double debayerPipelineOtherMs =
        qMax( 0.0, debayerExclusiveMs - debayerKnownMs );
    const double rawUint16KnownMs =
        rawUint16DiskReadMs +
        rawUint16DecompressMs +
        rawUint16UnpackMs +
        rawUint16CopyMs;
    const double rawUint16OtherMs =
        qMax( 0.0, rawUint16Ms - rawUint16KnownMs );
    const double rawUint16Lj92Pred6OtherMs =
        qMax( 0.0,
              rawUint16Lj92Pred6TotalMs
                - rawUint16Lj92Pred6BitstreamMs
                - rawUint16Lj92Pred6PredictorMs );
    const double rawUint16Lj92GenericOtherMs =
        qMax( 0.0,
              rawUint16Lj92GenericTotalMs
                - rawUint16Lj92GenericBitstreamMs
                - rawUint16Lj92GenericPredictorMs );
    const double rawUint16Lj92Pred1FastPathOtherMs =
        qMax( 0.0,
              rawUint16Lj92Pred1FastPathTotalMs
                - rawUint16Lj92Pred1FastPathBitstreamMs
                - rawUint16Lj92Pred1FastPathPredictorMs );
    const double processingMs = getMlvLastProcessingMilliseconds();
    const double processingSetupMs = processingGetLastSetupMilliseconds();
    const double processingShadowsHighlightsPrepMs =
        processingGetLastShadowsHighlightsPrepMilliseconds();
    const double processingShadowsHighlightsResizeMs =
        processingGetLastShadowsHighlightsResizeMilliseconds();
    const double processingShadowsHighlightsCopyMs =
        processingGetLastShadowsHighlightsCopyMilliseconds();
    const double processingShadowsHighlightsFilterMs =
        processingGetLastShadowsHighlightsFilterMilliseconds();
    const double processingShadowsHighlightsFilterFullresMs =
        processingGetLastShadowsHighlightsFilterFullresMilliseconds();
    const double processingShadowsHighlightsFilterHalfresDownsampleMs =
        processingGetLastShadowsHighlightsFilterHalfresDownsampleMilliseconds();
    const double processingShadowsHighlightsFilterHalfresRbfMs =
        processingGetLastShadowsHighlightsFilterHalfresRbfMilliseconds();
    const double processingShadowsHighlightsFilterHalfresUpsampleMs =
        processingGetLastShadowsHighlightsFilterHalfresUpsampleMilliseconds();
    const double processingShadowsHighlightsFilterQuarterresDownsampleMs =
        processingGetLastShadowsHighlightsFilterQuarterresDownsampleMilliseconds();
    const double processingShadowsHighlightsFilterQuarterresRbfMs =
        processingGetLastShadowsHighlightsFilterQuarterresRbfMilliseconds();
    const double processingShadowsHighlightsFilterQuarterresUpsampleMs =
        processingGetLastShadowsHighlightsFilterQuarterresUpsampleMilliseconds();
    const double processingShadowsHighlightsRbfTotalMs =
        processingGetLastShadowsHighlightsRbfTotalMilliseconds();
    const double processingShadowsHighlightsRbfBoundaryMs =
        processingGetLastShadowsHighlightsRbfBoundaryMilliseconds();
    const double processingShadowsHighlightsRbfRangeTableMs =
        processingGetLastShadowsHighlightsRbfRangeTableMilliseconds();
    const double processingShadowsHighlightsRbfLeftMs =
        processingGetLastShadowsHighlightsRbfLeftMilliseconds();
    const double processingShadowsHighlightsRbfRightMs =
        processingGetLastShadowsHighlightsRbfRightMilliseconds();
    const double processingShadowsHighlightsRbfHorizontalAverageMs =
        processingGetLastShadowsHighlightsRbfHorizontalAverageMilliseconds();
    const double processingShadowsHighlightsRbfVerticalDownMs =
        processingGetLastShadowsHighlightsRbfVerticalDownMilliseconds();
    const double processingShadowsHighlightsRbfVerticalUpFirstLineMs =
        processingGetLastShadowsHighlightsRbfVerticalUpFirstLineMilliseconds();
    const double processingShadowsHighlightsRbfVerticalUpBodyMs =
        processingGetLastShadowsHighlightsRbfVerticalUpBodyMilliseconds();
    const double processingShadowsHighlightsRbfVerticalUpBodyDiffMs =
        processingGetLastShadowsHighlightsRbfVerticalUpBodyDiffMilliseconds();
    const double processingShadowsHighlightsRbfVerticalUpBodyStoreMs =
        processingGetLastShadowsHighlightsRbfVerticalUpBodyStoreMilliseconds();
    const double processingShadowsHighlightsRbfVerticalUpBodyStoreFactorMs =
        processingGetLastShadowsHighlightsRbfVerticalUpBodyStoreFactorMilliseconds();
    const double processingShadowsHighlightsRbfVerticalUpBodyStoreColorMs =
        processingGetLastShadowsHighlightsRbfVerticalUpBodyStoreColorMilliseconds();
    const double processingShadowsHighlightsRbfVerticalUpBodyStoreColorSrcMs =
        processingGetLastShadowsHighlightsRbfVerticalUpBodyStoreColorSrcMilliseconds();
    const double processingShadowsHighlightsRbfVerticalUpBodyStoreColorPrevMs =
        processingGetLastShadowsHighlightsRbfVerticalUpBodyStoreColorPrevMilliseconds();
    const double processingShadowsHighlightsRbfVerticalUpBodyStoreColorAssignMs =
        processingGetLastShadowsHighlightsRbfVerticalUpBodyStoreColorAssignMilliseconds();
    const double processingShadowsHighlightsRbfVerticalUpMs =
        processingGetLastShadowsHighlightsRbfVerticalUpMilliseconds();
    const double processingShadowsHighlightsRbfOutputMs =
        processingGetLastShadowsHighlightsRbfOutputMilliseconds();
    const double processingHighestGreenMs =
        processingGetLastHighestGreenMilliseconds();
    const double processingCoreMs = processingGetLastCoreMilliseconds();
    const double processingDenoiseMs = processingGetLastDenoiseMilliseconds();
    const double processingRbfMs = processingGetLastRbfMilliseconds();
    const double processingCaMs = processingGetLastCaMilliseconds();
    const double processingChromaMs = processingGetLastChromaMilliseconds();
    const double processingSharpenMs = processingGetLastSharpenMilliseconds();
    const double processingGrainMs = processingGetLastGrainMilliseconds();
    const double processingKnownMs =
        processingSetupMs +
        processingShadowsHighlightsPrepMs +
        processingHighestGreenMs +
        processingCoreMs +
        processingDenoiseMs +
        processingRbfMs +
        processingCaMs +
        processingChromaMs +
        processingSharpenMs +
        processingGrainMs;
    const double processingOtherMs = qMax( 0.0, processingMs - processingKnownMs );
    const double processingCoreLevelsMs =
        processingGetLastCoreLevelsMilliseconds();
    const double processingCoreColorMs =
        processingGetLastCoreColorMilliseconds();
    const double processingDirect8MatrixMs =
        processingGetLastDirect8MatrixMilliseconds();
    const double processingDirect8GammaMs =
        processingGetLastDirect8GammaMilliseconds();
    const double processingDirect8CurvesMs =
        processingGetLastDirect8CurvesMilliseconds();
    const double processingCoreCreativeMs =
        processingGetLastCoreCreativeMilliseconds();
    const double processingCoreOutputMs =
        processingGetLastCoreOutputMilliseconds();
    const double processingCoreKnownMs =
        processingCoreLevelsMs +
        processingCoreColorMs +
        processingCoreCreativeMs +
        processingCoreOutputMs;
    const double processingCoreOtherMs =
        qMax( 0.0, processingCoreMs - processingCoreKnownMs );

    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_ms"),
                                      rawUint16Ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_disk_read_ms"),
                                      rawUint16DiskReadMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_decompress_ms"),
                                      rawUint16DecompressMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_decompress_prepare_ms"),
                                      rawUint16DecompressPrepareMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_decompress_execute_ms"),
                                      rawUint16DecompressExecuteMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred6_split_active"),
                                      rawUint16Lj92Pred6SplitActive != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred6_split_requested"),
                                      rawUint16Lj92Pred6SplitRequested != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_generic_split_active"),
                                      rawUint16Lj92GenericSplitActive != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_generic_split_requested"),
                                      rawUint16Lj92GenericSplitRequested != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred1_fast_path_active"),
                                      rawUint16Lj92Pred1FastPathActive != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred1_fast_path_measurement_requested"),
                                      rawUint16Lj92Pred1FastPathMeasurementRequested != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred1_fast_path_measurement_active"),
                                      rawUint16Lj92Pred1FastPathMeasurementActive != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred1_fast_path_eligible"),
                                      rawUint16Lj92Pred1FastPathEligible != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_scan_component_count"),
                                      rawUint16Lj92ScanComponentCount );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_component_count"),
                                      rawUint16Lj92ComponentCount );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_write_length"),
                                      rawUint16Lj92WriteLength );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_expected_write_length"),
                                      rawUint16Lj92ExpectedWriteLength );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_skip_length"),
                                      rawUint16Lj92SkipLength );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_linearize_active"),
                                      rawUint16Lj92LinearizeActive != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_predictor"),
                                      rawUint16Lj92Predictor );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred6_total_ms"),
                                      rawUint16Lj92Pred6TotalMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred6_bitstream_ms"),
                                      rawUint16Lj92Pred6BitstreamMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred6_predictor_ms"),
                                      rawUint16Lj92Pred6PredictorMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred6_other_ms"),
                                      rawUint16Lj92Pred6OtherMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_generic_total_ms"),
                                      rawUint16Lj92GenericTotalMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_generic_bitstream_ms"),
                                      rawUint16Lj92GenericBitstreamMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_generic_predictor_ms"),
                                      rawUint16Lj92GenericPredictorMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_generic_other_ms"),
                                      rawUint16Lj92GenericOtherMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred1_fast_path_total_ms"),
                                      rawUint16Lj92Pred1FastPathTotalMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred1_fast_path_bitstream_ms"),
                                      rawUint16Lj92Pred1FastPathBitstreamMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred1_fast_path_predictor_ms"),
                                      rawUint16Lj92Pred1FastPathPredictorMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_lj92_pred1_fast_path_other_ms"),
                                      rawUint16Lj92Pred1FastPathOtherMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_unpack_ms"),
                                      rawUint16UnpackMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_copy_ms"),
                                      rawUint16CopyMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_prefetch_hit"),
                                      rawUint16PrefetchHit != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_prefetch_decode_failures"),
                                      static_cast<qint64>( rawUint16PrefetchDecodeFailures ) );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_uint16_other_ms"),
                                      rawUint16OtherMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("llrawproc_ms"),
                                      llrawprocMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("llrawproc_pre_dualiso_fix_ms"),
                                      llrawprocPreDualIsoFixMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("llrawproc_total_ms"),
                                      llrpGetLastTotalMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("llrawproc_dark_frame_ms"),
                                      llrawprocDarkFrameMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("llrawproc_vertical_stripes_ms"),
                                      llrawprocVerticalStripesMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("llrawproc_focus_pixels_ms"),
                                      llrawprocFocusPixelsMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("llrawproc_bad_pixels_ms"),
                                      llrawprocBadPixelsMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("llrawproc_pattern_noise_ms"),
                                      llrawprocPatternNoiseMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("llrawproc_dual_iso_ms"),
                                      llrawprocDualIsoMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_total_ms"),
                                      dualIsoFull20.total_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_pattern_ms"),
                                      dualIsoFull20.pattern_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_noise_ms"),
                                      dualIsoFull20.noise_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_scratch_ms"),
                                      dualIsoFull20.scratch_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_convert20_ms"),
                                      dualIsoFull20.convert20_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_match_ms"),
                                      dualIsoFull20.match_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_ms"),
                                      dualIsoFull20.interp_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_mean23_ms"),
                                      dualIsoFull20.interp_mean23_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_ms"),
                                      dualIsoFull20.interp_amaze_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_scratch_ms"),
                                      dualIsoFull20.interp_amaze_scratch_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_clear_ms"),
                                      dualIsoFull20.interp_amaze_clear_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_squeeze_ms"),
                                      dualIsoFull20.interp_amaze_squeeze_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_rawdata_ms"),
                                      dualIsoFull20.interp_amaze_rawdata_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_demosaic_ms"),
                                      dualIsoFull20.interp_amaze_demosaic_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_demosaic_setup_ms"),
                                      dualIsoFull20.interp_amaze_demosaic_setup_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_demosaic_create_ms"),
                                      dualIsoFull20.interp_amaze_demosaic_create_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_demosaic_join_ms"),
                                      dualIsoFull20.interp_amaze_demosaic_join_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_demosaic_worker_total_ms"),
                                      dualIsoFull20.interp_amaze_demosaic_worker_total_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_demosaic_worker_max_ms"),
                                      dualIsoFull20.interp_amaze_demosaic_worker_max_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_demosaic_worker_count"),
                                      dualIsoFull20.interp_amaze_demosaic_worker_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_postprocess_ms"),
                                      dualIsoFull20.interp_amaze_postprocess_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_grayscale_ms"),
                                      dualIsoFull20.interp_amaze_grayscale_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_edge_init_ms"),
                                      dualIsoFull20.interp_amaze_edge_init_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_lut_ms"),
                                      dualIsoFull20.interp_amaze_lut_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_edge_direction_ms"),
                                      dualIsoFull20.interp_amaze_edge_direction_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_edge_simd_batches"),
                                      dualIsoFull20.interp_amaze_edge_simd_batches );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_edge_allskip_batches"),
                                      dualIsoFull20.interp_amaze_edge_allskip_batches );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_edge_mixed_batches"),
                                      dualIsoFull20.interp_amaze_edge_mixed_batches );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_edge_fullsearch_batches"),
                                      dualIsoFull20.interp_amaze_edge_fullsearch_batches );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_edge_fullsearch_pixels"),
                                      dualIsoFull20.interp_amaze_edge_fullsearch_pixels );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_edge_skip_pixels"),
                                      dualIsoFull20.interp_amaze_edge_skip_pixels );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_edge_scalar_pixels"),
                                      dualIsoFull20.interp_amaze_edge_scalar_pixels );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_amaze_actual_interp_ms"),
                                      dualIsoFull20.interp_amaze_actual_interp_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_border_ms"),
                                      dualIsoFull20.interp_border_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_fullres_ms"),
                                      dualIsoFull20.fullres_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_ms"),
                                      dualIsoFull20.mix_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_curve_select_ms"),
                                      dualIsoFull20.mix_curve_select_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_curve_build_ms"),
                                      dualIsoFull20.mix_curve_build_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_curve_float_ms"),
                                      dualIsoFull20.mix_curve_float_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_ev_lut_ms"),
                                      dualIsoFull20.mix_ev_lut_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_halfres_ms"),
                                      dualIsoFull20.mix_halfres_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_halfres_avx2_bulk_ms"),
                                      dualIsoFull20.mix_halfres_avx2_bulk_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_halfres_scalar_tail_ms"),
                                      dualIsoFull20.mix_halfres_scalar_tail_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_ms"),
                                      dualIsoFull20.mix_chroma_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_copy_ms"),
                                      dualIsoFull20.mix_chroma_copy_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_fullres_ms"),
                                      dualIsoFull20.mix_chroma_fullres_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_ms"),
                                      dualIsoFull20.mix_chroma_halfres_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_horiz_probe_ms"),
                                      dualIsoFull20.mix_chroma_horiz_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_vert_probe_ms"),
                                      dualIsoFull20.mix_chroma_vert_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_gather_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_gather_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_arithmetic_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_arithmetic_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_store_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_store_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_store_r_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_store_r_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_store_b_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_store_b_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_store_r_lookup_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_store_r_lookup_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_store_b_lookup_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_store_b_lookup_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_average_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_average_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_non_average_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_non_average_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_non_average_choose_true_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_non_average_choose_true_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_non_average_choose_false_probe_ms"),
                                      dualIsoFull20.mix_chroma_center_non_average_choose_false_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_write_both_count"),
                                      dualIsoFull20.mix_chroma_center_write_both_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_write_r_only_count"),
                                      dualIsoFull20.mix_chroma_center_write_r_only_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_write_b_only_count"),
                                      dualIsoFull20.mix_chroma_center_write_b_only_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_write_none_count"),
                                      dualIsoFull20.mix_chroma_center_write_none_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_use_average_count"),
                                      dualIsoFull20.mix_chroma_center_use_average_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_center_choose_ev_lt_eh_count"),
                                      dualIsoFull20.mix_chroma_center_choose_ev_lt_eh_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_gather_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_gather_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_arithmetic_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_arithmetic_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_store_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_store_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_store_r_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_store_r_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_store_b_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_store_b_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_store_r_lookup_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_store_r_lookup_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_store_b_lookup_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_store_b_lookup_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_store_r_low_clamp_count"),
                                      dualIsoFull20.mix_chroma_halfres_center_store_r_low_clamp_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_store_b_low_clamp_count"),
                                      dualIsoFull20.mix_chroma_halfres_center_store_b_low_clamp_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_store_r_high_clamp_count"),
                                      dualIsoFull20.mix_chroma_halfres_center_store_r_high_clamp_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_store_b_high_clamp_count"),
                                      dualIsoFull20.mix_chroma_halfres_center_store_b_high_clamp_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_average_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_average_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_non_average_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_non_average_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_non_average_choose_true_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_non_average_choose_true_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_non_average_choose_false_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_non_average_choose_false_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_non_average_write_r_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_non_average_write_r_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_non_average_write_b_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_non_average_write_b_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_non_average_write_both_probe_ms"),
                                      dualIsoFull20.mix_chroma_halfres_center_non_average_write_both_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_write_both_count"),
                                      dualIsoFull20.mix_chroma_halfres_center_write_both_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_write_r_only_count"),
                                      dualIsoFull20.mix_chroma_halfres_center_write_r_only_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_write_b_only_count"),
                                      dualIsoFull20.mix_chroma_halfres_center_write_b_only_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_write_none_count"),
                                      dualIsoFull20.mix_chroma_halfres_center_write_none_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_use_average_count"),
                                      dualIsoFull20.mix_chroma_halfres_center_use_average_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_halfres_center_choose_ev_lt_eh_count"),
                                      dualIsoFull20.mix_chroma_halfres_center_choose_ev_lt_eh_count );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_probe_mode"),
                                      dualIsoFull20.mix_chroma_probe_mode );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_chroma_probe_stage"),
                                      dualIsoFull20.mix_chroma_probe_stage );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_halfres_probe_mode"),
                                      dualIsoFull20.mix_halfres_probe_mode );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_alias_map_ms"),
                                      dualIsoFull20.mix_alias_map_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_alias_map_setup_ms"),
                                      dualIsoFull20.mix_alias_map_setup_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_alias_map_init_ms"),
                                      dualIsoFull20.mix_alias_map_init_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_alias_map_copy_ms"),
                                      dualIsoFull20.mix_alias_map_copy_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_alias_map_filter_ms"),
                                      dualIsoFull20.mix_alias_map_filter_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_alias_map_gaussian_ms"),
                                      dualIsoFull20.mix_alias_map_gaussian_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_alias_map_grayscale_ms"),
                                      dualIsoFull20.mix_alias_map_grayscale_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_overexposed_ms"),
                                      dualIsoFull20.mix_overexposed_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_setup_ms"),
                                      dualIsoFull20.final_blend_setup_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_row_kernel_ms"),
                                      dualIsoFull20.final_blend_row_kernel_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_raw2ev_gather_probe_ms"),
                                      dualIsoFull20.final_blend_raw2ev_gather_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_fullres_curve_gather_probe_ms"),
                                      dualIsoFull20.final_blend_fullres_curve_gather_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_ev2raw_store_probe_ms"),
                                      dualIsoFull20.final_blend_ev2raw_store_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_arithmetic_probe_ms"),
                                      dualIsoFull20.final_blend_arithmetic_probe_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_overexposed_density"),
                                      dualIsoFull20.final_blend_overexposed_density );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_cap_clamp_pct"),
                                      dualIsoFull20.final_blend_cap_clamp_pct );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_f_near_0_pct"),
                                      dualIsoFull20.final_blend_f_near_0_pct );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_f_near_1_pct"),
                                      dualIsoFull20.final_blend_f_near_1_pct );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_ms"),
                                      dualIsoFull20.final_blend_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_convert16_ms"),
                                      dualIsoFull20.convert16_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_other_ms"),
                                      dualIsoFull20.other_ms );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_curve_corr_ev"),
                                      dualIsoFull20.mix_curve_corr_ev );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_curve_overlap"),
                                      dualIsoFull20.mix_curve_overlap );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_curve_rebuilt"),
                                      dualIsoFull20.mix_curve_rebuilt != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_mix_curve_global_hit"),
                                      dualIsoFull20.mix_curve_global_hit != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_interp_method"),
                                      dualIsoFull20.interp_method );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_final_blend_probe_mode"),
                                      dualIsoFull20.final_blend_probe_mode );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_use_alias_map"),
                                      dualIsoFull20.use_alias_map != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_use_fullres"),
                                      dualIsoFull20.use_fullres != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_threads"),
                                      dualIsoFull20.threads );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_full20_valid"),
                                      dualIsoFull20.valid != 0 );
    insertDualIsoWarmupInstrumentationTelemetry( slot.stageTimingTelemetry,
                                                 dualIsoFull20 );
    slot.stageTimingTelemetry.insert( QStringLiteral("llrawproc_chroma_smooth_ms"),
                                      llrawprocChromaSmoothMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("llrawproc_other_ms"),
                                      llrawprocOtherMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_preview_total_ms"),
                                      dualIsoPreviewTotalMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_preview_histogram_ms"),
                                      slot.dualIsoPreviewHistogramMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_preview_regression_ms"),
                                      slot.dualIsoPreviewRegressionMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("dual_iso_preview_rowscale_ms"),
                                      slot.dualIsoPreviewRowscaleMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("debayered_frame_ms"),
                                      getMlvLastDebayeredFrameMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("raw_float_convert_ms"),
                                      rawFloatConvertMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("debayer_exclusive_ms"),
                                      debayerExclusiveMs );
    const int debayerEngineMode =
        m_pMlvObject ? doesMlvAlwaysUseAmaze( m_pMlvObject ) : -1;
    const bool debayerBasicU16Avx2Available =
        debayerBasicU16Avx2Active() != 0;
    slot.stageTimingTelemetry.insert( QStringLiteral("debayer_engine_mode"),
                                      debayerEngineMode );
    slot.stageTimingTelemetry.insert( QStringLiteral("debayer_basic_u16_avx2_available"),
                                      debayerBasicU16Avx2Available );
    slot.stageTimingTelemetry.insert( QStringLiteral("debayer_basic_u16_avx2_used"),
                                      debayerEngineMode == 0
                                      && debayerBasicU16Avx2Available );
    slot.stageTimingTelemetry.insert( QStringLiteral("debayer_wb_prepare_ms"),
                                      debayerWbPrepareMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("debayer_ca_ms"),
                                      debayerCaMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("debayer_kernel_ms"),
                                      debayerKernelMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("debayer_wb_undo_ms"),
                                      debayerWbUndoMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("debayer_pipeline_other_ms"),
                                      debayerPipelineOtherMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_ms"),
                                      processingMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_setup_ms"),
                                      processingSetupMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_prep_ms"),
                                      processingShadowsHighlightsPrepMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_resize_ms"),
                                      processingShadowsHighlightsResizeMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_copy_ms"),
                                      processingShadowsHighlightsCopyMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_filter_ms"),
                                      processingShadowsHighlightsFilterMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_filter_fullres_ms"),
                                      processingShadowsHighlightsFilterFullresMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_filter_halfres_downsample_ms"),
                                      processingShadowsHighlightsFilterHalfresDownsampleMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_filter_halfres_rbf_ms"),
                                      processingShadowsHighlightsFilterHalfresRbfMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_filter_halfres_upsample_ms"),
                                      processingShadowsHighlightsFilterHalfresUpsampleMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_filter_quarterres_downsample_ms"),
                                      processingShadowsHighlightsFilterQuarterresDownsampleMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_filter_quarterres_rbf_ms"),
                                      processingShadowsHighlightsFilterQuarterresRbfMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_filter_quarterres_upsample_ms"),
                                      processingShadowsHighlightsFilterQuarterresUpsampleMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_total_ms"),
                                      processingShadowsHighlightsRbfTotalMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_boundary_ms"),
                                      processingShadowsHighlightsRbfBoundaryMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_range_table_ms"),
                                      processingShadowsHighlightsRbfRangeTableMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_left_ms"),
                                      processingShadowsHighlightsRbfLeftMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_right_ms"),
                                      processingShadowsHighlightsRbfRightMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_horizontal_average_ms"),
                                      processingShadowsHighlightsRbfHorizontalAverageMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_vertical_down_ms"),
                                      processingShadowsHighlightsRbfVerticalDownMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_vertical_up_first_line_ms"),
                                      processingShadowsHighlightsRbfVerticalUpFirstLineMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_vertical_up_body_ms"),
                                      processingShadowsHighlightsRbfVerticalUpBodyMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_vertical_up_body_diff_ms"),
                                      processingShadowsHighlightsRbfVerticalUpBodyDiffMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_vertical_up_body_store_ms"),
                                      processingShadowsHighlightsRbfVerticalUpBodyStoreMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_vertical_up_body_store_factor_ms"),
                                      processingShadowsHighlightsRbfVerticalUpBodyStoreFactorMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_vertical_up_body_store_color_ms"),
                                      processingShadowsHighlightsRbfVerticalUpBodyStoreColorMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_vertical_up_body_store_color_src_ms"),
                                      processingShadowsHighlightsRbfVerticalUpBodyStoreColorSrcMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_vertical_up_body_store_color_prev_ms"),
                                      processingShadowsHighlightsRbfVerticalUpBodyStoreColorPrevMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_vertical_up_body_store_color_assign_ms"),
                                      processingShadowsHighlightsRbfVerticalUpBodyStoreColorAssignMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_vertical_up_ms"),
                                      processingShadowsHighlightsRbfVerticalUpMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_shadows_highlights_rbf_output_ms"),
                                      processingShadowsHighlightsRbfOutputMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_highest_green_ms"),
                                      processingHighestGreenMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_ms"),
                                      processingCoreMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_denoise_ms"),
                                      processingDenoiseMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_rbf_ms"),
                                      processingRbfMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_ca_ms"),
                                      processingCaMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_chroma_ms"),
                                      processingChromaMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_sharpen_ms"),
                                      processingSharpenMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_grain_ms"),
                                      processingGrainMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_other_ms"),
                                      processingOtherMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_levels_ms"),
                                      processingCoreLevelsMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_ms"),
                                      processingCoreColorMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_ms"),
                                      processingGetLastCoreColorMainMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_gradient_ms"),
                                      processingGetLastCoreColorGradientMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_ms"),
                                      processingGetLastCoreColorMainPreludeMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_vignette_ms"),
                                      processingGetLastCoreColorMainPreludeVignetteMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_creative_ms"),
                                      processingGetLastCoreColorMainPreludeCreativeMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_creative_shadows_ms"),
                                      processingGetLastCoreColorMainPreludeCreativeShadowsMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_creative_contrast_ms"),
                                      processingGetLastCoreColorMainPreludeCreativeContrastMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_wb_ms"),
                                      processingGetLastCoreColorMainPreludeWbMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_wb_matrix_ms"),
                                      processingGetLastCoreColorMainPreludeWbMatrixMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_wb_matrix_r_ms"),
                                      processingGetLastCoreColorMainPreludeWbMatrixRMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_wb_matrix_g_ms"),
                                      processingGetLastCoreColorMainPreludeWbMatrixGMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_wb_matrix_b_ms"),
                                      processingGetLastCoreColorMainPreludeWbMatrixBMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_wb_gradient_matrix_ms"),
                                      processingGetLastCoreColorMainPreludeWbGradientMatrixMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_wb_exposure_ms"),
                                      processingGetLastCoreColorMainPreludeWbExposureMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_wb_gamut_ms"),
                                      processingGetLastCoreColorMainPreludeWbGamutMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_main_prelude_wb_recon_ms"),
                                      processingGetLastCoreColorMainPreludeWbReconMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_ms"),
                                      processingGetLastCoreColorCamMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_main_ms"),
                                      processingGetLastCoreColorCamMainMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_gradient_ms"),
                                      processingGetLastCoreColorCamGradientMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_wb_ms"),
                                      processingGetLastCoreColorCamWbMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_wb_matrix_ms"),
                                      processingGetLastCoreColorCamWbMatrixMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_wb_gamut_ms"),
                                      processingGetLastCoreColorCamWbGamutMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_wb_desat_ms"),
                                      processingGetLastCoreColorCamWbDesatMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_ms"),
                                      processingGetLastCoreColorCamAgxMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_clip_ms"),
                                      processingGetLastCoreColorCamAgxClipMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_clip_neg_r_count"),
                                      processingGetLastCoreColorCamAgxClipNegRCount() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_clip_neg_g_count"),
                                      processingGetLastCoreColorCamAgxClipNegGCount() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_clip_neg_b_count"),
                                      processingGetLastCoreColorCamAgxClipNegBCount() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_matrix_ms"),
                                      processingGetLastCoreColorCamAgxMatrixMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_matrix_r_ms"),
                                      processingGetLastCoreColorCamAgxMatrixRMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_matrix_g_ms"),
                                      processingGetLastCoreColorCamAgxMatrixGMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_matrix_b_ms"),
                                      processingGetLastCoreColorCamAgxMatrixBMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_matrix_r_hi_count"),
                                      processingGetLastCoreColorCamAgxMatrixRHiCount() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_matrix_g_hi_count"),
                                      processingGetLastCoreColorCamAgxMatrixGHiCount() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_cam_agx_matrix_b_hi_count"),
                                      processingGetLastCoreColorCamAgxMatrixBHiCount() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_gamma_ms"),
                                      processingGetLastCoreColorGammaMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_gamma_main_ms"),
                                      processingGetLastCoreColorGammaMainMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_color_gamma_gradient_ms"),
                                      processingGetLastCoreColorGammaGradientMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_direct8_matrix_ms"),
                                      processingDirect8MatrixMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_direct8_gamma_ms"),
                                      processingDirect8GammaMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_direct8_curves_ms"),
                                      processingDirect8CurvesMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_creative_ms"),
                                      processingCoreCreativeMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_creative_hue_vs_ms"),
                                      processingGetLastCoreCreativeHueVsMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_creative_vibrance_ms"),
                                      processingGetLastCoreCreativeVibranceMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_creative_saturation_ms"),
                                      processingGetLastCoreCreativeSaturationMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_creative_toning_ms"),
                                      processingGetLastCoreCreativeToningMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_creative_curve_ms"),
                                      processingGetLastCoreCreativeCurveMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_creative_gradation_ms"),
                                      processingGetLastCoreCreativeGradationMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_creative_agx_inverse_ms"),
                                      processingGetLastCoreCreativeAgxInverseMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_output_ms"),
                                      processingCoreOutputMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processing_core_other_ms"),
                                      processingCoreOtherMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("processed16_total_ms"),
                                      getMlvLastProcessed16TotalMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processed16_for_8bit_ms"),
                                      getMlvLastProcessed16For8BitMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processed16_to_8bit_ms"),
                                      getMlvLastProcessed16To8BitMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processed16_cache_store_ms"),
                                      getMlvLastProcessed16CacheStoreMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processed8_total_ms"),
                                      getMlvLastProcessed8TotalMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processed8_cache_store_ms"),
                                      getMlvLastProcessed8CacheStoreMilliseconds() );
    slot.stageTimingTelemetry.insert( QStringLiteral("processed8_direct_path_active"),
                                      getMlvLastProcessed8DirectPathActive() != 0 );
    slot.stageTimingTelemetry.insert( QStringLiteral("processed8_direct_path_reason"),
                                      QString::fromLatin1(
                                          processingGetDirect8IncompatibilityReason(
                                              m_pMlvObject ? m_pMlvObject->processing : nullptr ) ) );
    slot.stageTimingTelemetry.insert( QStringLiteral("processed8_cache_hit"),
                                      processed8CacheHit );
    slot.stageTimingTelemetry.insert( QStringLiteral("processed8_cache_hit_scale_factor"),
                                      processed8CacheHitScale );
    slot.stageTimingTelemetry.insert( QStringLiteral("processed8_prefetch_hit"),
                                      processed8PrefetchHit );
    if( skipCpuDebayerForGpuTextureNoReadback )
    {
        // The no-readback GPU AMaZE texture path does not run the CPU debayer, so the
        // getMlvLast*Milliseconds() debayer-detail getters return stale carryover from the
        // last frame that did (e.g. warmup). Zero them here so the proof artifact reflects
        // the live path instead of a phantom ~240ms CPU debayer. Cross-check is render_work_ms;
        // render_thread_cpu_amaze_debayer_skipped_for_gpu_tex_nr distinguishes this from a true 0.
        slot.stageTimingTelemetry.insert( QStringLiteral("debayered_frame_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("debayer_exclusive_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("raw_float_convert_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("debayer_wb_prepare_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("debayer_ca_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("debayer_kernel_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("debayer_wb_undo_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("debayer_pipeline_other_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("processed16_total_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("processed16_for_8bit_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("processed16_to_8bit_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("processed16_cache_store_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("processed8_total_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("processed8_cache_store_ms"), 0.0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("processed8_direct_path_active"), false );
        slot.stageTimingTelemetry.insert( QStringLiteral("processed8_cache_hit"), false );
        slot.stageTimingTelemetry.insert( QStringLiteral("processed8_cache_hit_scale_factor"), 0 );
        slot.stageTimingTelemetry.insert( QStringLiteral("processed8_prefetch_hit"), false );
        restoreGpuPlaybackReconTimingTelemetry(
            slot.stageTimingTelemetry,
            preservedGpuPlaybackReconTextureTelemetry );
    }

    slot.processedFrame8Active =
        m_pMlvObject && m_pMlvObject->current_processed_frame_8bit_active;
    slot.processedFrame8Signature =
        m_pMlvObject ? m_pMlvObject->current_processed_frame_8bit_signature : 0;
    slot.processedFrame16Active =
        m_pMlvObject && m_pMlvObject->current_processed_frame_active;
    slot.processedFrame16Signature =
        m_pMlvObject ? m_pMlvObject->current_processed_frame_signature : 0;
    if( m_pMlvObject && m_pMlvObject->llrawproc )
    {
        slot.dualIsoPattern = m_pMlvObject->llrawproc->diso_pattern;
        slot.dualIsoAutoCorrection = m_pMlvObject->llrawproc->diso_auto_correction;
        slot.dualIsoEvCorrection = m_pMlvObject->llrawproc->diso_ev_correction;
        slot.dualIsoBlackDelta = m_pMlvObject->llrawproc->diso_black_delta;
    }

    const double renderThreadEndStageTime = mlv_stage_timing_now();
    const double renderThreadWorkMs = (renderThreadEndStageTime - render_start) * 1000.0;
    const double renderThreadTotalMs =
        (frameRequestStageTime > 0.0 && renderThreadEndStageTime >= frameRequestStageTime)
            ? (renderThreadEndStageTime - frameRequestStageTime) * 1000.0
            : renderThreadWorkMs;
    slot.frameReadyEmitStageTime = renderThreadEndStageTime;
    if( playbackSmokeTimelineTelemetryEnabled() )
    {
        slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_end_stage_time"),
                                          renderThreadEndStageTime );
        slot.stageTimingTelemetry.insert( QStringLiteral("frame_ready_emit_stage_time"),
                                          slot.frameReadyEmitStageTime );
    }
    slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_queue_wait_ms"),
                                      renderThreadQueueWaitMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_work_ms"),
                                      renderThreadWorkMs );
    slot.stageTimingTelemetry.insert( QStringLiteral("render_thread_total_ms"),
                                      renderThreadTotalMs );
    if( gpuTexNrOverlapTraceEnabled() )
    {
        qInfo().nospace()
            << "gpu_tex_nr_overlap_trace event=render_end"
            << " frame=" << slot.frameNumber
            << " serial=" << slot.requestSerial
            << " slot=" << slotIndex
            << " work_ms=" << renderThreadWorkMs
            << " total_ms=" << renderThreadTotalMs
            << " wall_ms=" << QDateTime::currentMSecsSinceEpoch();
    }
}
