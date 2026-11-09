/*!
 * \file EditSliderValueDialog.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Dialog with a DoubleSpinBox
 */

#include "EditSliderValueDialog.h"
#include <QLineEdit>

//Constructor
EditSliderValueDialog::EditSliderValueDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditSliderValueDialog)
{
    ui->setupUi(this);
#ifdef WIN32
    //setWindowFlags( Qt::Dialog | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint );
#else
    //setWindowFlags( Qt::Tool | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint );
#endif
    setWindowFlags( Qt::Tool | Qt::FramelessWindowHint );

    m_factor = 1.0;

    connect( ui->doubleSpinBox->findChild<QLineEdit *>(), &QLineEdit::returnPressed, this, &EditSliderValueDialog::accept );
}

//Destructor
EditSliderValueDialog::~EditSliderValueDialog()
{
    delete ui;
}

//Autosetup as much as possible in one step
void EditSliderValueDialog::autoSetup( QSlider *slider, DoubleClickLabel *label, double precision, int decimals, double factor, std::function<void()> callback )
{
    m_pSlider = slider;
    m_factor = factor;
    m_callback = callback;

    if( slider )
    {
        ui->doubleSpinBox->setMinimum( slider->minimum() / factor );
        ui->doubleSpinBox->setMaximum( slider->maximum() / factor );
    }

    ui->doubleSpinBox->setDecimals( decimals );
    ui->doubleSpinBox->setSingleStep( precision );
    ui->doubleSpinBox->setValue( label->text().remove( QRegularExpression("[^0-9.-]") ).toDouble() );
    ui->doubleSpinBox->selectAll();

    adjustSize();

    QPoint pos = label->mapToGlobal( QPoint( label->width(), 0 ) );
    setGeometry( pos.x() - width(), pos.y() + ( label->height() - height() ) / 2, width(), height() );
}

//Get the value of the spinbox multiplied with factor
double EditSliderValueDialog::getValue()
{
    return ui->doubleSpinBox->value() * m_factor;
}

void EditSliderValueDialog::accept()
{
    if( m_pSlider ) m_pSlider->setValue( getValue() );
    if( m_callback ) m_callback();

    m_pSlider = nullptr;
    m_callback = nullptr;

    QDialog::accept();
}
