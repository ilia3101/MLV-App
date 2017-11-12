/*!
 * \file ExportSettingsDialog.h
 * \author masc4ii
 * \copyright 2017
 * \brief Select codec
 */

#ifndef EXPORTSETTINGSDIALOG_H
#define EXPORTSETTINGSDIALOG_H

#include <QDialog>

#define CODEC_PRORES422PROXY 0
#define CODEC_PRORES422LT    1
#define CODEC_PRORES422ST    2
#define CODEC_PRORES422HQ    3
#define CODEC_PRORES4444     4
#define CODEC_AVIRAW         5
#define CODEC_CDNG           6

#define CODEC_PRORES_OPTION_KS 0
#define CODEC_PRORES_OPTION_AW 1

namespace Ui {
class ExportSettingsDialog;
}

class ExportSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportSettingsDialog(QWidget *parent = 0, uint8_t currentCodecProfile = 0, uint8_t currentCodecOption = 0, uint8_t previewMode = 0, bool fpsOverride = false, double fps = 25, bool exportAudio = true, int style = 0);
    ~ExportSettingsDialog();
    uint8_t encoderSetting(void);
    uint8_t encoderOption(void);
    bool isExportAudioEnabled(void);
    uint8_t previewMode(void);
    bool isFpsOverride(void);
    double getFps(void);
    int getStyleIndex(void);

private slots:
    void on_pushButtonClose_clicked();
    void on_comboBoxCodec_currentIndexChanged(int index);

private:
    Ui::ExportSettingsDialog *ui;
    int m_styleAtStart;
};

#endif // EXPORTSETTINGSDIALOG_H
