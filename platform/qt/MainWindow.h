/*!
 * \file MainWindow.h
 * \author masc4ii
 * \copyright 2017
 * \brief The main window
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileDialog>
#include <QDebug>
#include <QTimerEvent>
#include <QResizeEvent>
#include <QFileOpenEvent>
#include <QThreadPool>
#include <QProcess>
#include <QVector>
#include <QJsonObject>
#include <QImage>
#include <QPixmap>
#include <QGraphicsPixmapItem>
#include <QCloseEvent>
#include <QXmlStreamWriter>
#include <QActionGroup>
#include <QSortFilterProxyModel>
#include <QItemSelectionModel>
#include <QToolButton>
#include "SessionModel.h"
#include "../../src/mlv_include.h"
#include "InfoDialog.h"
#include "StatusDialog.h"
#include "AudioWave.h"
#include "ReceiptSettings.h"
#include "AudioPlayback.h"
#include "GraphicsPickerScene.h"
#include "MainWindowGpuPreviewPolicy.h"
#include "GpuPreviewProcessing.h"
#include "RenderFrameThread.h"
#include "ClipLifecycleBarrier.h"
#include "GradientElement.h"
#include "CrossElement.h"
#include "TimeCodeLabel.h"
#include "DoubleClickLabel.h"
#include "Scripting.h"
#include "ReceiptCopyMaskDialog.h"
#include "QRecentFilesMenu.h"
#include "PlaybackQualityPolicy.h"
#include "batch/BatchTypes.h"
#include <atomic>
#include <deque>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace Ui {
class MainWindow;
}

class QAction;
class QElapsedTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(int &argc, char **argv, QWidget *parent = 0);
    ~MainWindow();

    enum class PlaybackProfileScope
    {
        None = 0,
        Histogram,
        Waveform,
        Parade,
        Vectorscope
    };

    enum class PlaybackProfileDebayerRequest
    {
        Auto = 0,
        Receipt,
        None,
        Simple,
        Bilinear,
        LMMSE,
        IGV,
        AMaZE,
        AHD,
        RCD,
        DCB,
        AmazeCached
    };

    enum class PlaybackProfileProcessingRequest
    {
        Auto = 0,
        Receipt,
        Subset
    };

    struct PlaybackProfileOptions
    {
        QString inputPath;
        QString receiptPath;
        QString outputPath;
        int startFrame = 0;
        int frameCount = 0;
        int frameStep = 1;
        int workerThreads = 1;
        bool forceWorkerThreads = true;
        uint64_t rawCacheMB = 0;
        int cacheCpuCores = 1;
        bool zebras = false;
        bool fastOpen = false;
        bool showWindow = false;
        bool waitForPaint = false;
        bool exercisePlayAction = false;
        bool exerciseLookAssistSettle = false;
        bool exerciseLookAssistToggle = false;
        bool exerciseScaleFactorToggle = false;
        int exerciseScaleFactorToggleFrom = 2;
        PlaybackProfileScope scope = PlaybackProfileScope::None;
        PlaybackProfileDebayerRequest playbackDebayer =
            PlaybackProfileDebayerRequest::Auto;
        PlaybackProfileProcessingRequest playbackProcessing =
            PlaybackProfileProcessingRequest::Auto;
        GpuPreviewProcessingBackendRequest gpuPreviewProcessingBackend =
            GpuPreviewProcessingBackendRequest::Auto;
        GpuBilinearDebayerBackendRequest gpuBilinearDebayerBackend =
            GpuBilinearDebayerBackendRequest::Auto;
        GpuAmazeDebayerBackendRequest gpuAmazeDebayerBackend =
            GpuAmazeDebayerBackendRequest::Auto;
    };

    struct GuiPlaybackSmokeOptions
    {
        QString inputPath;
        QString receiptPath;
        int startFrame = 0;
        int durationMs = 8000;
        int targetPresentedFrames = 0; // test-only: stop after exactly N fresh presentations; durationMs remains the timeout
        bool loopPlayback = false;   // --loop: loop the clip so a SHORT clip plays the whole durationMs
                                     // window (else it plays once, stops, and the wait loop exits early).
        int settleMs = 2500;
        double settleCpuPercent = -1.0;
        int settleCpuStableMs = 1000;
        int settleCpuMaxMs = 45000;
        QString screenshotOutputPath;
        QString windowScreenshotOutputPath;
        PlaybackProfileScope scope = PlaybackProfileScope::None;
        PlaybackProfileDebayerRequest playbackDebayer =
            PlaybackProfileDebayerRequest::Auto;
        PlaybackProfileProcessingRequest playbackProcessing =
            PlaybackProfileProcessingRequest::Subset;
        GpuPreviewProcessingBackendRequest gpuPreviewProcessingBackend =
            GpuPreviewProcessingBackendRequest::Auto;
        GpuBilinearDebayerBackendRequest gpuBilinearDebayerBackend =
            GpuBilinearDebayerBackendRequest::Auto;
        GpuAmazeDebayerBackendRequest gpuAmazeDebayerBackend =
            GpuAmazeDebayerBackendRequest::Auto;
        bool forceScope = false;
        bool forcePlaybackDebayer = false;
        bool disableLookAssist = false;
        bool zebras = false;
        bool forceZebras = false;
        bool dropFrame = true;
        bool forceDropFrame = false;
        bool exerciseClipLifecycleStress = false;
        QString stressSwitchInputPath;
        int stressSwitchAtMs = 1000;
        int stressSeekFrame = 8;
    };

    int runHeadlessPlaybackProfile(const PlaybackProfileOptions & options);
    int runGuiPlaybackSmoke(const GuiPlaybackSmokeOptions & options);

protected:
    void timerEvent( QTimerEvent *t );
    void resizeEvent( QResizeEvent *event );
    bool event( QEvent *event );
    void dragEnterEvent( QDragEnterEvent *event );
    void dropEvent( QDropEvent *event );
    void closeEvent( QCloseEvent *event );
    bool eventFilter(QObject *watched, QEvent *event);

signals:
    void frameReady( void );

private slots:
    void openMlvSet( QStringList list );
    void timerFrameEvent( bool predictivePlaybackAdvance = false );
    void on_actionOpen_triggered();
    /* Open a folder of CinemaDNG frames as a clip (added programmatically). */
    void openDngFolderDialog();
    void on_actionTranscodeAndImport_triggered();
    void on_actionFcpxmlImportAssistant_triggered();
    void on_actionFcpxmlSelectionAssistant_triggered();
    void on_actionAbout_triggered();
    void on_actionAboutQt_triggered();
    void on_horizontalSliderPosition_valueChanged(int position);
    void on_actionClip_Information_triggered();
    void on_horizontalSliderGamma_valueChanged(int position);
    void on_horizontalSliderExposure_valueChanged(int position);
    void on_horizontalSliderExposureGradient_valueChanged(int position);
    void on_horizontalSliderContrast_valueChanged(int position);
    void on_horizontalSliderPivot_valueChanged(int position);
    void on_horizontalSliderContrastGradient_valueChanged(int position);
    void on_horizontalSliderTemperature_valueChanged(int position);
    void on_horizontalSliderTint_valueChanged(int position);
    void on_horizontalSliderClarity_valueChanged(int position);
    void on_horizontalSliderVibrance_valueChanged(int position);
    void on_horizontalSliderSaturation_valueChanged(int position);
    void on_horizontalSliderDS_valueChanged(int position);
    void on_horizontalSliderDR_valueChanged(int position);
    void on_horizontalSliderLS_valueChanged(int position);
    void on_horizontalSliderLR_valueChanged(int position);
    void on_horizontalSliderLighten_valueChanged(int position);
    void on_horizontalSliderShadows_valueChanged(int position);
    void on_horizontalSliderHighlights_valueChanged(int position);
    void on_horizontalSliderSharpen_valueChanged(int position);
    void on_horizontalSliderShMasking_valueChanged(int position);
    void on_horizontalSliderChromaBlur_valueChanged(int position);
    void on_horizontalSliderDenoiseStrength_valueChanged(int position);
    void on_horizontalSliderRbfDenoiseLuma_valueChanged(int position);
    void on_horizontalSliderRbfDenoiseChroma_valueChanged(int position);
    void on_horizontalSliderRbfDenoiseRange_valueChanged(int position);
    void on_horizontalSliderGrainStrength_valueChanged(int position);
    void on_horizontalSliderGrainLumaWeight_valueChanged(int position);
    void on_horizontalSliderLutStrength_valueChanged(int position);
    void on_horizontalSliderFilterStrength_valueChanged(int position);
    void on_horizontalSliderVignetteStrength_valueChanged(int position);
    void on_horizontalSliderVignetteRadius_valueChanged(int position);
    void on_horizontalSliderVignetteShape_valueChanged(int position);
    void on_horizontalSliderCaRed_valueChanged(int position);
    void on_horizontalSliderCaBlue_valueChanged(int position);
    void on_horizontalSliderCaDesaturate_valueChanged(int position);
    void on_horizontalSliderCaRadius_valueChanged(int position);
    void on_horizontalSliderRawWhite_valueChanged(int position);
    void on_horizontalSliderRawBlack_valueChanged(int position);
    void on_horizontalSliderDualIsoEvCorrection_valueChanged(int position);
    void on_horizontalSliderDualIsoBlackDelta_valueChanged(int position);
    void on_horizontalSliderTone_valueChanged(int position);
    void on_horizontalSliderToningStrength_valueChanged(int position);
    void on_horizontalSliderVidstabStepsize_valueChanged(int position);
    void on_horizontalSliderVidstabShakiness_valueChanged(int position);
    void on_horizontalSliderVidstabAccuracy_valueChanged(int position);
    void on_horizontalSliderVidstabZoom_valueChanged(int position);
    void on_horizontalSliderVidstabSmoothing_valueChanged(int position);

    void on_horizontalSliderExposure_doubleClicked();
    void on_horizontalSliderExposureGradient_doubleClicked();
    void on_horizontalSliderContrast_doubleClicked();
    void on_horizontalSliderPivot_doubleClicked();
    void on_horizontalSliderContrastGradient_doubleClicked();
    void on_horizontalSliderTemperature_doubleClicked();
    void on_horizontalSliderTint_doubleClicked();
    void on_horizontalSliderClarity_doubleClicked();
    void on_horizontalSliderVibrance_doubleClicked();
    void on_horizontalSliderSaturation_doubleClicked();
    void on_horizontalSliderDS_doubleClicked();
    void on_horizontalSliderDR_doubleClicked();
    void on_horizontalSliderLS_doubleClicked();
    void on_horizontalSliderLR_doubleClicked();
    void on_horizontalSliderLighten_doubleClicked();
    void on_horizontalSliderShadows_doubleClicked();
    void on_horizontalSliderHighlights_doubleClicked();
    void on_horizontalSliderSharpen_doubleClicked();
    void on_horizontalSliderShMasking_doubleClicked();
    void on_horizontalSliderChromaBlur_doubleClicked();
    void on_horizontalSliderDenoiseStrength_doubleClicked();
    void on_horizontalSliderRbfDenoiseLuma_doubleClicked();
    void on_horizontalSliderRbfDenoiseChroma_doubleClicked();
    void on_horizontalSliderRbfDenoiseRange_doubleClicked();
    void on_horizontalSliderGrainStrength_doubleClicked();
    void on_horizontalSliderGrainLumaWeight_doubleClicked();
    void on_horizontalSliderLutStrength_doubleClicked();
    void on_horizontalSliderFilterStrength_doubleClicked();
    void on_horizontalSliderVignetteStrength_doubleClicked();
    void on_horizontalSliderVignetteRadius_doubleClicked();
    void on_horizontalSliderVignetteShape_doubleClicked();
    void on_horizontalSliderCaRed_doubleClicked();
    void on_horizontalSliderCaBlue_doubleClicked();
    void on_horizontalSliderCaDesaturate_doubleClicked();
    void on_horizontalSliderCaRadius_doubleClicked();
    void on_horizontalSliderRawWhite_doubleClicked();
    void on_horizontalSliderRawBlack_doubleClicked();
    void on_horizontalSliderDualIsoEvCorrection_doubleClicked();
    void on_horizontalSliderDualIsoBlackDelta_doubleClicked();
    void on_horizontalSliderTone_doubleClicked();
    void on_horizontalSliderToningStrength_doubleClicked();
    void on_horizontalSliderVidstabStepsize_doubleClicked();
    void on_horizontalSliderVidstabShakiness_doubleClicked();
    void on_horizontalSliderVidstabAccuracy_doubleClicked();
    void on_horizontalSliderVidstabZoom_doubleClicked();
    void on_horizontalSliderVidstabSmoothing_doubleClicked();

    void on_actionGoto_First_Frame_triggered();
    void on_actionExport_triggered();
    void on_actionExportCurrentFrame_triggered();
    void on_checkBoxHighLightReconstruction_toggled(bool checked);
    void on_checkBoxLookAssistEnable_clicked(bool checked);
    void on_comboBoxUseCameraMatrix_currentIndexChanged(int index);
    void on_checkBoxCreativeAdjustments_toggled(bool checked);
    void on_checkBoxExrMode_toggled(bool checked);
    void on_checkBoxAgX_toggled(bool checked);
    void on_checkBoxChromaSeparation_toggled(bool checked);
    void on_comboBoxProfile_currentIndexChanged(int index);
    void on_comboBoxProfile_activated(int index);
    void on_comboBoxTonemapFct_currentIndexChanged(int index);
    void on_comboBoxProcessingGamut_currentIndexChanged(int index);
    void on_comboBoxFilterName_currentIndexChanged(int index);
    void on_comboBoxDenoiseWindow_currentIndexChanged(int index);
    void on_actionZoomFit_triggered(bool on);
    void on_actionZoom100_triggered();
    void on_actionShowHistogram_triggered(void);
    void on_actionShowWaveFormMonitor_triggered(void);
    void on_actionShowParade_triggered(void);
    void on_actionShowVectorScope_triggered(void);
    void on_actionUseNoneDebayer_triggered();
    void on_actionUseSimpleDebayer_triggered();
    void on_actionUseBilinear_triggered();
    void on_actionUseLmmseDebayer_triggered();
    void on_actionUseIgvDebayer_triggered();
    void on_actionUseAhdDebayer_triggered();
    void on_actionUseRcdDebayer_triggered();
    void on_actionUseDcbDebayer_triggered();
    void on_actionAlwaysUseAMaZE_triggered();
    void on_actionCaching_triggered();
    void on_actionDontSwitchDebayerForPlayback_triggered();
    void on_actionUseFastProcessingForPlayback_triggered();
    /* Phase 4E: Playback Quality dial (Fast / HighQuality / Auto). */
    void on_actionPlaybackQualityFast_triggered();
    void on_actionPlaybackQualityHQ_triggered();
    void on_actionPlaybackQualityAuto_triggered();
    void on_actionPlaybackQualityPhase3Fast_triggered();
    void on_actionPlaybackQualityPhase3HQ_triggered();
    void on_actionPlaybackPreviewSharpSmooth_triggered();
    void on_actionPlaybackPreviewAggressive_triggered();
    void on_actionPlaybackPreviewResAuto_triggered();
    void on_actionPlaybackPreviewResFull_triggered();
    void on_actionPlaybackPreviewResHalf_triggered();
    void on_actionPlaybackPreviewResQuarter_triggered();
    void on_actionPlaybackShowQualityIndicator_triggered();
    void on_actionPlaybackAutoTarget24_triggered();
    void on_actionPlaybackAutoTarget30_triggered();
    void on_actionPlaybackAutoTarget60_triggered();
    void cyclePlaybackQualityMode();
    void showPlaybackQualityContextMenu( const QPoint &pos );
    void on_actionExportSettings_triggered();
    void on_actionResetReceipt_triggered();
    void on_actionCopyRecept_triggered();
    void on_actionPasteReceipt_triggered();
    void on_actionNewSession_triggered();
    void on_actionOpenSession_triggered();
    void on_actionSaveSession_triggered();
    void on_actionSaveAsSession_triggered();
    void on_actionSaveSessionMetadata_triggered();
    void on_actionImportReceipt_triggered();
    void on_actionExportReceipt_triggered();
    void on_actionUseDefaultReceipt_triggered(bool checked);
    void on_actionNext_Clip_triggered();
    void on_actionPrevious_Clip_triggered();
    void on_actionSelectAllClips_triggered();
    void on_actionDeleteSelectedClips_triggered();
    void on_actionHelp_triggered();
    void on_actionCreateAllMappFilesNow_triggered();
    void on_actionBetterResizer_triggered();
    void on_actionShowInstalledFocusPixelMaps_triggered();
    void on_actionShowInstalledBadPixelMaps_triggered();
    void on_actionViewerBackgroundColor_triggered();
    void on_listViewSession_activated(const QModelIndex &index);
    void on_tableViewSession_activated(const QModelIndex &index);
    void on_dockWidgetSession_visibilityChanged(bool visible);
    void on_dockWidgetEdit_visibilityChanged(bool visible);
    void on_actionShowAudioTrack_toggled(bool checked);
    void on_listViewSession_customContextMenuRequested(const QPoint &pos);
    void on_tableViewSession_customContextMenuRequested(const QPoint &pos);
    void deleteFileFromSession( void );
    void renameActiveClip( void );
    void on_actionShowInFinder_triggered( void );
    void on_actionOpenWithExternalApplication_triggered( void );
    void rightClickShowFile( void );
    void selectAllFiles( void );
    void pictureCustomContextMenuRequested(const QPoint &pos);
    void on_labelScope_customContextMenuRequested(const QPoint &pos);
    void on_label_GammaVal_doubleClicked( void );
    void on_label_ExposureVal_doubleClicked( void );
    void on_label_ExposureGradient_doubleClicked( void );
    void on_label_ContrastVal_doubleClicked( void );
    void on_label_PivotVal_doubleClicked( void );
    void on_label_ContrastGradientVal_doubleClicked( void );
    void on_label_TemperatureVal_doubleClicked( void );
    void on_label_TintVal_doubleClicked( void );
    void on_label_ClarityVal_doubleClicked( void );
    void on_label_VibranceVal_doubleClicked( void );
    void on_label_SaturationVal_doubleClicked( void );
    void on_label_DrVal_doubleClicked( void );
    void on_label_DsVal_doubleClicked( void );
    void on_label_LrVal_doubleClicked( void );
    void on_label_LsVal_doubleClicked( void );
    void on_label_LightenVal_doubleClicked( void );
    void on_label_ShadowsVal_doubleClicked( void );
    void on_label_HighlightsVal_doubleClicked( void );
    void on_label_Sharpen_doubleClicked( void );
    void on_label_ShMasking_doubleClicked( void );
    void on_label_ChromaBlur_doubleClicked( void );
    void on_label_DenoiseStrength_doubleClicked( void );
    void on_label_RbfDenoiseLuma_doubleClicked( void );
    void on_label_RbfDenoiseChroma_doubleClicked( void );
    void on_label_RbfDenoiseRange_doubleClicked( void );
    void on_label_GrainStrength_doubleClicked( void );
    void on_label_GrainLumaWeight_doubleClicked( void );
    void on_labelAudioTrack_sizeChanged( void );
    void on_label_LutStrengthVal_doubleClicked( void );
    void on_label_FilterStrengthVal_doubleClicked( void );
    void on_label_VignetteStrengthVal_doubleClicked( void );
    void on_label_VignetteRadiusVal_doubleClicked( void );
    void on_label_VignetteShapeVal_doubleClicked( void );
    void on_label_CaRedVal_doubleClicked( void );
    void on_label_CaBlueVal_doubleClicked( void );
    void on_label_CaDesaturateVal_doubleClicked( void );
    void on_label_CaRadiusVal_doubleClicked( void );
    void on_label_RawWhiteVal_doubleClicked( void );
    void on_label_RawBlackVal_doubleClicked( void );
    void on_DualIsoEvCorrectionVal_doubleClicked( void );
    void on_DualIsoBlackDeltaVal_doubleClicked( void );
    void on_label_ToneVal_doubleClicked( void );
    void on_label_ToningStrengthVal_doubleClicked( void );
    void on_label_VidstabStepsizeVal_doubleClicked( void );
    void on_label_VidstabShakinessVal_doubleClicked( void );
    void on_label_VidstabAccuracyVal_doubleClicked( void );
    void on_label_VidstabZoomVal_doubleClicked( void );
    void on_label_VidstabSmoothingVal_doubleClicked( void );
    void on_actionFullscreen_triggered(bool checked);
    void exportHandler( void );
    void on_actionPlay_triggered(bool checked);
    void on_actionPlay_toggled(bool checked);
    void on_actionShowZebras_triggered();
    void toolButtonFocusPixelsChanged( void );
    void toolButtonFocusPixelsIntMethodChanged( void );
    void toolButtonBadPixelsChanged( void );
    void toolButtonBadPixelsSearchMethodChanged( void );
    void toolButtonBadPixelsIntMethodChanged( void );
    void toolButtonChromaSmoothChanged( void );
    void toolButtonPatternNoiseChanged( void );
    void toolButtonUpsideDownChanged( void );
    void toolButtonVerticalStripesChanged( void );
    void on_spinBoxDeflickerTarget_valueChanged(int arg1);
    void toolButtonDualIsoChanged( void );
    void on_DualIsoPatternComboBox_currentIndexChanged(int index);
    void on_toolButtonDualIsoMatchExposures1_clicked();
    void on_toolButtonDualIsoMatchExposures2_clicked();
    void toolButtonDualIsoInterpolationChanged( void );
    void toolButtonDualIsoAliasMapChanged( void );
    void toolButtonDualIsoFullresBlendingChanged( void );
    void toolButtonDarkFrameSubtractionChanged( bool checked );
    void toolButtonGCurvesChanged( void );
    void on_toolButtonGCurvesReset_clicked();
    void on_toolButtonGCurvesResetOne_clicked();
    void on_toolButtonHueVsHueReset_clicked();
    void on_toolButtonHueVsHueResetDefaultPoints_clicked();
    void on_toolButtonHueVsSatReset_clicked();
    void on_toolButtonHueVsSatResetDefaultPoints_clicked();
    void on_toolButtonHueVsLumaReset_clicked();
    void on_toolButtonHueVsLumaResetDefaultPoints_clicked();
    void on_toolButtonLumaVsSatReset_clicked();
    void on_actionNextFrame_triggered();
    void on_actionPreviousFrame_triggered();
    void on_checkBoxRawFixEnable_clicked(bool checked);
    void on_checkBoxLutEnable_clicked(bool checked);
    void on_checkBoxFilterEnable_clicked(bool checked);
    void on_checkBoxVidstabEnable_toggled(bool checked);
    void on_checkBoxVidstabTripod_toggled(bool checked);
    void on_toolButtonDeleteBpm_clicked( void );
    void on_toolButtonBadPixelsSearchMethodEdit_toggled(bool checked);
    void on_toolButtonBadPixelsCrosshairEnable_toggled(bool checked);
    void badPixelPicked( int x, int y );
    void on_actionWhiteBalancePicker_toggled(bool checked);
    void whiteBalancePicked( int x, int y );
    void on_toolButtonWbMode_clicked();
    void gradientAnchorPicked( int x, int y );
    void gradientFinalPosPicked(int x, int y , bool isFinished);
    void on_groupBoxRawCorrection_toggled(bool arg1);
    void on_groupBoxCutInOut_toggled(bool arg1);
    void on_groupBoxDebayer_toggled(bool arg1);
    void on_groupBoxProfiles_toggled(bool arg1);
    void on_groupBoxProcessing_toggled(bool arg1);
    void on_groupBoxDetails_toggled(bool arg1);
    void on_groupBoxHsl_toggled(bool arg1);
    void on_groupBoxToning_toggled(bool arg1);
    void on_groupBoxColorWheels_toggled(bool arg1);
    void on_groupBoxLut_toggled(bool arg1);
    void on_groupBoxFilter_toggled(bool arg1);
    void on_groupBoxVignette_toggled(bool arg1);
    void on_groupBoxLinearGradient_toggled(bool arg1);
    void on_groupBoxTransformation_toggled(bool arg1);
    void exportAbort( void );
    void drawFrameReady( void );

    void on_toolButtonGradientPaint_toggled(bool checked);
    void on_checkBoxGradientEnable_toggled(bool checked);
    void on_spinBoxGradientX_valueChanged(int arg1);
    void on_spinBoxGradientY_valueChanged(int arg1);
    void on_spinBoxGradientLength_valueChanged(int arg1);
    void on_labelGradientAngle_doubleClicked( void );
    void on_dialGradientAngle_valueChanged(int value);
    void gradientGraphicElementMoved( int x, int y );
    void gradientGraphicElementHovered( bool isHovered );

    void on_toolButtonCutIn_clicked(void);
    void on_toolButtonCutOut_clicked(void);
    void on_toolButtonCutInDelete_clicked(void);
    void on_toolButtonCutOutDelete_clicked(void);
    void on_spinBoxCutIn_valueChanged(int arg1);
    void on_spinBoxCutOut_valueChanged(int arg1);

    void on_actionPreviewDisabled_triggered();
    void on_actionPreviewList_triggered();
    void on_actionPreviewPicture_triggered();
    void on_actionPreviewPictureBottom_triggered();
    void on_actionPreviewTableModeBottom_triggered();

    void on_comboBoxHStretch_currentIndexChanged(int index);
    void on_comboBoxVStretch_currentIndexChanged(int index);

    void mpTcLabel_customContextMenuRequested(const QPoint &pos);
    void on_actionTimecodePositionMiddle_triggered();
    void on_actionTimecodePositionRight_triggered();
    void tcLabelDoubleClicked();
    void on_actionToggleTimecodeDisplay_triggered();

    void on_toolButtonDarkFrameSubtractionFile_clicked();
    void on_lineEditDarkFrameFile_textChanged(const QString &arg1);

    void on_actionCheckForUpdates_triggered(void);
    void updateCheck(void);

    void on_toolButtonLoadLut_clicked();
    void on_toolButtonNextLut_clicked();
    void on_toolButtonPrevLut_clicked();
    void on_lineEditLutName_textChanged(const QString &arg1);

    void on_actionPlaybackScaleAuto_triggered();
    void on_actionPlaybackScale1_triggered();
    void on_actionPlaybackScale2_triggered();
    void on_actionPlaybackScale4_triggered();
    void on_actionPlaybackScale8_triggered();

    void on_actionSelectExternalApplication_triggered();
    void openRecentSession( QString fileName );

    void on_actionDarkThemeStandard_triggered(bool checked);
    void on_actionDarkThemeModern_triggered(bool checked);

    void on_comboBoxDebayer_currentIndexChanged( int index );

    void on_actionMarkRed_triggered();
    void on_actionMarkYellow_triggered();
    void on_actionMarkGreen_triggered();
    void on_actionUnmark_triggered();

    void on_actionShowRedClips_toggled(bool arg1);
    void on_actionShowYellowClips_toggled(bool arg1);
    void on_actionShowGreenClips_toggled(bool arg1);
    void on_actionShowUnmarkedClips_toggled(bool arg1);

    void on_lineEditTransferFunction_textChanged(const QString &arg1);
    void onPlaybackPrepResultReady( void );

private:
    struct DisplayPreviewCacheEntry
    {
        bool valid = false;
        bool zoomFit = false;
        bool betterResizer = false;
        bool zebras = false;
        bool gpuScaling = false;
        uint64_t frameIndex = 0;
        uint64_t signature = 0;
        int sourceWidth = 0;
        int sourceHeight = 0;
        int sceneWidth = 0;
        int sceneHeight = 0;
        int imageWidth = 0;
        int imageHeight = 0;
        int transformationMode = 0;
        int devicePixelRatioMilli = 0;
        uint8_t underOver = 0;
        QImage image;
        QPixmap pixmap;
    };

    using PresentationRequestContext = RenderFrameThread::ReadyFrame::PresentationContext;

    // Immutable inputs to the playback-prep worker. Non-owning views point
    // into the ready render slot while it is pinned as presenting; owned
    // vectors remain available for fallback paths that must detach storage.
    struct PlaybackPrepTask
    {
        RenderFrameThread::ReadyFrame readyFrame;
        PresentationRequestContext requestContext;
        uint64_t requestSerial = 0;
        uint64_t presentationGeneration = 0;
        double displayStart = 0.0;
        double enqueueTime = 0.0;
        double preEnqueueMs = 0.0;
        double workerQueueMs = 0.0;
        uint64_t displayFrame = 0;
        double stretchX = 1.0;
        double stretchY = 1.0;
        const uint8_t *scopeSourceImage = nullptr;
        size_t scopeSourceImageSize = 0;
        const uint8_t *sourceImage = nullptr;
        size_t sourceImageSize = 0;
        int sourceWidth = 0;
        int sourceHeight = 0;
        int sceneWidth = 0;
        int sceneHeight = 0;
        int transformationMode = 0;
        int devicePixelRatioMilli = 0;
        const uint16_t *sourceImage16 = nullptr;
        size_t sourceImage16Size = 0;
        const float *gpuAmazeTextureRawFrame = nullptr;
        size_t gpuAmazeTextureRawFrameSize = 0;
        const uint16_t *gpuPlaybackReconTextureBayerFrame = nullptr;
        size_t gpuPlaybackReconTextureBayerFrameSize = 0;
        const uint16_t *gpuPlaybackReconTextureInputBayerFrame = nullptr;
        size_t gpuPlaybackReconTextureInputBayerFrameSize = 0;
        const uint16_t *gpuPlaybackReconTextureRetainedDeviceBayer16 = nullptr;
        int gpuPlaybackReconTextureRetainedDeviceWidth = 0;
        int gpuPlaybackReconTextureRetainedDeviceHeight = 0;
        uint64_t gpuPlaybackReconTextureRetainedDeviceToken = 0;
        RenderFrameThread::GpuPlaybackReconTextureState gpuPlaybackReconTextureState;
        bool gpu16PreviewActive = false;
        bool gpuPreviewProcessingActive = false;
        bool cpuPreviewProcessingActive = false;
        bool useGpuImagePresentation = false;
        bool useGpuShaderZebras = false;
        bool zoomFitEnabled = false;
        bool zebrasEnabled = false;
        bool betterResizerEnabled = false;
        bool displayPreviewCachingAllowed = false;
        bool playbackFastScaleActive = false;
        GpuDisplayViewport::PresentationOptions gpuPresentationOptions;
        std::vector<uint8_t> ownedSourceImage;
        std::vector<uint16_t> ownedSourceImage16;
        std::vector<float> ownedGpuAmazeTextureRawFrame;
        std::vector<uint16_t> ownedGpuPlaybackReconTextureBayerFrame;
        std::vector<uint16_t> ownedGpuPlaybackReconTextureInputBayerFrame;
        std::vector<uint8_t> ownedPlaybackScaledImage8;

        void rebindOwnedImagePointers()
        {
            if( !ownedSourceImage.empty() )
            {
                sourceImage = ownedSourceImage.data();
                sourceImageSize = ownedSourceImage.size();
                scopeSourceImage = sourceImage;
                scopeSourceImageSize = sourceImageSize;
                readyFrame.rawImage8 = sourceImage;
            }
            if( !ownedSourceImage16.empty() )
            {
                sourceImage16 = ownedSourceImage16.data();
                sourceImage16Size = ownedSourceImage16.size() * sizeof( uint16_t );
                readyFrame.rawImage16 = sourceImage16;
                readyFrame.rawImage16Words = ownedSourceImage16.size();
            }
            if( !ownedGpuAmazeTextureRawFrame.empty() )
            {
                gpuAmazeTextureRawFrame = ownedGpuAmazeTextureRawFrame.data();
                gpuAmazeTextureRawFrameSize = ownedGpuAmazeTextureRawFrame.size();
                readyFrame.gpuAmazeTextureRawFrame = gpuAmazeTextureRawFrame;
                readyFrame.gpuAmazeTextureRawFrameSize = gpuAmazeTextureRawFrameSize;
            }
            if( !ownedGpuPlaybackReconTextureBayerFrame.empty() )
            {
                gpuPlaybackReconTextureBayerFrame =
                    ownedGpuPlaybackReconTextureBayerFrame.data();
                gpuPlaybackReconTextureBayerFrameSize =
                    ownedGpuPlaybackReconTextureBayerFrame.size();
                readyFrame.gpuPlaybackReconTextureBayerFrame =
                    gpuPlaybackReconTextureBayerFrame;
                readyFrame.gpuPlaybackReconTextureBayerFrameSize =
                    gpuPlaybackReconTextureBayerFrameSize;
            }
            if( !ownedGpuPlaybackReconTextureInputBayerFrame.empty() )
            {
                gpuPlaybackReconTextureInputBayerFrame =
                    ownedGpuPlaybackReconTextureInputBayerFrame.data();
                gpuPlaybackReconTextureInputBayerFrameSize =
                    ownedGpuPlaybackReconTextureInputBayerFrame.size();
                readyFrame.gpuPlaybackReconTextureInputBayerFrame =
                    gpuPlaybackReconTextureInputBayerFrame;
                readyFrame.gpuPlaybackReconTextureInputBayerFrameSize =
                    gpuPlaybackReconTextureInputBayerFrameSize;
            }
            if( !ownedPlaybackScaledImage8.empty() )
            {
                readyFrame.playbackScaledImage8 = ownedPlaybackScaledImage8.data();
            }
        }
    };

    // Worker output. Composes the originating Task so the presenter has the
    // full input context without field duplication.
    struct PlaybackPrepResult
    {
        PlaybackPrepTask task;
        int preparedWidth = 0;
        int preparedHeight = 0;
        // Bytes per scanline in preparedImage. Always rounded up to a
        // multiple of 4 so the buffer is safe to wrap as a Format_RGB888
        // QImage on the GUI thread (Qt requires 32-bit-aligned scanlines;
        // an unpadded width*3 stride for odd widths overshoots the buffer
        // by up to one row inside qt_convert_rgb888_to_rgb32_ssse3 and
        // segfaults — see the 2026-04-24 crash investigation).
        int preparedBytesPerLine = 0;
        const uint8_t *preparedBorrowedImage = nullptr;
        size_t preparedBorrowedImageSize = 0;
        bool preparedImageMoved = false;
        uint8_t underOver = 0;
        double imageBuildMs = 0.0;
        double workerQueueMs = 0.0;
        double workerTotalMs = 0.0;
        double resultReadyTime = 0.0;
        QImage preparedOwnedImage;
        std::vector<uint8_t> preparedImage;
        std::vector<uint8_t> scopeSourceImage;
    };

    Ui::MainWindow *ui;
    InfoDialog *m_pInfoDialog;
    StatusDialog *m_pStatusDialog;
    AudioWave *m_pAudioWave;
    AudioPlayback *m_pAudioPlayback;
    RenderFrameThread *m_pRenderThread;
    mlvObject_t *m_pMlvObject = nullptr;
    processingObject_t *m_pProcessingObject;
    QGraphicsPixmapItem *m_pGraphicsItem;
    GradientElement *m_pGradientElement;
    QVector<CrossElement*> m_pBadPixelCrosses;
    bool m_badPixelCrosshairVisible = false;
    GraphicsPickerScene* m_pScene;
    TimeCodeLabel* m_pTimeCodeImage;
    ReceiptCopyMaskDialog *m_pCopyMask;
    Scripting* m_pScripting;
    uint8_t m_timeCodePosition;
    QLabel *m_pCachingStatus;
    QLabel *m_pFpsStatus;
    QString m_lastPlaybackFpsStatusText;
    double m_playbackFpsEmaFrameMs = 0.0;
    QTime m_lastPlaybackFpsStatusUpdateTime;
    QLabel *m_pFrameNumber;
    QLabel *m_pChosenDebayer;
    QLabel *m_pPlaybackQualityIndicator = nullptr; // Phase 4E
    // Phase 4F-toolbar: Playback Quality dropdown on the main toolbar. The
    // button is a second view onto the same Playback Quality QActions used
    // by the Playback -> Playback Quality menu and the Q shortcut.
    QToolButton *m_pPlaybackQualityToolButton = nullptr;
    QMenu *m_pPlaybackQualityToolButtonMenu = nullptr;
    int m_playbackScaleFactorOverride = 0;
    QActionGroup *m_darkFrameGroup;
    QActionGroup *m_previewDebayerGroup;
    QActionGroup *m_sessionListGroup;
    QActionGroup *m_playbackElementGroup;
    QActionGroup *m_scopeGroup;
    QActionGroup *m_playbackQualityGroup = nullptr;       // Phase 4E
    QActionGroup *m_playbackPreviewModeGroup = nullptr;   // Phase 4E
    QActionGroup *m_playbackPreviewResolutionGroup = nullptr; // Round-3 item 1
    QActionGroup *m_playbackAutoTargetFpsGroup = nullptr; // Phase 4E
    QActionGroup *m_playbackScaleFactorGroup = nullptr;   // Phase 4E
    DoubleClickLabel *m_pTcLabel;
    bool m_tcModeDuration;
    int m_lastTimeCodeFrameIndex = -1;
    bool m_lastTimeCodeDurationMode = false;
    double m_lastTimeCodeFps = -1.0;
    uint8_t *m_pRawImage;
    uint16_t *m_pRawImage16;
    uint32_t m_cacheSizeMB;
    uint8_t m_codecProfile;
    uint8_t m_codecOption;
    uint8_t m_exportDebayerMode;
    uint8_t m_previewMode;
    uint8_t m_wbMode;
    bool m_frameChanged;
    int m_currentFrameIndex;
    double m_newPosDropMode;
    bool m_dontDraw;
    bool m_frameStillDrawing;
    bool m_fileLoaded = false;
    bool m_inOpeningProcess;
    bool m_setSliders;
    int m_timerId;
    int m_timerCacheId;
    int8_t m_countTimeDown;
    bool m_resizeFilterEnabled;
    bool m_resizeFilterHeightLocked;
    uint8_t m_smoothFilterSetting;
    uint16_t m_resizeWidth;
    uint16_t m_resizeHeight;
    bool m_fpsOverride;
    double m_frameRate;
    bool m_tryToSyncAudio;
    bool m_audioExportEnabled;
    bool m_hdrExport;
    bool m_exportAbortPressed;
    bool m_zoomTo100Center;
    bool m_zoomModeChanged;
    bool m_playbackStopped;
    bool m_dualIsoPlaybackPreviewActive;
    /* Phase 4E: Playback Quality state. m_playbackQualityMode reflects the
     * persisted user choice; m_playbackQualityActiveScale and
     * m_playbackQualityActiveHq reflect the *effective* state for the next
     * frame (equal to the user choice for Fast/HighQuality, dynamically
     * decided by the auto sampler for Auto). */
    int m_playbackQualityMode = 0;
    int m_playbackPreviewMode = 0;
    int m_playbackPreviewResolution = 0; // Round-3 item 1: PlaybackPreviewResolution
    int m_playbackAutoTargetFps = 30;
    int m_playbackQualityActiveScale = 1;
    bool m_playbackQualityActiveHq = false;
    PlaybackQualityAutoDecisionReason m_playbackQualityAutoDecisionReason =
        PlaybackQualityAutoDecisionReason::WarmupHq;
    double m_playbackQualityAutoDecisionAverageMs = 0.0;
    double m_playbackQualityAutoDecisionBudgetMs = 1000.0 / 30.0;
    size_t m_playbackQualityAutoDecisionSampleCount = 0;
    bool m_playbackQualityAutoHeadroomCapability = false;
    PlaybackQualityAutoCapabilityTracker m_playbackQualityAutoCapabilityTracker;
    bool m_playbackQualityIndicatorVisible = true;
    struct PlaybackQualityIndicatorCache
    {
        int playbackQualityMode = -1;
        int playbackAutoTargetFps = -1;
        int playbackScaleFactorOverride = -1;
        int playbackQualityActiveScale = -1;
        bool playbackQualityActiveHq = false;
        int playbackQualityAutoDecisionReason = -1;
        double playbackQualityAutoDecisionAverageMs = -1.0;
        double playbackQualityAutoDecisionBudgetMs = -1.0;
        size_t playbackQualityAutoDecisionSampleCount = static_cast<size_t>( -1 );
        bool playbackQualityAutoHeadroomCapability = false;
        bool playbackQualityAutoValidatedNoReadbackCapability = false;
        bool playbackQualityAutoValidatedNoReadbackDemoted = false;
        int envScale = -2;
        bool envHq = false;
        int envPreviewOverride = -2;
        bool aggressivePreviewActive = false;
        int lastPresentedPlaybackScaleFactorActive = -1;
        bool lastPresentedRequestContextValid = false;
        int lastPresentedRequestScaleFactor = -1;
        int phase3Tier = -1;
        int gpuPlaybackPipelineStatus = -1;
        bool scopePerformanceCaveat = false;
    };
    PlaybackQualityIndicatorCache m_playbackQualityIndicatorCache;
    bool m_playbackQualityIndicatorCacheValid = false;
    bool m_scopePerformanceWarningShown = false;
    struct DualIsoPlaybackUiCache
    {
        int toolMode = -1;
        int pattern = -2;
        int autoCorrection = 0;
        double evCorrection = 0.0;
        int blackDelta = 0;
    };
    DualIsoPlaybackUiCache m_dualIsoPlaybackUiCache;
    bool m_dualIsoPlaybackUiCacheValid = false;
    uint64_t m_playbackQualityFrameCounter = 0;
    double m_playbackQualityLastPresentedTime = 0.0;
    PlaybackQualityAutoSampler m_playbackQualitySampler;
    bool m_lastLookAssistDiagnosticsValid = false;
    QString m_lastLookAssistScene;
    double m_lastLookAssistMedian = 0.0;
    double m_lastLookAssistP05 = 0.0;
    double m_lastLookAssistP95 = 0.0;
    double m_lastLookAssistP99 = 0.0;
    double m_lastLookAssistMedianR = 0.0;
    double m_lastLookAssistMedianG = 0.0;
    double m_lastLookAssistMedianB = 0.0;
    double m_lastLookAssistBalanceR = 0.0;
    double m_lastLookAssistBalanceG = 0.0;
    double m_lastLookAssistBalanceB = 0.0;
    int m_lastLookAssistBalanceSamples = 0;
    QString m_lastLookAssistBalanceSource;
    int m_lastLookAssistExposure = 0;
    int m_lastLookAssistContrast = 0;
    int m_lastLookAssistPivot = 75;
    int m_lastLookAssistShadows = 0;
    int m_lastLookAssistHighlights = 0;
    int m_lastLookAssistVibrance = 0;
    int m_lastLookAssistTemperatureDelta = 0;
    int m_lastLookAssistTintDelta = 0;
    bool m_lastLookAssistAutoWhiteBalanceValid = false;
    QString m_lastLookAssistAutoWhiteBalanceSource;
    int m_lastLookAssistAutoWhiteBalanceTemperature = 0;
    int m_lastLookAssistAutoWhiteBalanceTint = 0;
    QString m_lastLookAssistAutoWhiteBalanceDecision;
    double m_lastLookAssistAutoWhiteBalanceDamping = 1.0;
    int m_lastLookAssistAutoWhiteBalanceCandidateTemperature = 0;
    int m_lastLookAssistAutoWhiteBalanceCandidateTint = 0;
    int m_lastLookAssistAutoWhiteBalanceRawX = -1;
    int m_lastLookAssistAutoWhiteBalanceRawY = -1;
    double m_lastLookAssistAutoWhiteBalanceLuma = 0.0;
    double m_lastLookAssistAutoWhiteBalanceChroma = 0.0;
    QString m_lastLookAssistColorCastWarning;
    bool m_lastLookAssistSafetyFallback = false;
    bool m_lastLookAssistPostBalanceValid = false;
    double m_lastLookAssistPostBalanceR = 0.0;
    double m_lastLookAssistPostBalanceG = 0.0;
    double m_lastLookAssistPostBalanceB = 0.0;
    int m_lastLookAssistPostBalanceSamples = 0;
    double m_lastLookAssistPostGreenArtifactRatio = 0.0;
    double m_lastLookAssistPostGreenArtifactMeanAxis = 0.0;
    double m_lastLookAssistPostVisibleGreenAxis = 0.0;
    int m_lastLookAssistPostTemperatureDelta = 0;
    int m_lastLookAssistPostTintDelta = 0;
    int m_lastLookAssistChromaSmooth = 0;
    bool m_lastLookAssistChromaSmoothAutoApplied = false;
    int m_lastLookAssistAnalysisFrame = -1;
    QString m_lastLookAssistAnalysisAutoReason;
    size_t m_lastLookAssistAnalysisAutoSampleCount = 0;
    bool m_lastLookAssistAnalysisAfterAutoSteady = false;
    int m_lookAssistUnsettledAnalysisCount = 0;
    int m_lookAssistAutoWarmupDeferralCount = 0;
    // De-dupe guard: the receipt whose Auto Look Assist analysis already ran this clip-open. A second
    // setSliders/deferral must not re-run the ~3s auto-WB analysis (it derives the same look and just
    // re-freezes the UI). Keyed on the receipt pointer so different clips re-analyze naturally.
    ReceiptSettings *m_lookAssistAppliedReceipt = nullptr;
    // Generation counter bumped on every clip open/close. The async look-assist
    // worker captures it at dispatch; the queued apply lambda re-checks before
    // touching any UI state, ensuring stale results from a previous clip are dropped.
    std::atomic<int> m_lookAssistAsyncGeneration{0};
    // Round-4 debt block: count of detached look-assist workers still running.
    // The worker reads m_pMlvObject (findMlvWhiteBalance renders from raw), so
    // every freeMlvObject path must drainLookAssistWorkers() first or the
    // worker reads freed memory (0xC0000005 on fast clip switching).
    std::atomic<int> m_lookAssistWorkersInFlight{0};
    ClipLifecycleBarrier m_clipLifecycleBarrier;
    QString m_phase3ClipPlaytimeFingerprint;
    qint64 m_phase3ClipPlaytimePendingMs = 0;
    qint64 m_phase3ClipPlaytimeSinceFlushMs = 0;
    double m_phase3LastPresentedStageTime = 0.0;
    bool m_playbackFrameAdvancePending = false;
    // Last playback frame number accepted for presentation; drives the forward-only
    // present guard in drawFrameReady() that drops stale backward-jumping renders.
    long long m_lastPresentedPlaybackFrame = -1;
    bool m_skipImmediateTimecodeLabel = false;
    bool m_playToFirstFramePending = false;
    bool m_playToFirstFrameTargetFrameValid = false;
    bool m_lastPlayToFirstFrameValid = false;
    bool m_lastPlayStartPrerollRequested = false;
    bool m_playbackInternalSliderAdvance = false;
    bool m_inClipDeleteProcess;
    bool m_renderThreadUsing16BitPreview;
    bool m_renderThreadUsingGpuPreviewProcessing;
    bool m_renderThreadUsingGpuBilinearDebayer;
    bool m_renderThreadUsingGpuAmazeDebayer;
    bool m_renderThreadUsingPlaybackPreviewProcessing = false;
    bool m_renderThreadUsingCpuPreviewProcessing = false;
    int m_playToFirstFrameTargetFrame = -1;
    double m_playToFirstFrameStartSeconds = 0.0;
    double m_lastPlayToFirstFrameMs = 0.0;
    bool m_gpuTexNrImmediateDrainActive = false;
    double m_lastDrawFrameReadyQueueMs = 0.0;
    double m_lastDrawFrameReadyAdvanceMs = 0.0;
    double m_lastDrawFrameReadySceneMs = 0.0;
    double m_lastDrawFrameReadyImageMs = 0.0;
    double m_lastDrawFrameReadyPresentMs = 0.0;
    double m_lastDrawFrameReadyScopesMs = 0.0;
    double m_lastDrawFrameReadyOverlayMs = 0.0;
    double m_lastDrawFrameReadyTotalMs = 0.0;
    bool m_playbackTimelineAdvancePending = false;
    uint64_t m_playbackTimelineExpectedRequestSerial = 0;
    uint64_t m_playbackTimelineIssuedRequestSerial = 0;
    uint64_t m_playbackTimelineSourceRequestSerial = 0;
    uint32_t m_playbackTimelineSourceFrame = 0;
    bool m_playbackTimelineAdvanceEarly = false;
    bool m_playbackTimelinePredictiveGpuTexNr = false;
    double m_playbackTimelineAdvanceIssueStageTime = 0.0;
    double m_playbackTimelineSourceFrameReadyEmitStageTime = 0.0;
    double m_playbackTimelineSourceDrawBeginStageTime = 0.0;
    double m_playbackScopeLastUpdateTime = 0.0;
    uint64_t m_playbackScopeUpdateCount = 0;
    uint64_t m_playbackScopeSkipCount = 0;
    uint64_t m_playbackAudioSyncRequestCount = 0;
    uint64_t m_playbackAudioSyncAppliedCount = 0;
    uint64_t m_playbackAudioSyncSkippedCount = 0;
    int m_lastPlaybackAudioSyncFrame = -1;
    double m_lastPlaybackAudioSyncTime = 0.0;
    bool m_playbackSmokeActive = false;
    bool m_playbackSmokeFrameTelemetry = false;
    bool m_playbackSmokeTimelineTelemetry = false;
    uint64_t m_playbackSmokeSessionId = 0;
    int m_playbackSmokeStartPosition = 0;
    int m_playbackSmokeStartCutIn = 0;
    int m_playbackSmokeStartCutOut = 0;
    int m_playbackSmokeStartScaleRequest = 1;
    int m_playbackSmokeStartQualityMode = 0;
    int m_playbackSmokeStartWorkerThreads = 0;
    int m_playbackSmokePresentedFrames = 0;
    int m_playbackSmokeParityMatchCount = 0;
    int m_playbackSmokeTargetPresentedFrames = 0;
    int m_playbackSmokeFirstPresentedFrame = -1;
    int m_playbackSmokeLastPresentedFrame = -1;
    uint64_t m_dualIsoWarmupTelemetryPresentationGeneration = 0;
    int m_dualIsoWarmupTelemetryPresentedFrames = 0;
    uint64_t m_playbackSmokeStartRequestSerial = 0;
    uint64_t m_playbackSmokeStartDecodeRequestsIssued = 0;
    uint64_t m_playbackSmokeStartPrepStaleDrops = 0;
    uint64_t m_playbackSmokeStartPrepGenerationDrops = 0;
    uint64_t m_playbackSmokeStartPrepReplacedBefore = 0;
    uint64_t m_playbackSmokeStartPrepReplacedAfter = 0;
    uint64_t m_playbackSmokeStartScopeUpdates = 0;
    uint64_t m_playbackSmokeStartScopeSkips = 0;
    uint64_t m_playbackSmokeStartAudioSyncRequests = 0;
    uint64_t m_playbackSmokeStartAudioSyncApplied = 0;
    uint64_t m_playbackSmokeStartAudioSyncSkipped = 0;
    double m_playbackSmokeStartTime = 0.0;
    double m_playbackSmokeLastPresentedTime = 0.0;
    double m_playbackSmokeFirstPresentMs = 0.0;
    double m_playbackSmokePresentedIntervalSumMs = 0.0;
    double m_playbackSmokePresentedIntervalMaxMs = 0.0;
    double m_playbackSmokeRenderTotalSumMs = 0.0;
    double m_playbackSmokeRenderTotalMaxMs = 0.0;
    double m_playbackSmokeRenderWorkSumMs = 0.0;
    double m_playbackSmokeQueueWaitSumMs = 0.0;
    double m_playbackSmokePresentUiSignalLatencySumMs = 0.0;
    double m_playbackSmokePresentDrawPresentSumMs = 0.0;
    double m_playbackSmokePresentOverlaysScopesSumMs = 0.0;
    double m_playbackSmokePresentRenderSlotReleaseSumMs = 0.0;
    double m_playbackSmokePresentPacingSumMs = 0.0;
    double m_playbackSmokeLlrawprocSumMs = 0.0;
    double m_playbackSmokeProcessed8SumMs = 0.0;
    double m_playbackSmokeProcessed8CacheStoreSumMs = 0.0;
    double m_playbackSmokeRawUint16SumMs = 0.0;
    double m_playbackSmokeRawUint16DecompressSumMs = 0.0;
    double m_playbackSmokeRawUint16UnpackSumMs = 0.0;
    double m_playbackSmokeLlrawprocTotalSumMs = 0.0;
    double m_playbackSmokeLlrawprocDualIsoSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20TotalSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20PatternSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20NoiseSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20ScratchSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20Convert20SumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MatchSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20InterpSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20FullresSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixCurveSelectSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixCurveBuildSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixCurveFloatSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixEvLutSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixHalfresSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixHalfresAvx2BulkSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixHalfresScalarTailSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaCopySumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaFullresSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHorizProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaVertProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaCenterProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaCenterGatherProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaCenterArithmeticProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaCenterStoreProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaCenterStoreRLookupProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaCenterStoreBLookupProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaCenterAverageProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaCenterNonAverageProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaCenterNonAverageChooseTrueProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaCenterNonAverageChooseFalseProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterGatherProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterArithmeticProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterStoreProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterStoreRProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterStoreBProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterStoreRLookupProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterStoreBLookupProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterAverageProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterNonAverageProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterNonAverageChooseTrueProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterNonAverageChooseFalseProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixChromaHalfresCenterNonAverageWriteBothProbeSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixAliasMapSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20MixOverexposedSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20FinalBlendSumMs = 0.0;
    double m_playbackSmokeDualIsoFull20Convert16SumMs = 0.0;
    double m_playbackSmokeDualIsoFull20OtherSumMs = 0.0;
    double m_playbackSmokeLlrawprocChromaSmoothSumMs = 0.0;
    double m_playbackSmokeLlrawprocOtherSumMs = 0.0;
    double m_playbackSmokeDebayeredFrameSumMs = 0.0;
    double m_playbackSmokeDebayerExclusiveSumMs = 0.0;
    double m_playbackSmokeDebayerWbPrepareSumMs = 0.0;
    double m_playbackSmokeDebayerCaSumMs = 0.0;
    double m_playbackSmokeDebayerKernelSumMs = 0.0;
    double m_playbackSmokeDebayerWbUndoSumMs = 0.0;
    double m_playbackSmokeDebayerPipelineOtherSumMs = 0.0;
    double m_playbackSmokeProcessingSumMs = 0.0;
    double m_playbackSmokeProcessingSetupSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsPrepSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsResizeSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsCopySumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsFilterSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsFilterHalfresDownsampleSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsFilterHalfresRbfSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsFilterHalfresUpsampleSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsFilterQuarterresDownsampleSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsFilterQuarterresRbfSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsFilterQuarterresUpsampleSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfTotalSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfBoundarySumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfRangeTableSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfLeftSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfRightSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfHorizontalAverageSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfVerticalDownSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfVerticalUpFirstLineSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfVerticalUpBodySumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfVerticalUpBodyDiffSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfVerticalUpBodyStoreSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfVerticalUpBodyStoreFactorSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfVerticalUpBodyStoreColorSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfVerticalUpBodyStoreColorSrcSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfVerticalUpBodyStoreColorPrevSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfVerticalUpBodyStoreColorAssignSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfVerticalUpSumMs = 0.0;
    double m_playbackSmokeProcessingShadowsHighlightsRbfOutputSumMs = 0.0;
    double m_playbackSmokeProcessingHighestGreenSumMs = 0.0;
    double m_playbackSmokeProcessingCoreSumMs = 0.0;
    double m_playbackSmokeProcessingCoreLevelsSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorGradientSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeVignetteSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeCreativeSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeCreativeShadowsSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeCreativeContrastSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeWbSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeWbMatrixSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeWbMatrixRSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeWbMatrixGSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeWbMatrixBSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeWbGradientMatrixSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeWbExposureSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeWbGamutSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorMainPreludeWbReconSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamMainSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamGradientSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamWbSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamWbMatrixSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamWbGamutSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamWbDesatSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxClipSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxClipNegRCountSum = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxClipNegGCountSum = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxClipNegBCountSum = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxMatrixSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxMatrixRSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxMatrixGSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxMatrixBSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxMatrixRHiCountSum = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxMatrixGHiCountSum = 0.0;
    double m_playbackSmokeProcessingCoreColorCamAgxMatrixBHiCountSum = 0.0;
    double m_playbackSmokeProcessingCoreColorGammaSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorGammaMainSumMs = 0.0;
    double m_playbackSmokeProcessingCoreColorGammaGradientSumMs = 0.0;
    double m_playbackSmokeProcessingCoreCreativeSumMs = 0.0;
    double m_playbackSmokeProcessingCoreCreativeHueVsSumMs = 0.0;
    double m_playbackSmokeProcessingCoreCreativeVibranceSumMs = 0.0;
    double m_playbackSmokeProcessingCoreCreativeSaturationSumMs = 0.0;
    double m_playbackSmokeProcessingCoreCreativeToningSumMs = 0.0;
    double m_playbackSmokeProcessingCoreCreativeCurveSumMs = 0.0;
    double m_playbackSmokeProcessingCoreCreativeGradationSumMs = 0.0;
    double m_playbackSmokeProcessingCoreCreativeAgxInverseSumMs = 0.0;
    double m_playbackSmokeProcessingCoreOutputSumMs = 0.0;
    double m_playbackSmokeProcessingCoreOtherSumMs = 0.0;
    double m_playbackSmokeProcessingChromaSumMs = 0.0;
    double m_playbackSmokeProcessingSharpenSumMs = 0.0;
    double m_playbackSmokeProcessingGrainSumMs = 0.0;
    double m_playbackSmokeProcessingDirect8MatrixSumMs = 0.0;
    double m_playbackSmokeProcessingDirect8GammaSumMs = 0.0;
    double m_playbackSmokeProcessingDirect8CurvesSumMs = 0.0;
    double m_playbackSmokeProcessed16SumMs = 0.0;
    double m_playbackSmokeProcessed16For8BitSumMs = 0.0;
    double m_playbackSmokeProcessed16To8BitSumMs = 0.0;
    double m_playbackSmokeProcessed16CacheStoreSumMs = 0.0;
    double m_playbackSmokeProcessed16SetupSumMs = 0.0;
    double m_playbackSmokeProcessed16CoreMathSumMs = 0.0;
    double m_playbackSmokeProcessed16LocalToneSumMs = 0.0;
    double m_playbackSmokeProcessed16ThreadingOverheadSumMs = 0.0;
    double m_playbackSmokeProcessed8SetupSumMs = 0.0;
    double m_playbackSmokeProcessed8CoreMathSumMs = 0.0;
    double m_playbackSmokeProcessed8LocalToneSumMs = 0.0;
    double m_playbackSmokeProcessed8ThreadingOverheadSumMs = 0.0;
    double m_playbackSmokePlaybackScaleSumMs = 0.0;
    double m_playbackSmokeDrawImageSumMs = 0.0;
    double m_playbackSmokeDrawPresentSumMs = 0.0;
    double m_playbackSmokeDrawAdvanceSumMs = 0.0;
    double m_playbackSmokeDrawScopesSumMs = 0.0;
    double m_playbackSmokeDrawTotalSumMs = 0.0;
    double m_playbackSmokeDrawTotalMaxMs = 0.0;
    double m_playbackSmokePrepPreEnqueueSumMs = 0.0;
    double m_playbackSmokePrepWorkerQueueSumMs = 0.0;
    double m_playbackSmokePrepWorkerBuildSumMs = 0.0;
    double m_playbackSmokePrepWorkerTotalSumMs = 0.0;
    double m_playbackSmokePrepResultQueueSumMs = 0.0;
    double m_playbackSmokePrepTotalBeforeFinishSumMs = 0.0;
    int m_playbackSmokePrepInlinePresentFrames = 0;
    int m_playbackSmokeProcessed8DirectPathFrames = 0;
    int m_playbackSmokeBorrowedPreparedRgb8Frames = 0;
    int m_playbackSmokeOwnedPreparedRgb8Frames = 0;
    int m_playbackSmokeMovedPreparedRgb8Frames = 0;
    int m_playbackSmokeQImagePreparedRgb8Frames = 0;
    uint64_t m_playbackSmokeBorrowedPreparedRgb8Bytes = 0;
    uint64_t m_playbackSmokeOwnedPreparedRgb8Bytes = 0;
    uint64_t m_playbackSmokeMovedPreparedRgb8Bytes = 0;
    uint64_t m_playbackSmokeQImagePreparedRgb8Bytes = 0;
    int m_playbackSmokeDualIsoFull20ValidFrames = 0;
    int m_playbackSmokeDualIsoFull20LastInterpMethod = -1;
    int m_playbackSmokeDualIsoFull20LastMixChromaProbeMode = -1;
    int m_playbackSmokeDualIsoFull20LastMixHalfresProbeMode = -1;
    int m_playbackSmokeDualIsoFull20LastThreads = 0;
    bool m_playbackSmokeDualIsoFull20LastAliasMap = false;
    bool m_playbackSmokeDualIsoFull20LastFullres = false;
    int m_playbackSmokeLastDebayerEngineMode = -1;
    int m_playbackSmokeDebayerBasicU16Avx2AvailableFrames = 0;
    int m_playbackSmokeDebayerBasicU16Avx2UsedFrames = 0;
    int m_playbackSmokeProcessed8CacheHits = 0;
    int m_playbackSmokeProcessed8PrefetchHits = 0;
    int m_playbackSmokeRawPrefetchHits = 0;
    int m_playbackSmokeGpuStatusCpuFrames = 0;
    int m_playbackSmokeGpuStatusPreviewFrames = 0;
    int m_playbackSmokeGpuStatusReconReadbackFrames = 0;
    int m_playbackSmokeGpuStatusTextureReadbackFrames = 0;
    int m_playbackSmokeGpuStatusTextureNoReadbackFrames = 0;
    int m_playbackSmokeFallbackCount = 0;
    QJsonObject m_playbackSmokeFallbackReasonCounts;
    QJsonObject m_playbackSmokeAsyncH2dBooleanTrueCounts;
    QJsonObject m_playbackSmokeAsyncH2dBooleanSampleCounts;
    QJsonObject m_playbackSmokeAsyncH2dTimingSums;
    QJsonObject m_playbackSmokeAsyncH2dTimingMaxima;
    QJsonObject m_playbackSmokeAsyncH2dTimingSampleCounts;
    uint64_t m_playbackSmokeQueuedPlaybackDropSum = 0;
    uint64_t m_playbackSmokeQueuedPlaybackDropMax = 0;
    int m_playbackSmokeLastWorkerThreads = 0;
    bool m_playbackSmokeLastWorkerThreadCapActive = false;
    int m_playbackSmokeLastOpenMpThreads = 0;
    bool m_playbackSmokeLastOpenMpThreadCapActive = false;
    int m_playbackSmokeLastScaleRequest = 1;
    int m_playbackSmokeLastScaleActive = 1;
    bool m_headlessPlaybackProfileUsePlaybackPolicy = false;
    bool m_headlessPlaybackProfileActive = false;
    uint64_t m_nextRenderRequestSerial = 1;
    uint64_t m_lastPresentedRequestSerial = 0;
    GpuPreviewProcessingBackendRequest m_gpuPreviewProcessingBackendRequest =
        GpuPreviewProcessingBackendRequest::Auto;
    GpuBilinearDebayerBackendRequest m_gpuBilinearDebayerBackendRequest =
        GpuBilinearDebayerBackendRequest::Auto;
    GpuAmazeDebayerBackendRequest m_gpuAmazeDebayerBackendRequest =
        GpuAmazeDebayerBackendRequest::Auto;
    MainWindowGpuPreviewPolicyState m_lastQueuedGpuPreviewPolicy;
    GpuDisplayViewport::PresentationOptions m_lastQueuedGpuPresentationOptions;
    GpuPreviewProcessingConfig m_lastQueuedGpuPreviewProcessingConfig;
    QString m_lastQueuedPlaybackProcessingReason;
    bool m_gpuPreviewProcessingConfigCacheValid = false;
    uint64_t m_gpuPreviewProcessingConfigCacheGeneration = 1;
    uint64_t m_gpuPreviewProcessingConfigCacheEntryGeneration = 0;
    const processingObject_t *m_gpuPreviewProcessingConfigCacheProcessing = nullptr;
    int m_gpuPreviewProcessingConfigCacheDualIso = 0;
    int m_gpuPreviewProcessingConfigCacheHighestGreenDiso = 0;
    int m_gpuPreviewProcessingConfigCacheGradientHighestGreenDiso = 0;
    GpuPreviewProcessingConfig m_gpuPreviewProcessingConfigCache;
    QString m_gpuPreviewProcessingConfigCacheReason;
    std::deque<PresentationRequestContext> m_pendingPresentationRequests;
    PresentationRequestContext m_lastPresentedRequestContext;
    bool m_lastPresentedRequestContextValid = false;

    // Playback-prep worker: background image preparation with request-conflated
    // delivery to the UI thread.
    // - `m_latestRequestedSerial` conflates queued/pre-compute work. A frame
    //   that already finished prep may still present when its generation is
    //   current; latest-serial equality after compute can starve presentation
    //   forever when requests arrive faster than prep completes.
    // - `m_playbackPresentationGeneration` invalidates in-flight work whose
    //   request serial is still current but whose display assumptions changed
    //   underneath it, such as Playback Scale x2/x4 -> x1.
    // - Conflation queue semantics: at most one in-flight task ("current") and
    //   at most one queued task ("pending"). New enqueues replace pending.
    std::atomic<uint64_t> m_latestRequestedSerial{0};
    std::atomic<uint64_t> m_playbackPresentationGeneration{1};
    std::thread m_playbackPrepThread;
    std::mutex m_playbackPrepMutex;
    std::condition_variable m_playbackPrepCv;
    std::atomic<bool> m_playbackPrepStop{false};
    bool m_playbackPrepPendingValid = false;
    PlaybackPrepTask m_playbackPrepPending;
    std::deque<PlaybackPrepResult> m_playbackPrepResults;
    std::atomic<uint64_t> m_playbackPrepStaleDropCount{0};
    std::atomic<uint64_t> m_playbackPrepGenerationDropCount{0};
    std::atomic<uint64_t> m_playbackPrepReplacedBeforeComputeCount{0};
    std::atomic<uint64_t> m_playbackPrepReplacedAfterComputeCount{0};
    bool m_lastPresentedFrameUsedGpuBilinearDebayer = false;
    int m_lastPresentedPlaybackScaleFactorActive = 1;
    QString m_lastPresentedGpuBilinearFallbackReason;
    QString m_lastPresentedGpuBilinearRendererDescription;
    bool m_lastPresentedFrameUsedGpuAmazeDebayer = false;
    QString m_lastPresentedGpuAmazeFallbackReason;
    QString m_lastPresentedGpuAmazeRendererDescription;
    double m_lastPresentedDualIsoPreviewHistogramMs = 0.0;
    double m_lastPresentedDualIsoPreviewRegressionMs = 0.0;
    double m_lastPresentedDualIsoPreviewRowscaleMs = 0.0;
    QJsonObject m_lastPresentedStageTimingTelemetry;
    QJsonObject m_lastPresentedFrameColorTelemetry;
    uint32_t m_displayPreviewCacheNextSlot;
    int m_lastDisplaySceneWidth = -1;
    int m_lastDisplaySceneHeight = -1;
    DisplayPreviewCacheEntry m_displayPreviewCache[8];
    QString m_lastExportPath;
    QString m_lastSessionFileName;
    QString m_lastMlvOpenFileName;
    QString m_lastReceiptFileName;
    QString m_lastDarkframeFileName;
    QString m_lastLutFileName;
    QString m_externalApplicationName;
    QString m_sessionFileName;
    QString m_defaultReceiptFileName;
    ReceiptSettings *m_pReceiptClipboard;
    QVector<ReceiptSettings*> m_exportQueue;
    SessionModel* m_pModel;
    QSortFilterProxyModel* m_pProxyModel;
    QItemSelectionModel* m_pSelectionModel;
    int m_lastClipBeforeExport;
    void drawFrame( bool updateTimecodeLabel = true );
    void queuePresentationRequest( const PresentationRequestContext &context );
    bool consumePresentationRequest( uint64_t requestSerial,
                                     PresentationRequestContext *context );
    void queuePlaybackLookaheadRequests(
        const PresentationRequestContext &baseContext,
        RenderFrameThread::OutputMode renderOutputMode,
        bool useGpuBilinearDebayer,
        bool useGpuAmazeDebayer,
        const RenderFrameThread::PresentationPreparationOptions &presentationPreparation,
        int requestedFrame );
    void computeDisplaySceneGeometry( int sourceWidth,
                                      int sourceHeight,
                                      bool zoomFitEnabled,
                                      double stretchX,
                                      double stretchY,
                                      int *sceneWidth,
                                      int *sceneHeight ) const;
    void recordPresentedFrame( const RenderFrameThread::ReadyFrame &readyFrame,
                               const PresentationRequestContext &requestContext );
    bool isFrameSettledForAnalysis( int frameIndex,
                                    uint64_t requestSerialFloor ) const;
    bool isLookAssistFrameSettledForAnalysis( int baselineFrame,
                                              uint64_t requestSerialFloor,
                                              int *analysisFrame );
    void beginPlaybackSmokeTelemetry( void );
    void notePlaybackSmokePresentedFrame( uint64_t displayFrame,
                                          const RenderFrameThread::ReadyFrame &readyFrame,
                                          const PresentationRequestContext &requestContext );
    void finishPlaybackSmokeTelemetry( const char *reason );
    void enqueuePlaybackPrepTask( const PlaybackPrepTask &task );
    void invalidatePlaybackPrepForDisplayChange( const char *reason );
    void waitForRenderThreadIdleBeforeCoreMutation( const char *reason );
    PlaybackPrepResult buildPlaybackPrepResult( const PlaybackPrepTask &task );
    void playbackPrepThreadLoop( void );
    void presentPlaybackPreparedFrame( const PlaybackPrepResult &result );
    void finishPresentedFrame( uint64_t displayFrame,
                               RenderFrameThread::ReadyFrame &readyFrame,
                               const PresentationRequestContext &requestContext,
                               const uint8_t *rgb8DisplaySource,
                               uint8_t underOver,
                               bool releasePresentedFrameEarly,
                               QElapsedTimer &prepRegionClock,
                               qint64 prepRegionStartNs,
                               double displayStart );
    void setBadPixelCrosshairVisibility( bool visible, bool force = false );
    bool playbackPolicyActive( void ) const;
    void applyPlaybackDebayerSelection( void );
    void setPlaybackProfileDebayerRequest(
        PlaybackProfileDebayerRequest request );
    void setPlaybackProfileProcessingRequest(
        PlaybackProfileProcessingRequest request );
    void restorePlaybackDebayerSelection( const QString & label );
    QString selectedPlaybackDebayerLabel( void ) const;
    QString playbackDebayerLabel( void ) const;
    QString selectedPlaybackProcessingLabel( void ) const;
    QString playbackProcessingLabel( void ) const;
    void importNewMlv(QString fileName);
    int openMlvForPreview(QString fileName);
    int openMlv(QString fileName);
    void playbackHandling( int timeDiff );
    void initGui( void );
    void showPerformanceProfilingDialog( void );
    void initLib( void );
    void readSettings( void );
    void writeSettings( void );
    void startExportPipe( QString fileName );
    void startExportCdng( QString fileName );
    void startExportMlv( QString fileName );
    void startExportAVFoundation( QString fileName );
    void addFileToSession( QString fileName );
    int askToSaveCurrentSession( void );
    void openSession(QString fileNameSession );
    void saveSession( QString fileName );
    void applyEffectiveDualIsoPlaybackSettings( void );
    /* Phase 4E: Playback Quality dial helpers. */
    void initPlaybackQualityFromSettings( void );
    void initPlaybackPreviewModeFromSettings( void );
    void initPlaybackPreviewResolutionFromSettings( void );
    void drainLookAssistWorkers( const char *reason );
    void freeActiveMlvObjectAfterLifecycleBarrier( const char *reason );
    void initPlaybackScaleFactorFromSettings( void );
    void seedPlaybackQualityActiveStateForCurrentContext( void );
    void resetPlaybackQualityAutoRunState( void );
    void applyPlaybackQualityMode( int mode, bool persist, bool forceRefresh );
    void applyPlaybackPreviewMode( int mode, bool persist, bool forceRefresh );
    void applyPlaybackPreviewResolution( int res, bool persist, bool forceRefresh );
    void applyPlaybackScaleFactorOverride( int scaleFactor, bool persist );
    void applyPlaybackAutoTargetFps( int targetFps, bool persist );
    void setPlaybackQualityIndicatorVisible( bool visible, bool persist );
    void updatePlaybackQualityIndicator( void );
    void updatePhase3PlaybackQualityUi( void );
    bool maybeShowPhase3AcknowledgementDialog( int mode );
    void promotePlaybackQualityTier( int mode, PlaybackQualityTier tier );
    void demotePlaybackQualityTier( int mode );
    QString playbackQualityTierStatusText( int mode ) const;
    void notePhase3PlaybackTime( int measuredFrameMs );
    void flushPhase3PlaybackTime( bool force );
    QString activeClipPhase3Fingerprint( void ) const;
    QStringList pinnedClipFingerprintsForPhase3( void ) const;
    int  effectivePlaybackScaleFactorForRequest( void ) const;
    MainWindowGpuPreviewPolicyState gpuPreviewPolicyForCurrentScopeState(
        bool includeVisibleScopes ) const;
    bool visibleScopesBlockGpuTexturePlayback( void ) const;
    void maybeShowScopePerformanceWarning( QAction *scopeAction );
    static bool dualIsoPlaybackPreferHqMean23GuiFallback( void );
    void beginPlayToFirstFrameMeasurement( void );
    void notePlayToFirstFramePresentation( int presentedFrame );
    bool primePlaybackCacheOnPlayStart( void );
    void invalidateDisplayPreviewCache( void );
    void invalidateGpuPreviewProcessingConfigCache( void );
    GpuPreviewProcessingConfig gpuPreviewProcessingConfigForCurrentSettings(
        QString *reason );
    void clearPresentationForClipOpen( const char *reason );
    void requestFrameRefresh( bool resetCurrentFrameCache, const char *reason = nullptr );
    void readXmlElementsFromFile(QXmlStreamReader *Rxml, ReceiptSettings *receipt , int version);
    void writeXmlElementsToFile( QXmlStreamWriter *xmlWriter, ReceiptSettings *receipt );
    void deleteSession( void );
    bool isFileInSession( QString fileName );
    void pasteReceiptFromClipboardTo( int row );
    void setSliders(ReceiptSettings *sliders , bool paste);
    void resetSliders( void );
    void setReceipt( ReceiptSettings *sliders );
    void replaceReceipt(ReceiptSettings *receiptTarget, ReceiptSettings *receiptSource , bool paste);
    void resetReceiptWithDefault( ReceiptSettings *receipt );
    int showFileInEditor(int row);
    void addClipToExportQueue( int row, QString fileName );
    void previewPicture( int row );
    void setPreviewMode( void );
    double getFramerate( void );
    void paintAudioTrack( void );
    uint8_t drawZebras( QImage *image );
    void drawFrameNumberLabel( int frameIndex = -1 );
    void updateTimeCodeLabelForFrame( int frameIndex );
    bool shouldUseGpu16PreviewPath( void ) const;
    bool shouldUseGpuPreviewProcessingPath( void ) const;
    bool shouldUseGpuBilinearDebayerPath( void ) const;
    bool shouldUseGpuAmazeDebayerPath( void ) const;
    bool gpuPreviewSurfaceActive( void ) const;
    void setToolButtonFocusPixels( int index );
    void setToolButtonFocusPixelsIntMethod( int index );
    void setToolButtonBadPixels( int index );
    void setToolButtonBadPixelsSearchMethod( int index );
    void setToolButtonBadPixelsIntMethod( int index );
    void setToolButtonChromaSmooth( int index );
    void setToolButtonPatternNoise( int index );
    void setToolButtonUpsideDown( int index );
    void setToolButtonVerticalStripes( int index );
    void setToolButtonDualIso( int index );
    void setToolButtonDualIsoInterpolation( int index );
    void setToolButtonDualIsoAliasMap( int index );
    void setToolButtonDualIsoFullresBlending( int index );
    void setToolButtonDarkFrameSubtraction( int index );
    void setToolButtonGCurves( int index );
    int toolButtonFocusPixelsCurrentIndex( void );
    int toolButtonFocusPixelsIntMethodCurrentIndex( void );
    int toolButtonBadPixelsCurrentIndex( void );
    int toolButtonBadPixelsSearchMethodCurrentIndex( void );
    int toolButtonBadPixelsIntMethodCurrentIndex( void );
    int toolButtonChromaSmoothCurrentIndex( void );
    int toolButtonPatternNoiseCurrentIndex( void );
    int toolButtonUpsideDownCurrentIndex( void );
    int toolButtonVerticalStripesCurrentIndex( void );
    int toolButtonDualIsoCurrentIndex( void );
    int toolButtonDualIsoInterpolationCurrentIndex( void );
    int toolButtonDualIsoAliasMapCurrentIndex( void );
    int toolButtonDualIsoFullresBlendingCurrentIndex( void );
    int toolButtonDarkFrameSubtractionCurrentIndex( void );
    int toolButtonGCurvesCurrentIndex( void );
    void initCutInOut( int frames );
    bool normalizePlaybackCutRangeForLoadedClip( const char *where );
    int normalizePlaybackRequestedFrame( int requestedFrame, const char *where );
    void initRawBlackAndWhite( void );
    double getHorizontalStretchFactor( bool downScale );
    double getVerticalStretchFactor( bool downScale );
    void setWhiteBalanceFromMlv( ReceiptSettings *sliders );
    void captureLookAssistBaseline( ReceiptSettings *receipt );
    void restoreLookAssistBaseline( ReceiptSettings *receipt );
    void applyLookAssistToReceipt( ReceiptSettings *receipt,
                                   int analysisFrame = -1 );
    void syncLookAssistDerivedUiToReceipt( ReceiptSettings *receipt );
    void setGradientMask( void );
    uint16_t autoCorrectRawBlackLevel( void );
    uint16_t autoCorrectRawWhiteLevel( void );
    uint16_t restrictedLosslessDualIsoOutputWhiteLevel( void );
    void applyRawLevelsAutoFix( void );
    QRecentFilesMenu *m_pRecentFilesMenu;
    void selectDebayerAlgorithm( void );
    void enableCreativeAdjustments( bool enable );
    void resultingResolution( void );
    bool isExportSequence( void );
    void setMarkColor(int clipNr , uint8_t mark);
    void focusPixelCheckAndInstallation( void );
    void checkFocusPixelUpdate( void );
    QModelIndexList selectedClipsList( void );
    void listViewSessionUpdate( void );
    void checkDiskFull( QString path );

signals:
    void exportReady( void );
    void playbackPrepResultReady( void );
};

#endif // MAINWINDOW_H
