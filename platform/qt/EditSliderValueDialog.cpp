/*!
 * \file EditSliderValueDialog.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Dialog with a DoubleSpinBox
 */

#include "EditSliderValueDialog.h"

//Constructor
EditSliderValueDialog::EditSliderValueDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditSliderValueDialog)
{
    ui->setupUi(this);
#ifdef WIN32
    setWindowFlags( Qt::Dialog | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint );
#else
    setWindowFlags( Qt::Tool | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint );
#endif
    m_factor = 1.0;
}

//Destructor
EditSliderValueDialog::~EditSliderValueDialog()
{
    delete ui;
}

//Autosetup as much as possible in one step
void EditSliderValueDialog::autoSetup(QSlider *slider, DoubleClickLabel *label, double precision, int decimals, double factor)
{
    m_factor = factor;
    ui->doubleSpinBox->setMinimum( slider->minimum() / factor );
    ui->doubleSpinBox->setMaximum( slider->maximum() / factor );
    ui->doubleSpinBox->setDecimals( decimals );
    ui->doubleSpinBox->setSingleStep( precision );
    ui->doubleSpinBox->setValue( label->text().toDouble() );
    ui->doubleSpinBox->selectAll();
    QPoint pos;
    pos.setX(0);
    pos.setY(0);
    pos = label->mapToGlobal( pos );
    setGeometry( pos.x(), pos.y(), 80, 20 );
}

//Get the value of the spinbox multimlied with factor
double EditSliderValueDialog::getValue( void )
{
    return ui->doubleSpinBox->value() * m_factor;
}
