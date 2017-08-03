/*!
 * \file ExportSettingsDialog.h
 * \author masc4ii
 * \copyright 2017
 * \brief Select codec
 */

#ifndef EXPORTSETTINGSDIALOG_H
#define EXPORTSETTINGSDIALOG_H

#include <QDialog>

namespace Ui {
class ExportSettingsDialog;
}

class ExportSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportSettingsDialog(QWidget *parent = 0, uint8_t currentCodecProfile = 0);
    ~ExportSettingsDialog();
    uint8_t getEncoderSetting(void);
    bool getExportAudio(void);

private:
    Ui::ExportSettingsDialog *ui;
};

#endif // EXPORTSETTINGSDIALOG_H
