/*!
 * \file ExportSettingsDialog.h
 * \author masc4ii
 * \copyright 2017
 * \brief Select codec
 */

#ifndef EXPORTSETTINGSDIALOG_H
#define EXPORTSETTINGSDIALOG_H

#include <QDialog>
#include <Scripting.h>

#define CODEC_PRORES422PROXY        0
#define CODEC_PRORES422LT           1
#define CODEC_PRORES422ST           2
#define CODEC_PRORES422HQ           3
#define CODEC_PRORES4444            4
#define CODEC_AVIRAW                5
#define CODEC_CDNG                  6
#define CODEC_CDNG_LOSSLESS         7
#define CODEC_CDNG_FAST             8
#define CODEC_H264                  9
#define CODEC_H265                  10
#define CODEC_TIFF                  11
#define CODEC_MLV                   12
#define CODEC_DNXHD                 13
#define CODEC_DNXHR                 14
#define CODEC_AUDIO_ONLY            15

#define CODEC_PRORES_OPTION_KS      0
#define CODEC_PRORES_OPTION_AW      1
#define CODEC_PRORES_AVFOUNDATION   2

#define CODEC_CNDG_DEFAULT          0
#define CODEC_CDNG_RESOLVE          1

#define CODEC_H264_MOV              0
#define CODEC_H264_MP4              1
#define CODEC_H264_MKV              2
#define CODEC_H264_AVFOUNDATION     3

#define CODEC_H265_MOV              0
#define CODEC_H265_MP4              1
#define CODEC_H265_MKV              2

#define CODEC_MLV_FASTPASS          0
#define CODEC_MLV_COMPRESSED        1
#define CODEC_MLV_AVERAGED          2
#define CODEC_MLV_EXTRACT_DF        3

#define CODEC_DNXHD_1080p_10bit     0
#define CODEC_DNXHD_1080p_8bit      1
#define CODEC_DNXHD_720p_10bit      2
#define CODEC_DNXHD_720p_8bit       3

#define CODEC_DNXHR_444_1080p_10bit 0
#define CODEC_DNXHR_HQX_1080p_10bit 1
#define CODEC_DNXHR_HQ_1080p_8bit   2
#define CODEC_DNXHR_SQ_1080p_8bit   3
#define CODEC_DNXHR_LB_1080p_8bit   4

namespace Ui {
class ExportSettingsDialog;
}

class ExportSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportSettingsDialog(QWidget *parent = 0,
                                  Scripting *scripting = 0,
                                  uint8_t currentCodecProfile = 0,
                                  uint8_t currentCodecOption = 0,
                                  uint8_t debayerMode = 1,
                                  bool resize = false,
                                  uint16_t resizeWidth = 1920,
                                  uint16_t resizeHeight = 1080,
                                  bool fpsOverride = false,
                                  double fps = 25,
                                  bool exportAudio = true,
                                  bool heightLocked = false);
    ~ExportSettingsDialog();
    uint8_t encoderSetting(void);
    uint8_t encoderOption(void);
    uint8_t debayerMode(void);
    bool isResizeEnabled(void);
    uint16_t resizeWidth(void);
    uint16_t resizeHeight(void);
    bool isFpsOverride(void);
    double getFps(void);
    bool isExportAudioEnabled(void);
    bool isHeightLocked(void);

private slots:
    void on_pushButtonClose_clicked();
    void on_comboBoxCodec_currentIndexChanged(int index);
    void on_checkBoxFpsOverride_toggled(bool checked);
    void on_checkBoxResize_toggled(bool checked);
    void on_comboBoxOption_currentIndexChanged(const QString &arg1);
    void on_toolButtonLockHeight_toggled(bool checked);

    void on_comboBoxPostExportScript_currentIndexChanged(const QString &arg1);

private:
    Ui::ExportSettingsDialog *ui;
    Scripting *m_pScripting;
};

#endif // EXPORTSETTINGSDIALOG_H
