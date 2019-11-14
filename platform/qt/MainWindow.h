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
#include <QGraphicsPixmapItem>
#include <QCloseEvent>
#include <QXmlStreamWriter>
#include <QActionGroup>
#include "../../src/mlv_include.h"
#include "InfoDialog.h"
#include "StatusDialog.h"
#include "AudioWave.h"
#include "ReceiptSettings.h"
#include "AudioPlayback.h"
#include "GraphicsPickerScene.h"
#include "RenderFrameThread.h"
#include "GradientElement.h"
#include "TimeCodeLabel.h"
#include "DoubleClickLabel.h"
#include "Scripting.h"
#include "Updater/updaterUI/CUpdater.h"
#include "ReceiptCopyMaskDialog.h"
#include "QRecentFilesMenu.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(int &argc, char **argv, QWidget *parent = 0);
    ~MainWindow();

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
    void timerFrameEvent( void );
    void on_actionOpen_triggered();
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
    void on_comboBoxUseCameraMatrix_currentIndexChanged(int index);
    void on_checkBoxCreativeAdjustments_toggled(bool checked);
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
    void on_actionAlwaysUseAMaZE_triggered();
    void on_actionCaching_triggered();
    void on_actionDontSwitchDebayerForPlayback_triggered();
    void on_actionExportSettings_triggered();
    void on_actionResetReceipt_triggered();
    void on_actionCopyRecept_triggered();
    void on_actionPasteReceipt_triggered();
    void on_actionNewSession_triggered();
    void on_actionOpenSession_triggered();
    void on_actionSaveSession_triggered();
    void on_actionSaveAsSession_triggered();
    void on_actionImportReceipt_triggered();
    void on_actionExportReceipt_triggered();
    void on_actionNext_Clip_triggered();
    void on_actionPrevious_Clip_triggered();
    void on_actionSelectAllClips_triggered();
    void on_actionDeleteSelectedClips_triggered();
    void on_actionHelp_triggered();
    void on_actionCreateAllMappFilesNow_triggered();
    void on_actionBetterResizer_triggered();
    void on_actionShowInstalledFocusPixelMaps_triggered();
    void on_listWidgetSession_activated(const QModelIndex &index);
    void on_dockWidgetSession_visibilityChanged(bool visible);
    void on_dockWidgetEdit_visibilityChanged(bool visible);
    void on_actionShowAudioTrack_toggled(bool checked);
    void on_listWidgetSession_customContextMenuRequested(const QPoint &pos);
    void deleteFileFromSession( void );
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
    void on_toolButtonDualIsoForce_toggled( bool checked );
    void toolButtonDualIsoChanged( void );
    void toolButtonDualIsoInterpolationChanged( void );
    void toolButtonDualIsoAliasMapChanged( void );
    void toolButtonDualIsoFullresBlendingChanged( void );
    void toolButtonDarkFrameSubtractionChanged( bool checked );
    void toolButtonGCurvesChanged( void );
    void on_toolButtonGCurvesReset_clicked();
    void on_toolButtonGCurvesResetOne_clicked();
    void on_toolButtonHueVsHueReset_clicked();
    void on_toolButtonHueVsSatReset_clicked();
    void on_toolButtonHueVsLumaReset_clicked();
    void on_toolButtonLumaVsSatReset_clicked();
    void on_actionNextFrame_triggered();
    void on_actionPreviousFrame_triggered();
    void on_checkBoxRawFixEnable_clicked(bool checked);
    void on_checkBoxLutEnable_clicked(bool checked);
    void on_checkBoxFilterEnable_clicked(bool checked);
    void on_checkBoxVidstabEnable_toggled(bool checked);
    void on_checkBoxVidstabTripod_toggled(bool checked);
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
    void updateCheckResponse( bool arg );

    void on_toolButtonLoadLut_clicked();
    void on_lineEditLutName_textChanged(const QString &arg1);

    void on_toolButtonRawBlackAutoCorrect_clicked();

    void on_actionSelectExternalApplication_triggered();
    void openRecentSession( QString fileName );

    void on_actionDarkThemeStandard_triggered(bool checked);
    void on_actionDarkThemeModern_triggered(bool checked);

    void on_comboBoxDebayer_currentIndexChanged( int index );

private:
    Ui::MainWindow *ui;
    InfoDialog *m_pInfoDialog;
    StatusDialog *m_pStatusDialog;
    AudioWave *m_pAudioWave;
    AudioPlayback *m_pAudioPlayback;
    RenderFrameThread *m_pRenderThread;
    mlvObject_t *m_pMlvObject;
    processingObject_t *m_pProcessingObject;
    QGraphicsPixmapItem *m_pGraphicsItem;
    QGraphicsPixmapItem *m_pGraphicsBadPixelItem;
    GradientElement *m_pGradientElement;
    GraphicsPickerScene* m_pScene;
    TimeCodeLabel* m_pTimeCodeImage;
    ReceiptCopyMaskDialog *m_pCopyMask;
    Scripting* m_pScripting;
    CUpdater* m_pUpdateCheck;
    uint8_t m_timeCodePosition;
    QLabel *m_pCachingStatus;
    QLabel *m_pFpsStatus;
    QLabel *m_pFrameNumber;
    QLabel *m_pChosenDebayer;
    QActionGroup *m_darkFrameGroup;
    QActionGroup *m_previewDebayerGroup;
    QActionGroup *m_sessionListGroup;
    QActionGroup *m_playbackElementGroup;
    QActionGroup *m_scopeGroup;
    DoubleClickLabel *m_pTcLabel;
    bool m_tcModeDuration;
    uint8_t *m_pRawImage;
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
    bool m_fileLoaded;
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
    QString m_lastExportPath;
    QString m_lastSessionFileName;
    QString m_lastMlvOpenFileName;
    QString m_lastReceiptFileName;
    QString m_lastDarkframeFileName;
    QString m_lastLutFileName;
    QString m_externalApplicationName;
    QString m_sessionFileName;
    ReceiptSettings *m_pReceiptClipboard;
    QVector<ReceiptSettings*> m_pSessionReceipts;
    QVector<ReceiptSettings*> m_exportQueue;
    int m_lastActiveClipInSession;
    int m_lastClipBeforeExport;
    void drawFrame( void );
    void importNewMlv(QString fileName);
    int openMlvForPreview(QString fileName);
    int openMlv(QString fileName);
    void playbackHandling( int timeDiff );
    void initGui( void );
    void initLib( void );
    void readSettings( void );
    void writeSettings( void );
    void startExportPipe( QString fileName );
    void startExportCdng( QString fileName );
    void startExportMlv( QString fileName );
    void startExportAVFoundation( QString fileName );
    void addFileToSession( QString fileName );
    void openSession(QString fileNameSession );
    void saveSession( QString fileName );
    void readXmlElementsFromFile(QXmlStreamReader *Rxml, ReceiptSettings *receipt , int version);
    void writeXmlElementsToFile( QXmlStreamWriter *xmlWriter, ReceiptSettings *receipt );
    void deleteSession( void );
    bool isFileInSession( QString fileName );
    void setSliders(ReceiptSettings *sliders , bool paste);
    void setReceipt( ReceiptSettings *sliders );
    void replaceReceipt(ReceiptSettings *receiptTarget, ReceiptSettings *receiptSource , bool paste);
    int showFileInEditor(int row);
    void addClipToExportQueue( int row, QString fileName );
    void previewPicture( int row );
    void setPreviewMode( void );
    double getFramerate( void );
    void paintAudioTrack( void );
    uint8_t drawZebras( void );
    void drawFrameNumberLabel( void );
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
    int toolButtonDualIsoForceCurrentIndex();
    int toolButtonDualIsoCurrentIndex( void );
    int toolButtonDualIsoInterpolationCurrentIndex( void );
    int toolButtonDualIsoAliasMapCurrentIndex( void );
    int toolButtonDualIsoFullresBlendingCurrentIndex( void );
    int toolButtonDarkFrameSubtractionCurrentIndex( void );
    int toolButtonGCurvesCurrentIndex( void );
    void initCutInOut( int frames );
    void initRawBlackAndWhite( void );
    double getHorizontalStretchFactor( bool downScale );
    double getVerticalStretchFactor( bool downScale );
    void setWhiteBalanceFromMlv( ReceiptSettings *sliders );
    void setGradientMask( void );
    uint16_t autoCorrectRawBlackLevel( void );
    bool isRawBlackLevelWrong( void );
    QRecentFilesMenu *m_pRecentFilesMenu;
    void selectDebayerAlgorithm( void );
    void enableCreativeAdjustments( bool enable );
    void resultingResolution( void );

signals:
    void exportReady( void );
};

#endif // MAINWINDOW_H
