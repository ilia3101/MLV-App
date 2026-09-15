/*!
 * \file RenderFrameThread.h
 * \author masc4ii
 * \copyright 2017
 * \brief The render thread
 */

#ifndef RENDERFRAMETHREAD_H
#define RENDERFRAMETHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <QJsonObject>
#include "../../src/mlv_include.h"
#include "MainWindowGpuPreviewPolicy.h"
#include "Phase3Mode.h"
#include "PlaybackScaling.h"

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <vector>

class DecodeWorker;
class ReconWorker;

class RenderFrameThread : public QThread
{
    Q_OBJECT

public:
    enum OutputMode
    {
        OutputProcessed8 = 0,
        OutputProcessed16,
        OutputDebayered16
    };

    enum class SlotState : uint8_t
    {
        Idle = 0,
        Requested,
        Decoding,
        Decoded,
        ReconReady,
        Recon,
        ProcessReady,
        Processing,
        Ready,
        Presenting
    };

    struct GpuPlaybackReconTextureState
    {
        struct LutSnapshot
        {
            std::vector<int> raw2ev;
            std::vector<int> ev2raw;
            std::vector<double> mixCurve;
            std::vector<double> fullresCurve;
            std::vector<float> randn05;
        };

        struct LutSnapshotKey
        {
            const int *raw2ev = nullptr;
            const int *ev2raw = nullptr;
            const double *mixCurve = nullptr;
            const double *fullresCurve = nullptr;
            const float *randn05 = nullptr;
            int width = 0;
            int height = 0;
            int blackLevel = 0;
            int whiteLevel = 0;
            int whiteDarkened = 0;
            int blackDelta = 0;
            double evCorrection = 0.0;
            double darkNoise = 0.0;
            int interpMethod = 0;
            bool useAliasMap = false;
            bool useFullres = false;
            int chromaSmoothMethod = 0;
            std::array<int, 4> isBright{{0, 0, 0, 0}};
            bool applyDither = false;
            uint64_t processingGeneration = 0;

            bool operator==( const LutSnapshotKey &other ) const
            {
                return raw2ev == other.raw2ev
                    && ev2raw == other.ev2raw
                    && mixCurve == other.mixCurve
                    && fullresCurve == other.fullresCurve
                    && randn05 == other.randn05
                    && width == other.width
                    && height == other.height
                    && blackLevel == other.blackLevel
                    && whiteLevel == other.whiteLevel
                    && whiteDarkened == other.whiteDarkened
                    && blackDelta == other.blackDelta
                    && evCorrection == other.evCorrection
                    && darkNoise == other.darkNoise
                    && interpMethod == other.interpMethod
                    && useAliasMap == other.useAliasMap
                    && useFullres == other.useFullres
                    && chromaSmoothMethod == other.chromaSmoothMethod
                    && isBright == other.isBright
                    && applyDither == other.applyDither
                    && processingGeneration == other.processingGeneration;
            }
        };

        struct LutCacheEntry
        {
            LutSnapshotKey key;
            std::shared_ptr<const LutSnapshot> luts;
            uint64_t lastUse = 0;
        };

        bool valid = false;
        int width = 0;
        int height = 0;
        int blackLevel = 0;
        int whiteLevel = 0;
        int whiteDarkened = 0;
        int blackDelta = 0;
        double evCorrection = 0.0;
        double darkNoise = 0.0;
        int interpMethod = 0;
        bool useAliasMap = false;
        bool useFullres = false;
        int chromaSmoothMethod = 0;
        std::array<int, 4> isBright{{0, 0, 0, 0}};
        bool applyDither = false;
        uint64_t processingGeneration = 0;
        std::shared_ptr<const LutSnapshot> luts;
    };

    struct ReadyFrame
    {
        struct PresentationContext
        {
            uint64_t requestSerial = 0;
            uint32_t frameNumber = 0;
            int sceneWidth = 0;
            int sceneHeight = 0;
            int imageWidth = 0;
            int imageHeight = 0;
            int devicePixelRatioMilli = 0;
            bool zoomFitEnabled = false;
            bool fastPlaybackScaleEligible = false;
            uint64_t presentationGeneration = 0;
            /* Requested playback scale factor (1, 2, 4, or 8). The renderer may
             * reject incompatible clip dimensions and report the active scale. */
            int playbackScaleFactor = 1;
            MainWindowGpuPreviewPolicyState gpuPreviewPolicy;
            GpuDisplayViewport::PresentationOptions gpuPresentationOptions;
            GpuPreviewProcessingConfig gpuPreviewProcessingConfig;
            uint64_t gpuPreviewProcessingConfigGeneration = 0;
            QString playbackProcessingReason;
            bool playbackActive = false;
            bool dropFramePlaybackActive = false;
            bool renderThreadUsing16BitPreview = false;
            bool renderThreadUsingGpuPreviewProcessing = false;
            bool renderThreadUsingGpuBilinearDebayer = false;
            bool renderThreadUsingGpuAmazeDebayer = false;
            bool gpuAmazeTexturePresentRequested = false;
            bool gpuPlaybackReconTexturePresentRequested = false;
            bool gpuPlaybackReconAmazeTexturePresentAdmitted = false;
            QString gpuPlaybackReconAmazeTexturePresentFallbackReason;
            QString gpuPlaybackReconAmazeTexturePresentRenderer;
            QString gpuPlaybackReconTexturePresentFallbackReason;
            bool renderThreadUsingCpuPreviewProcessing = false;
            bool renderThreadUsingPlaybackPreviewProcessing = false;
            bool playbackLookaheadRequest = false;
            uint32_t playbackLookaheadOriginFrame = 0;
            int playbackLookaheadDepth = 0;
            double renderRequestIssueStageTime = 0.0;
            bool playbackTimelineAdvanceRequest = false;
            bool playbackTimelineAdvanceEarly = false;
            bool playbackTimelinePredictiveGpuTexNr = false;
            uint64_t playbackTimelineSourceRequestSerial = 0;
            uint32_t playbackTimelineSourceFrame = 0;
            double playbackTimelineAdvanceIssueStageTime = 0.0;
            double playbackTimelineSourceFrameReadyEmitStageTime = 0.0;
            double playbackTimelineSourceDrawBeginStageTime = 0.0;
        };

        const uint8_t *rawImage8 = nullptr;
        const uint16_t *rawImage16 = nullptr;
        size_t rawImage16Words = 0;
        const uint8_t *playbackScaledImage8 = nullptr;
        uint32_t frameNumber = 0;
        uint64_t requestSerial = 0;
        OutputMode outputMode = OutputProcessed8;
        int renderedImageWidth = 0;
        int renderedImageHeight = 0;
        int playbackScaleFactorActive = 1;
        bool playbackFastScaleActive = false;
        int playbackScaledWidth = 0;
        int playbackScaledHeight = 0;
        int playbackScaledBytesPerLine = 0;
        bool usedGpuBilinearDebayer = false;
        QString gpuBilinearFallbackReason;
        QString gpuBilinearRendererDescription;
        bool usedGpuAmazeDebayer = false;
        QString gpuAmazeFallbackReason;
        QString gpuAmazeRendererDescription;
        bool gpuAmazeTexturePresentCandidate = false;
        const float *gpuAmazeTextureRawFrame = nullptr;
        size_t gpuAmazeTextureRawFrameSize = 0;
        int gpuAmazeTextureWidth = 0;
        int gpuAmazeTextureHeight = 0;
        int gpuAmazeTextureBlackLevel = 0;
        std::array<double, 3> gpuAmazeTextureWbMultipliers{{1.0, 1.0, 1.0}};
        bool gpuPlaybackReconTexturePresentCandidate = false;
        bool gpuPlaybackReconTextureNoReadbackCandidate = false;
        const uint16_t *gpuPlaybackReconTextureBayerFrame = nullptr;
        size_t gpuPlaybackReconTextureBayerFrameSize = 0;
        const uint16_t *gpuPlaybackReconTextureInputBayerFrame = nullptr;
        size_t gpuPlaybackReconTextureInputBayerFrameSize = 0;
        const uint16_t *gpuPlaybackReconTextureRetainedDeviceBayer16 = nullptr;
        int gpuPlaybackReconTextureRetainedDeviceWidth = 0;
        int gpuPlaybackReconTextureRetainedDeviceHeight = 0;
        uint64_t gpuPlaybackReconTextureRetainedDeviceToken = 0;
        int gpuPlaybackReconTextureWidth = 0;
        int gpuPlaybackReconTextureHeight = 0;
        int gpuPlaybackReconTextureBlackLevel = 0;
        std::array<double, 3> gpuPlaybackReconTextureWbMultipliers{{1.0, 1.0, 1.0}};
        GpuPlaybackReconTextureState gpuPlaybackReconTextureState;
        double dualIsoPreviewHistogramMs = 0.0;
        double dualIsoPreviewRegressionMs = 0.0;
        double dualIsoPreviewRowscaleMs = 0.0;
        double frameReadyEmitStageTime = 0.0;
        PresentationContext presentationContext;
        bool processedFrame8Active = false;
        uint64_t processedFrame8Signature = 0;
        bool processedFrame16Active = false;
        uint64_t processedFrame16Signature = 0;
        int dualIsoPattern = 0;
        int dualIsoAutoCorrection = 0;
        double dualIsoEvCorrection = 0.0;
        int dualIsoBlackDelta = 0;
        QJsonObject stageTimingTelemetry;
    };

    struct PresentationPreparationOptions
    {
        bool fastPlaybackScale = false;
        int targetWidth = 0;
        int targetHeight = 0;
    };

    struct RenderRequest
    {
        uint32_t frameNumber = 0;
        OutputMode outputMode = OutputProcessed8;
        bool useGpuBilinearDebayer = false;
        bool useGpuAmazeDebayer = false;
        uint64_t requestSerial = 0;
        double renderFrameEntryStageTime = 0.0;
        double requestStageTime = 0.0;
        double requestQueuePushStageTime = 0.0;
        double phase3LoopWakeStageTime = 0.0;
        double phase3RenderRequestTakeStageTime = 0.0;
        double phase3AfterTakeStampStageTime = 0.0;
        double phase3AfterRenderFrameFlagStageTime = 0.0;
        double phase3ModeBranchEntryStageTime = 0.0;
        double phase3BeforeLiveFallbackCheckStageTime = 0.0;
        double phase3AfterLiveFallbackCheckStageTime = 0.0;
        double phase3BeforeKillSwitchCheckStageTime = 0.0;
        double phase3AfterKillSwitchCheckStageTime = 0.0;
        double phase3BeforeFirstTransitionStageTime = 0.0;
        double phase3AfterFirstTransitionStageTime = 0.0;
        double phase3BeforeSecondTransitionStageTime = 0.0;
        double phase3AfterSecondTransitionStageTime = 0.0;
        double phase3ModeBranchExitStageTime = 0.0;
        double phase3PolicyDoneStageTime = 0.0;
        double phase3ActiveAssignDoneStageTime = 0.0;
        double phase3BeforeDecodeAheadStageTime = 0.0;
        double phase3AfterDecodeAheadStageTime = 0.0;
        double phase3BeforeUnlockStageTime = 0.0;
        double phase3AfterUnlockStageTime = 0.0;
        double phase3RenderDecodedSlotEntryStageTime = 0.0;
        double phase3BeforeDrawFrameStageTime = 0.0;
        double runSerialActiveAssignDoneStageTime = 0.0;
        double runSerialPhasePolicyDoneStageTime = 0.0;
        double runSerialBeforeUnlockStageTime = 0.0;
        double runSerialAfterUnlockStageTime = 0.0;
        double runSerialBeforeDrawFrameStageTime = 0.0;
        Phase3Mode phase3Mode = Phase3Mode::Disabled;
        ReadyFrame::PresentationContext presentationContext;
        PresentationPreparationOptions presentationPreparationOptions;
        int queuedPlaybackDropCount = 0;
        bool renderThreadBusyAtRequest = false;
        bool renderThreadRenderingAtRequest = false;
        bool renderThreadQueuedAtRequest = false;
        bool phase3WorkInFlightAtRequest = false;
        int renderRequestQueueDepthAtRequest = 0;
        int freeSlotCountAtRequest = 0;
        int readySlotCountAtRequest = 0;
        int presentingSlotCountAtRequest = 0;
        int phase3ActiveSlotCountAtRequest = 0;
        int decodeRequestCountAtRequest = 0;
        int reconRequestCountAtRequest = 0;
        int decodeReadySlotCountAtRequest = 0;
        int processReadySlotCountAtRequest = 0;
    };

    struct DecodeQueueEntry
    {
        int slotIndex = -1;
        RenderRequest request;
    };

    struct ReconQueueEntry
    {
        int slotIndex = -1;
        RenderRequest request;
    };

    RenderFrameThread();
    ~RenderFrameThread();
    void init( mlvObject_t *pMlvObject,
               int imageWidth,
               int imageHeight );
    void renderFrame( uint32_t frameNumber,
                      OutputMode outputMode,
                      bool useGpuBilinearDebayer,
                      bool useGpuAmazeDebayer,
                      uint64_t requestSerial,
                      const ReadyFrame::PresentationContext &presentationContext,
                      const PresentationPreparationOptions &presentationPreparation );
    void setGpuPlaybackReconTextureLutSnapshotGeneration( uint64_t generation );
    bool isFrameReady( void );
    bool isIdle( void );
    bool acquireLatestReadyFrame( ReadyFrame *frame );
    bool acquireOldestGpuTextureNoReadbackReadyFrame( ReadyFrame *frame );
    bool acquireLatestGpuTextureNoReadbackReadyFrame( ReadyFrame *frame );
    bool acquireOldestGpuTextureNoReadbackReadyFrameForPlaybackTarget(
        ReadyFrame *frame,
        int activePlaybackTarget,
        uint64_t activeGeneration );
    bool acquireLatestReadyFrameForPlaybackTarget( ReadyFrame *frame,
                                                   int activePlaybackTarget,
                                                   uint64_t activeGeneration );
    bool hasGpuTextureNoReadbackReadyFrame( void );
    int gpuTextureNoReadbackReadyFrameCount( void );
    uint64_t decodeRequestsIssuedCount( void ) const;
    bool hasPlaybackLookaheadRequest( uint32_t frameNumber,
                                      uint64_t activeGeneration );
    bool hasReadyPlaybackLookaheadFrame( uint32_t frameNumber,
                                         uint64_t activeGeneration );
    int prunePlaybackLookaheadOutsideForwardWindow( int activePlaybackTarget,
                                                    uint64_t activeGeneration,
                                                    int cutInFrame,
                                                    int cutOutFrame,
                                                    int lookaheadDepth,
                                                    bool loopEnabled );
    int cancelPlaybackPresentationRequests( uint64_t presentationGeneration );
    void releasePresentedFrame( void );
    void releasePresentedFrameForRequestSerial( uint64_t requestSerial );
    bool lastFrameUsedGpuBilinearDebayer( void ) const;
    QString lastGpuBilinearFallbackReason( void ) const;
    QString lastGpuBilinearRendererDescription( void ) const;
    bool lastFrameUsedGpuAmazeDebayer( void ) const;
    QString lastGpuAmazeFallbackReason( void ) const;
    QString lastGpuAmazeRendererDescription( void ) const;
    double lastDualIsoPreviewHistogramMilliseconds( void ) const;
    double lastDualIsoPreviewRegressionMilliseconds( void ) const;
    double lastDualIsoPreviewRowscaleMilliseconds( void ) const;
    QJsonObject lastStageTimingTelemetry( void ) const;
    double lastFrameReadyEmitStageTime( void ) const;
    void setPhase3Mode( Phase3Mode mode ) noexcept;
    Phase3Mode phase3Mode( void ) const noexcept;
    void stop( void );
    void lock( void );
    void unlock( void );

signals:
    void frameReady( void );

private:
    struct FrameSlot
    {
        std::vector<uint8_t> rawImage8;
        std::vector<uint16_t> rawImage16;
        std::vector<uint8_t> playbackScaledImage8;
        uint32_t frameNumber = 0;
        uint64_t requestSerial = 0;
        OutputMode outputMode = OutputProcessed8;
        bool ready = false;
        bool presenting = false;
        std::atomic<SlotState> state{ SlotState::Idle };
        Phase3Mode phase3Mode = Phase3Mode::Disabled;
        int renderedImageWidth = 0;
        int renderedImageHeight = 0;
        int playbackScaleFactorActive = 1;
        bool playbackFastScaleActive = false;
        int playbackScaledWidth = 0;
        int playbackScaledHeight = 0;
        int playbackScaledBytesPerLine = 0;
        bool usedGpuBilinearDebayer = false;
        QString gpuBilinearFallbackReason;
        QString gpuBilinearRendererDescription;
        bool usedGpuAmazeDebayer = false;
        QString gpuAmazeFallbackReason;
        QString gpuAmazeRendererDescription;
        bool gpuAmazeTexturePresentCandidate = false;
        std::vector<float> gpuAmazeTextureRawFrame;
        int gpuAmazeTextureWidth = 0;
        int gpuAmazeTextureHeight = 0;
        int gpuAmazeTextureBlackLevel = 0;
        std::array<double, 3> gpuAmazeTextureWbMultipliers{{1.0, 1.0, 1.0}};
        bool gpuPlaybackReconTexturePresentCandidate = false;
        bool gpuPlaybackReconTextureNoReadbackCandidate = false;
        std::vector<uint16_t> gpuPlaybackReconTextureInputBayerFrame;
        /* Normal admitted no-readback playback can borrow rawImage16 instead of
         * keeping a duplicate input snapshot; validation/fallback paths still use
         * the owned vector so proof and CPU-readback behavior stay fail-closed. */
        bool gpuPlaybackReconTextureInputBorrowedFromRawImage16 = false;
        const uint16_t *gpuPlaybackReconTextureRetainedDeviceBayer16 = nullptr;
        int gpuPlaybackReconTextureRetainedDeviceWidth = 0;
        int gpuPlaybackReconTextureRetainedDeviceHeight = 0;
        uint64_t gpuPlaybackReconTextureRetainedDeviceToken = 0;
        /* Recon-output Dual ISO bayer, snapshotted right after the recon stage.
         * The slot's rawImage16 buffer is later overwritten by the process stage
         * (getMlvProcessedFrame16Scaled), so the recon bayer that the no-readback
         * GL R16 texture actually represents must be preserved separately when
         * output-validation is explicitly enabled. Normal no-readback playback
         * leaves this empty so it does not copy a proof-only full-resolution
         * oracle on every presented frame. */
        std::vector<uint16_t> gpuPlaybackReconTextureBayerFrame;
        GpuPlaybackReconTextureState gpuPlaybackReconTextureState;
        int gpuPlaybackReconTextureWidth = 0;
        int gpuPlaybackReconTextureHeight = 0;
        int gpuPlaybackReconTextureBlackLevel = 0;
        std::array<double, 3> gpuPlaybackReconTextureWbMultipliers{{1.0, 1.0, 1.0}};
        double dualIsoPreviewHistogramMs = 0.0;
        double dualIsoPreviewRegressionMs = 0.0;
        double dualIsoPreviewRowscaleMs = 0.0;
        double frameReadyEmitStageTime = 0.0;
        ReadyFrame::PresentationContext presentationContext;
        bool processedFrame8Active = false;
        uint64_t processedFrame8Signature = 0;
        bool processedFrame16Active = false;
        uint64_t processedFrame16Signature = 0;
        int dualIsoPattern = 0;
        int dualIsoAutoCorrection = 0;
        double dualIsoEvCorrection = 0.0;
        int dualIsoBlackDelta = 0;
        QJsonObject stageTimingTelemetry;
        RenderRequest queuedRequest;

        void resetMetadata( void )
        {
            frameNumber = 0;
            requestSerial = 0;
            outputMode = OutputProcessed8;
            ready = false;
            presenting = false;
            phase3Mode = Phase3Mode::Disabled;
            renderedImageWidth = 0;
            renderedImageHeight = 0;
            playbackScaleFactorActive = 1;
            playbackFastScaleActive = false;
            playbackScaledWidth = 0;
            playbackScaledHeight = 0;
            playbackScaledBytesPerLine = 0;
            usedGpuBilinearDebayer = false;
            gpuBilinearFallbackReason.clear();
            gpuBilinearRendererDescription.clear();
            usedGpuAmazeDebayer = false;
            gpuAmazeFallbackReason.clear();
            gpuAmazeRendererDescription.clear();
            gpuAmazeTexturePresentCandidate = false;
            gpuAmazeTextureRawFrame.clear();
            gpuAmazeTextureWidth = 0;
            gpuAmazeTextureHeight = 0;
            gpuAmazeTextureBlackLevel = 0;
            gpuAmazeTextureWbMultipliers = {{1.0, 1.0, 1.0}};
            gpuPlaybackReconTexturePresentCandidate = false;
            gpuPlaybackReconTextureNoReadbackCandidate = false;
            gpuPlaybackReconTextureInputBayerFrame.clear();
            gpuPlaybackReconTextureInputBorrowedFromRawImage16 = false;
            gpuPlaybackReconTextureRetainedDeviceBayer16 = nullptr;
            gpuPlaybackReconTextureRetainedDeviceWidth = 0;
            gpuPlaybackReconTextureRetainedDeviceHeight = 0;
            gpuPlaybackReconTextureRetainedDeviceToken = 0;
            gpuPlaybackReconTextureBayerFrame.clear();
            gpuPlaybackReconTextureState = GpuPlaybackReconTextureState();
            gpuPlaybackReconTextureWidth = 0;
            gpuPlaybackReconTextureHeight = 0;
            gpuPlaybackReconTextureBlackLevel = 0;
            gpuPlaybackReconTextureWbMultipliers = {{1.0, 1.0, 1.0}};
            dualIsoPreviewHistogramMs = 0.0;
            dualIsoPreviewRegressionMs = 0.0;
            dualIsoPreviewRowscaleMs = 0.0;
            frameReadyEmitStageTime = 0.0;
            processedFrame8Active = false;
            processedFrame8Signature = 0;
            processedFrame16Active = false;
            processedFrame16Signature = 0;
            dualIsoPattern = 0;
            dualIsoAutoCorrection = 0;
            dualIsoEvCorrection = 0.0;
            dualIsoBlackDelta = 0;
            presentationContext = ReadyFrame::PresentationContext();
            stageTimingTelemetry = QJsonObject();
            queuedRequest = RenderRequest();
        }
    };

    mutable QMutex m_mutex;
    QWaitCondition m_waitCondition;
    struct FrameSlotStorage
    {
        std::unique_ptr<FrameSlot[]> frames;
        int slotCount = 0;

        void reset( int count )
        {
            frames.reset( new FrameSlot[count] );
            slotCount = count;
        }

        int size( void ) const { return slotCount; }
        FrameSlot *begin( void ) { return frames.get(); }
        FrameSlot *end( void ) { return frames ? frames.get() + slotCount : nullptr; }
        const FrameSlot *begin( void ) const { return frames.get(); }
        const FrameSlot *end( void ) const { return frames ? frames.get() + slotCount : nullptr; }
        FrameSlot &operator[]( int index ) { return frames[index]; }
        const FrameSlot &operator[]( int index ) const { return frames[index]; }
    };

    static constexpr int kDefaultFrameSlotCount = 4;
    static constexpr int kMaxFrameSlotCount = 12;
    static constexpr int kRenderRequestQueueDepth = 4;
    static constexpr size_t kGpuPlaybackReconTextureLutCacheSlots = 8;
    mlvObject_t *m_pMlvObject;
    bool m_initialized;
    bool m_stop;
    bool m_renderFrame;
    bool m_renderingFrame;
    bool m_frameReady;
    std::deque<RenderRequest> m_renderRequests;
    OutputMode m_activeOutputMode;
    bool m_activeUseGpuBilinearDebayer;
    bool m_activeUseGpuAmazeDebayer;
    uint32_t m_activeFrameNumber;
    uint64_t m_activeFrameRequestSerial;
    ReadyFrame::PresentationContext m_activePresentationContext;
    PresentationPreparationOptions m_activePresentationPreparationOptions;
    RenderRequest m_activeRenderRequest;
    int m_activeQueuedPlaybackDropCount;
    bool m_loggedGpuBilinearSuccess;
    bool m_loggedGpuAmazeSuccess;
    bool m_lastFrameUsedGpuBilinearDebayer;
    QString m_lastGpuBilinearFallbackReason;
    QString m_lastGpuBilinearRendererDescription;
    bool m_lastFrameUsedGpuAmazeDebayer;
    QString m_lastGpuAmazeFallbackReason;
    QString m_lastGpuAmazeRendererDescription;
    double m_lastDualIsoPreviewHistogramMs;
    double m_lastDualIsoPreviewRegressionMs;
    double m_lastDualIsoPreviewRowscaleMs;
    double m_activeFrameRequestStageTime;
    double m_lastRenderThreadQueueWaitMs;
    double m_lastRenderThreadWorkMs;
    double m_lastRenderThreadTotalMs;
    double m_lastFrameReadyEmitStageTime;
    QJsonObject m_lastStageTimingTelemetry;
    int m_lastLoggedPlaybackScaleFactorRequest = 0;
    int m_lastLoggedPlaybackScaleFactorActive = 0;
    int m_imageWidth;
    int m_imageHeight;
    int m_renderingSlotIndex;
    int m_presentingSlotIndex;
    std::atomic<Phase3Mode> m_phase3Mode;
    FrameSlotStorage m_frameSlots;
    FastPlaybackScaleCache m_playbackScaleCache;
    BilinearPlaybackScaleCache m_playbackBilinearScaleCache;
    CubicPlaybackScaleCache m_playbackCubicScaleCache;
    std::vector<float> m_gpuBilinearDebayerRawFrame;
    std::vector<float> m_gpuAmazeDebayerRawFrame;
    std::vector<uint16_t> m_gpuPlaybackReconStateRgb16;
    std::array<GpuPlaybackReconTextureState::LutCacheEntry,
               kGpuPlaybackReconTextureLutCacheSlots>
        m_gpuPlaybackReconTextureLutCache;
    uint64_t m_gpuPlaybackReconTextureLutCacheUseCounter = 0;
    std::atomic<uint64_t> m_gpuPlaybackReconTextureLutSnapshotGeneration{1};
    DecodeWorker *m_decodeWorker;
    ReconWorker *m_reconWorker;
    bool m_decodeWorkerStop;
    bool m_reconWorkerStop;
    std::deque<DecodeQueueEntry> m_decodeRequests;
    std::atomic<uint64_t> m_decodeRequestsIssuedCount{ 0 };
    std::deque<ReconQueueEntry> m_reconRequests;
    std::deque<int> m_decodeReadySlots;
    std::deque<int> m_processReadySlots;
    QWaitCondition m_decodeWaitCondition;
    QWaitCondition m_reconWaitCondition;

    static int configuredFrameSlotCount( void );
    void run( void );
    void runSerial( void );
    void runPhase3( void );
    bool phase3DecodeAheadActive( Phase3Mode mode ) const;
    void ensureDecodeWorkerStartedLocked( void );
    void stopDecodeWorkerLocked( void );
    void ensureReconWorkerStartedLocked( void );
    void stopReconWorkerLocked( void );
    bool phase3WorkInFlightLocked( void ) const;
    void queueDecodeRequestLocked( int slotIndex, const RenderRequest &request );
    bool takeDecodeRequestForWorker( DecodeQueueEntry *entry );
    void decodeFrameForWorker( const DecodeQueueEntry &entry );
    void signalDecodeDoneFromWorker( int slotIndex );
    bool takeReconRequestForWorker( ReconQueueEntry *entry );
    void reconFrameForWorker( const ReconQueueEntry &entry, llrawprocWorkerState_t *workerState );
    void signalReconDoneFromWorker( int slotIndex );
    int waitForDecodedSlotLocked( void );
    int waitForProcessReadySlotLocked( void );
    void setupActiveRequestLocked( const RenderRequest &request, int slotIndex );
    void renderDecodedSlot( int slotIndex, const RenderRequest &request, Phase3Mode activePhase3Mode );
    void publishRenderedSlot( int slotIndex, const RenderRequest &request, Phase3Mode activePhase3Mode );
    void transitionSlotState( int slotIndex,
                              SlotState from,
                              SlotState to,
                              Phase3Mode mode,
                              uint32_t frameNumber,
                              uint64_t requestSerial,
                              const char * context );
    void emitPhase3StageTelemetry( const RenderRequest &request,
                                   const FrameSlot &slot,
                                   int slotIndex,
                                   Phase3Mode mode ) const;
    void drawFrame( int slotIndex,
                    const uint16_t *decodedRawFrame = nullptr,
                    bool decodedRawFrameAlreadyReconned = false );
    bool acquireReadySlotLocked( ReadyFrame *frame,
                                 int readySlotIndex,
                                 bool discardOlderReady );
    int findLatestReadySlotLocked( void ) const;
    int findOldestReadySlotLocked( void ) const;
    int findFreeSlotLocked( void ) const;
    void releaseSlotLocked( int slotIndex );
    void copySlotTelemetryLocked( const FrameSlot &slot );

    friend class DecodeWorker;
    friend class ReconWorker;
};

#endif // RENDERFRAMETHREAD_H
