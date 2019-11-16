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
#include <QListWidgetItem>

#define CODEC_PRORES422PROXY        0
#define CODEC_PRORES422LT           1
#define CODEC_PRORES422ST           2
#define CODEC_PRORES422HQ           3
#define CODEC_PRORES4444            4
#define CODEC_AVI                   5
#define CODEC_CDNG                  6
#define CODEC_CDNG_LOSSLESS         7
#define CODEC_CDNG_FAST             8
#define CODEC_H264                  9
#define CODEC_H265                  10
#define CODEC_TIFF                  11
#define CODEC_PNG                   12
#define CODEC_JPG2K                 13
#define CODEC_MJPEG                 14
#define CODEC_FFVHUFF               15
#define CODEC_MLV                   16
#define CODEC_DNXHD                 17
#define CODEC_DNXHR                 18
#define CODEC_AUDIO_ONLY            19

#define CODEC_AVI_OPTION_YUV420     0
#define CODEC_AVI_OPTION_V210       1
#define CODEC_AVI_OPTION_BGR24      2

#define CODEC_PRORES_OPTION_KS      0
#define CODEC_PRORES_OPTION_AW      1
#define CODEC_PRORES_AVFOUNDATION   2

#define CODEC_CNDG_DEFAULT          0
#define CODEC_CDNG_RESOLVE          1

#define CODEC_H264_H_MOV            0
#define CODEC_H264_H_MP4            1
#define CODEC_H264_H_MKV            2
#define CODEC_H264_M_MOV            3
#define CODEC_H264_M_MP4            4
#define CODEC_H264_M_MKV            5
#define CODEC_H264_AVFOUNDATION     6

#define CODEC_H265_H_MOV            0
#define CODEC_H265_H_MP4            1
#define CODEC_H265_H_MKV            2
#define CODEC_H265_M_MOV            3
#define CODEC_H265_M_MP4            4
#define CODEC_H265_M_MKV            5
#define CODEC_H265_AVFOUNDATION     6

#define CODEC_TIFF_SEQ              0
#define CODEC_TIFF_AVG              1

#define CODEC_PNG_16                0
#define CODEC_PNG_8                 1

#define CODEC_JPG2K_SEQ             0
#define CODEC_JPG2K_MOV             1

#define CODEC_FFVHUFF_OPTION10      0
#define CODEC_FFVHUFF_OPTION12      1
#define CODEC_FFVHUFF_OPTION16      2

#define CODEC_MLV_FASTPASS          0
#define CODEC_MLV_COMPRESS          1
#define CODEC_MLV_DECOMPRESS        2
#define CODEC_MLV_AVERAGED          3
#define CODEC_MLV_EXTRACT_DF        4

#define CODEC_DNXHD_1080p_10bit     0
#define CODEC_DNXHD_1080p_8bit      1
#define CODEC_DNXHD_720p_10bit      2
#define CODEC_DNXHD_720p_8bit       3

#define CODEC_DNXHR_444_1080p_10bit 0
#define CODEC_DNXHR_HQX_1080p_10bit 1
#define CODEC_DNXHR_HQ_1080p_8bit   2
#define CODEC_DNXHR_SQ_1080p_8bit   3
#define CODEC_DNXHR_LB_1080p_8bit   4

#define SMOOTH_FILTER_OFF           0
#define SMOOTH_FILTER_1PASS         1
#define SMOOTH_FILTER_3PASS         2
#define SMOOTH_FILTER_3PASS_USM     3
#define SMOOTH_FILTER_3PASS_USM_BB  4 //=last resort

#define SCALE_BICUBIC               0
#define SCALE_BILINEAR              1
#define SCALE_SINC                  2
#define SCALE_LANCZOS               3
#define SCALE_BSPLINE               4

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
                                  bool heightLocked = false,
                                  uint8_t smooth = 0,
                                  bool hdrBlending = false);
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
    uint8_t smoothSetting(void);
    bool hdrBlending(void);

private slots:
    void on_pushButtonClose_clicked();
    void on_comboBoxCodec_currentIndexChanged(int index);
    void on_checkBoxFpsOverride_toggled(bool checked);
    void on_checkBoxResize_toggled(bool checked);
    void on_comboBoxOption_currentIndexChanged(const QString &arg1);
    void on_toolButtonLockHeight_toggled(bool checked);
    void on_comboBoxPostExportScript_currentIndexChanged(const QString &arg1);
    void on_checkBoxHdrBlending_toggled(bool checked);
    void on_comboBoxSmoothing_currentIndexChanged(int index);

    void on_toolButtonAddPreset_clicked();
    void on_toolButtonDeletePreset_clicked();
    void on_listWidget_itemChanged(QListWidgetItem *item);
    void on_listWidget_itemClicked(QListWidgetItem *item);

    void on_listWidget_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);

private:
    Ui::ExportSettingsDialog *ui;
    Scripting *m_pScripting;
    bool m_blockOnce;
};

#endif // EXPORTSETTINGSDIALOG_H
