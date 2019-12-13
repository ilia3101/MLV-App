/*!
 * \file PixelMapListDialog.h
 * \author masc4ii
 * \copyright 2019
 * \brief Show all installed focus pixel maps
 */

#ifndef PIXELMAPLISTDIALOG_H
#define PIXELMAPLISTDIALOG_H

#include <QDialog>
#include "../../src/mlv_include.h"

namespace Ui {
class PixelMapListDialog;
}

class PixelMapListDialog : public QDialog
{
    Q_OBJECT

public:
    enum MapType{FPM, BPM};
    explicit PixelMapListDialog(QWidget *parent = 0, MapType mapType = FPM);
    ~PixelMapListDialog();
    void showCurrentMap( mlvObject_t *pMlvObject );

private:
    Ui::PixelMapListDialog *ui;
    MapType m_mapType;
};

#endif // PIXELMAPLISTDIALOG_H
