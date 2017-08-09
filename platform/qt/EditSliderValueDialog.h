/*!
 * \file EditSliderValueDialog.h
 * \author masc4ii
 * \copyright 2017
 * \brief Dialog with a DoubleSpinBox
 */

#ifndef EDITSLIDERVALUEDIALOG_H
#define EDITSLIDERVALUEDIALOG_H

#include <QDialog>
#include <DoubleClickLabel.h>
#include <QSlider>
#include "ui_EditSliderValueDialog.h"

namespace Ui {
class EditSliderValueDialog;
}

class EditSliderValueDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EditSliderValueDialog(QWidget *parent = 0);
    ~EditSliderValueDialog();
    Ui::EditSliderValueDialog *ui;
    void autoSetup( QSlider *slider, DoubleClickLabel *label, double precision, int decimals, double factor );
    double getValue( void );

private:
    double m_factor;
};

#endif // EDITSLIDERVALUEDIALOG_H
