/*!
 * \file MainWindowEditControls.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief The main window
 */

#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "math.h"

#include <QMessageBox>
#include <QThread>
#include <QTime>
#include <QSettings>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QDesktopWidget>
#else
#include <QWidget>
#endif
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QMimeData>
#include <QDir>
#include <QSpacerItem>
#include <QDate>
#include <QStorageInfo>
#include <QColorDialog>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef Q_OS_MACX
#include "AvfLibWrapper.h"
#include "MainWindow.h"
#endif

#include "ExportSettingsDialog.h"
#include "EditSliderValueDialog.h"
#include "DarkStyle.h"
#include "DarkStyleModern.h"
#include "Updater/updaterUI/cupdaterdialog.h"
#include "Updater/Updater.h"
#include "FcpxmlAssistantDialog.h"
#include "FcpxmlSelectDialog.h"
#include "UserManualDialog.h"
#include "StretchFactors.h"
#include "SingleFrameExportDialog.h"
#include "FpmInstaller.h"
#include "ScopesLabel.h"
#include "avir/avirthreadpool.h"
#include "MoveToTrash.h"
#include "OverwriteListDialog.h"
#include "PixelMapListDialog.h"
#include "TranscodeDialog.h"
#include "BadPixelFileHandler.h"
#include "FocusPixelMapManager.h"
#include "StatusFpmDialog.h"
#include "RenameDialog.h"

/* spaceTag argument options: ffmpeg color space tag number compliant */
#define SPACETAG_REC709   1   /* rec709 color space */
#define SPACETAG_UNKNOWN  2   /* No color space tag set */

#ifdef __cplusplus
extern "C" {
#endif

#include <../../src/mlv/camid/camera_id.h>
extern const char* camidGetCameraName(uint32_t cameraModel, int camname_type);

#ifdef __cplusplus
}
#endif

#define APPNAME "MLV App"
#define VERSION QString("%1.%2").arg(VERSION_MAJOR).arg(VERSION_MINOR)
#define GITVERSION QString("QTv%1.%2").arg(VERSION_MAJOR).arg(VERSION_MINOR)

#define FACTOR_DS       22.5
#define FACTOR_LS       11.2
#define FACTOR_LIGHTEN  0.6

#define ACTIVE_RECEIPT               m_pModel->receipt(m_pModel->activeRow())
#define GET_RECEIPT(index)           m_pModel->receipt(index)
#define ACTIVE_CLIP                  m_pModel->activeClip()
#define GET_CLIP(index)              m_pModel->clip(index)
#define SESSION_CLIP_COUNT           m_pModel->rowCount(QModelIndex())
#define SESSION_LAST_CLIP            m_pModel->receipt( SESSION_CLIP_COUNT - 1 )
#define SESSION_ACTIVE_CLIP_ROW      m_pModel->activeRow()
#define SET_ACTIVE_CLIP_IDX(index)   m_pModel->setActiveRow(index)
#define SESSION_EMPTY                m_pModel->rowCount(QModelIndex())==0

//bad pixel picking ready
void MainWindow::badPixelPicked( int x, int y )
{
    on_toolButtonBadPixelsSearchMethodEdit_toggled( true ); //Click until deactivation

    //Quit if no mlv loaded
    if( !m_fileLoaded ) return;

    //Some math if in stretch (fit) mode
    if( ui->actionZoomFit->isChecked() )
    {
        x *= getMlvWidth( m_pMlvObject ) / m_pScene->width();
        y *= getMlvHeight( m_pMlvObject ) / m_pScene->height();
    }
    else
    {
        x /= getHorizontalStretchFactor(false);
        y /= getVerticalStretchFactor(false);
    }

    //Quit if click not in picture
    if( x < 0 || y < 0 || x > getMlvWidth( m_pMlvObject ) || y > getMlvHeight( m_pMlvObject ) ) return;

    //qDebug() << "Click in Scene:" << x << y;
    //pixel in BPM available?
    if( BadPixelFileHandler::isPixelIncluded( m_pMlvObject, x, y ) )
        BadPixelFileHandler::removePixel( m_pMlvObject, x, y ); //remove it
    else
        BadPixelFileHandler::addPixel( m_pMlvObject, x, y ); //add it

    //Prepare crosses for bad pixel map
    BadPixelFileHandler::crossesPrepareAll( m_pMlvObject, &m_pBadPixelCrosses, m_pScene );
    BadPixelFileHandler::crossesRedrawAll( m_pMlvObject, &m_pBadPixelCrosses, m_pScene );
    if( ui->toolButtonBadPixelsCrosshairEnable->isChecked() )
        BadPixelFileHandler::crossesShowAll( &m_pBadPixelCrosses );

    //Refresh
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Activate & Deactivate wbPicker
void MainWindow::on_actionWhiteBalancePicker_toggled(bool checked)
{
    ui->graphicsView->setWbPickerActive( checked );
    m_pScene->setWbPickerActive( checked );
    m_pGradientElement->setMovable( !checked );
    ui->toolButtonGradientPaint->setChecked( false );
    ui->toolButtonBadPixelsSearchMethodEdit->setChecked( false );
}

//wb picking ready
void MainWindow::whiteBalancePicked( int x, int y )
{
    //ui->actionWhiteBalancePicker->setChecked( false ); //Single Click
    on_actionWhiteBalancePicker_toggled( true ); //Click until deactivation

    //Quit if no mlv loaded
    if( !m_fileLoaded ) return;

    //Some math if in stretch (fit) mode
    if( ui->actionZoomFit->isChecked() )
    {
        x *= getMlvWidth( m_pMlvObject ) / m_pScene->width();
        y *= getMlvHeight( m_pMlvObject ) / m_pScene->height();
    }
    else
    {
        x /= getHorizontalStretchFactor(false);
        y /= getVerticalStretchFactor(false);
    }

    //Quit if click not in picture
    if( x < 0 || y < 0 || x > getMlvWidth( m_pMlvObject ) || y > getMlvHeight( m_pMlvObject ) ) return;

    int temp, tint;
    //qDebug() << "Click in Scene:" << x << y;
    findMlvWhiteBalance( m_pMlvObject, ui->horizontalSliderPosition->value(), x, y, &temp, &tint, m_wbMode );
    //qDebug() << "wbTemp:" << temp << "wbTint" << tint;
    ui->horizontalSliderTemperature->setValue( temp );
    on_horizontalSliderTemperature_valueChanged( temp );
    ui->horizontalSliderTint->setValue( tint );
    on_horizontalSliderTint_valueChanged( tint );
}

//WB Picker Type change
void MainWindow::on_toolButtonWbMode_clicked()
{
    if( m_wbMode )
    {
        m_wbMode = 0;
        ui->toolButtonWbMode->setIcon( QIcon( ":/IMG/IMG/Grey-Ball-icon.png" ) );
        ui->toolButtonWbMode->setToolTip( tr( "WB picker on grey" ) );
    }
    else
    {
        m_wbMode = 1;
        ui->toolButtonWbMode->setIcon( QIcon( ":/RetinaIMG/RetinaIMG/face.png" ) );
        ui->toolButtonWbMode->setToolTip( tr( "WB picker on skin" ) );
    }
}

//Gradient anchor was selected by user
void MainWindow::gradientAnchorPicked(int x, int y)
{
    ui->checkBoxGradientEnable->setChecked( true );
    //Some math if in stretch (fit) mode
    x *= getMlvWidth( m_pMlvObject ) / m_pScene->width();
    y *= getMlvHeight( m_pMlvObject ) / m_pScene->height();

    m_pGradientElement->reset();
    m_pGradientElement->setStartPos( x, y );

    ui->spinBoxGradientX->blockSignals( true );
    ui->spinBoxGradientY->blockSignals( true );
    ui->spinBoxGradientX->setValue( x );
    ui->spinBoxGradientY->setValue( y );
    ui->spinBoxGradientX->blockSignals( false );
    ui->spinBoxGradientY->blockSignals( false );
}

//Gradient final position was selected by user
void MainWindow::gradientFinalPosPicked(int x, int y, bool isFinished)
{
    //Get both positions
    QPointF endPos = QPointF( x * getMlvWidth( m_pMlvObject ) / m_pScene->width(),
                              y * getMlvHeight( m_pMlvObject ) / m_pScene->height() );
    //Some math
    m_pGradientElement->setFinalPos( endPos.x(), endPos.y() );
    m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                               m_pScene->height(),
                                               getMlvWidth( m_pMlvObject ),
                                               getMlvHeight( m_pMlvObject ) );
    m_pGradientElement->gradientGraphicsElement()->show();

    //Set the UI numbers and sliders
    ui->labelGradientAngle->setText( QString( "%1°" ).arg( m_pGradientElement->uiAngle(), 0, 'f', 1 ) );
    ui->dialGradientAngle->blockSignals( true );
    ui->dialGradientAngle->setValue( m_pGradientElement->uiAngle() * 10.0 );
    ui->dialGradientAngle->blockSignals( false );
    ui->spinBoxGradientLength->blockSignals( true );
    ui->spinBoxGradientLength->setValue( m_pGradientElement->uiLength() );
    ui->spinBoxGradientLength->blockSignals( false );

    //If action finished, uncheck paint button
    if( isFinished )
    {
        ui->toolButtonGradientPaint->setChecked( false );
    }

    setGradientMask();
}

//Collapse & Expand Raw Correction
void MainWindow::on_groupBoxRawCorrection_toggled(bool arg1)
{
    ui->frameRawCorrection->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxRawCorrection->setMaximumHeight( 30 );
    else ui->groupBoxRawCorrection->setMaximumHeight( 16777215 );
}

//Collapse & Expand Cut In Out
void MainWindow::on_groupBoxCutInOut_toggled(bool arg1)
{
    ui->frameCutInOut->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxCutInOut->setMaximumHeight( 30 );
    else ui->groupBoxCutInOut->setMaximumHeight( 16777215 );
}

//Collapse & Expand Debayer
void MainWindow::on_groupBoxDebayer_toggled(bool arg1)
{
    ui->frameDebayer->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxDebayer->setMaximumHeight( 30 );
    else ui->groupBoxDebayer->setMaximumHeight( 16777215 );
}

void MainWindow::on_groupBoxProfiles_toggled(bool arg1)
{
    ui->frameProfiles->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxProfiles->setMaximumHeight( 30 );
    else ui->groupBoxProfiles->setMaximumHeight( 16777215 );
}

//Collapse & Expand Processing
void MainWindow::on_groupBoxProcessing_toggled(bool arg1)
{
    ui->frameProcessing->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxProcessing->setMaximumHeight( 30 );
    else ui->groupBoxProcessing->setMaximumHeight( 16777215 );
}

//Collapse & Expand Details
void MainWindow::on_groupBoxDetails_toggled(bool arg1)
{
    ui->frameDetails->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxDetails->setMaximumHeight( 30 );
    else ui->groupBoxDetails->setMaximumHeight( 16777215 );
}

//Collapse & Expand HSL box
void MainWindow::on_groupBoxHsl_toggled(bool arg1)
{
    ui->frameHsl->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxHsl->setMaximumHeight( 30 );
    else ui->groupBoxHsl->setMaximumHeight( 16777215 );
}

//Collapse & Expand Toning
void MainWindow::on_groupBoxToning_toggled(bool arg1)
{
    ui->frameToning->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxToning->setMaximumHeight( 30 );
    else ui->groupBoxToning->setMaximumHeight( 16777215 );
}

//Collapse & Expand Color Wheels
void MainWindow::on_groupBoxColorWheels_toggled(bool arg1)
{
    ui->frameColorWheels->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxColorWheels->setMaximumHeight( 30 );
    else ui->groupBoxColorWheels->setMaximumHeight( 16777215 );
}

//Collapse & Expand LUT
void MainWindow::on_groupBoxLut_toggled(bool arg1)
{
    ui->frameLut->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxLut->setMaximumHeight( 30 );
    else ui->groupBoxLut->setMaximumHeight( 16777215 );
}

//Collapse & Expand Filter
void MainWindow::on_groupBoxFilter_toggled(bool arg1)
{
    ui->frameFilter->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxFilter->setMaximumHeight( 30 );
    else ui->groupBoxFilter->setMaximumHeight( 16777215 );
}

//Collapse & Expand Vignette
void MainWindow::on_groupBoxVignette_toggled(bool arg1)
{
    ui->frameVignette->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxVignette->setMaximumHeight( 30 );
    else ui->groupBoxVignette->setMaximumHeight( 16777215 );
}

//Collapse & Expand Linear Gradient
void MainWindow::on_groupBoxLinearGradient_toggled(bool arg1)
{
    ui->frameGradient->setVisible( arg1 );
    if( !arg1 )
    {
        ui->groupBoxLinearGradient->setMaximumHeight( 30 );
        m_pGradientElement->gradientGraphicsElement()->hide();
    }
    else
    {
        if( ui->checkBoxGradientEnable->isChecked() ) m_pGradientElement->gradientGraphicsElement()->show();
        ui->groupBoxLinearGradient->setMaximumHeight( 16777215 );
    }
}

//Collapse & Expand Viewer
void MainWindow::on_groupBoxTransformation_toggled(bool arg1)
{
    ui->frameAspectRatio->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxTransformation->setMaximumHeight( 30 );
    else ui->groupBoxTransformation->setMaximumHeight( 16777215 );
}

//Paintmode for gradient enabled/disabled
void MainWindow::on_toolButtonGradientPaint_toggled(bool checked)
{
    if( !checked )
    {
        ui->graphicsView->setCrossCursorActive( false ); // has to be done first
        ui->graphicsView->setDragMode( QGraphicsView::ScrollHandDrag );
        m_pGradientElement->gradientGraphicsElement()->show();
    }
    else
    {
        ui->toolButtonBadPixelsSearchMethodEdit->setChecked( false );
        m_pGradientElement->gradientGraphicsElement()->hide();
        ui->graphicsView->setDragMode( QGraphicsView::NoDrag );
        ui->graphicsView->setCrossCursorActive( true ); // has to be done last
    }
    m_pScene->setGradientAdjustment( checked );
}

//Gradient Enable checked/unchecked
void MainWindow::on_checkBoxGradientEnable_toggled(bool checked)
{
    if( checked && ui->groupBoxLinearGradient->isChecked() ) m_pGradientElement->gradientGraphicsElement()->show();
    else m_pGradientElement->gradientGraphicsElement()->hide();

    processingSetGradientEnable( m_pProcessingObject, checked );

    m_frameChanged = true;
}

//The gradient startPoint X has changed
void MainWindow::on_spinBoxGradientX_valueChanged(int arg1)
{
    m_pGradientElement->gradientGraphicsElement()->blockSignals( true );
    m_pGradientElement->setStartPos( arg1, ui->spinBoxGradientY->value() );
    m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                               m_pScene->height(),
                                               getMlvWidth( m_pMlvObject ),
                                               getMlvHeight( m_pMlvObject ) );
    m_pGradientElement->gradientGraphicsElement()->blockSignals( false );

    //Send to processing module
    setGradientMask();
}

//The gradient startPoint Y has changed
void MainWindow::on_spinBoxGradientY_valueChanged(int arg1)
{
    m_pGradientElement->gradientGraphicsElement()->blockSignals( true );
    m_pGradientElement->setStartPos( ui->spinBoxGradientX->value(), arg1 );
    m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                               m_pScene->height(),
                                               getMlvWidth( m_pMlvObject ),
                                               getMlvHeight( m_pMlvObject ) );
    m_pGradientElement->gradientGraphicsElement()->blockSignals( false );

    //Send to processing module
    setGradientMask();
}

//The gradient length has changed
void MainWindow::on_spinBoxGradientLength_valueChanged(int arg1)
{
    m_pGradientElement->gradientGraphicsElement()->blockSignals( true );
    m_pGradientElement->setUiLength( arg1 );
    m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                               m_pScene->height(),
                                               getMlvWidth( m_pMlvObject ),
                                               getMlvHeight( m_pMlvObject ) );
    m_pGradientElement->gradientGraphicsElement()->blockSignals( false );

    //Send to processing module
    setGradientMask();
}

//The gradient angle label was doubleclicked
void MainWindow::on_labelGradientAngle_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.ui->doubleSpinBox->setMinimum( -179.9 );
    editSlider.ui->doubleSpinBox->setMaximum( 180.0 );
    editSlider.ui->doubleSpinBox->setDecimals( 1 );
    editSlider.ui->doubleSpinBox->setSingleStep( 0.1 );
    QString valString = ui->labelGradientAngle->text();
    valString.chop(1);
    editSlider.ui->doubleSpinBox->setValue( valString.toDouble() );
    editSlider.ui->doubleSpinBox->selectAll();
    QPoint pos;
    pos.setX(0);
    pos.setY(0);
    pos = ui->labelGradientAngle->mapToGlobal( pos );
    editSlider.setGeometry( pos.x(), pos.y(), 80, 20 );
    editSlider.exec();
    ui->dialGradientAngle->setValue( editSlider.getValue() * 10.0 );
}

//The gradient angle dial was turned
void MainWindow::on_dialGradientAngle_valueChanged(int value)
{
    ui->labelGradientAngle->setText( QString( "%1°" ).arg( value / 10.0, 0, 'f', 1 ) );

    m_pGradientElement->gradientGraphicsElement()->blockSignals( true );
    m_pGradientElement->setUiAngle( value / 10.0 );
    m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                               m_pScene->height(),
                                               getMlvWidth( m_pMlvObject ),
                                               getMlvHeight( m_pMlvObject ) );
    m_pGradientElement->gradientGraphicsElement()->blockSignals( false );

    //Send to processing module
    setGradientMask();
}

//Someone moved the gradient graphics element
void MainWindow::gradientGraphicElementMoved(int x, int y)
{
    //Some math if in stretch (fit) mode
    x *= getMlvWidth( m_pMlvObject ) / m_pScene->width();
    y *= getMlvHeight( m_pMlvObject ) / m_pScene->height();

    m_pGradientElement->setStartPos( x, y );

    ui->spinBoxGradientX->blockSignals( true );
    ui->spinBoxGradientY->blockSignals( true );
    ui->spinBoxGradientX->setValue( x );
    ui->spinBoxGradientY->setValue( y );
    ui->spinBoxGradientX->blockSignals( false );
    ui->spinBoxGradientY->blockSignals( false );

    //Send to processing module
    setGradientMask();
}

//Someone starts/stops hovering the element
void MainWindow::gradientGraphicElementHovered(bool isHovered)
{
    //We don't want to see hovering if wb picker is enabled
    if( ui->actionWhiteBalancePicker->isChecked() ) isHovered = false;

    //Change color of grading elements to show the user it is hovered
    QPen pen;
    if( isHovered ) pen = QPen( Qt::yellow );
    else pen = QPen( Qt::white );
    pen.setWidth( 0 );
    m_pGradientElement->gradientGraphicsElement()->setPen( pen );
}

//Set the gradient mask into processing module
void MainWindow::setGradientMask(void)
{
    //Send to processing module
    processingSetGradientMask( m_pProcessingObject,
                               getMlvWidth( m_pMlvObject ),
                               getMlvHeight( m_pMlvObject ),
                               (float)m_pGradientElement->getFinalPos().x(),
                               (float)m_pGradientElement->getFinalPos().y(),
                               (float)m_pGradientElement->getStartPos().x(),
                               (float)m_pGradientElement->getStartPos().y() );

    /*qDebug() << "Gradient" << (float)m_pGradientElement->getFinalPos().x() <<
            (float)m_pGradientElement->getFinalPos().y() <<
            (float)m_pGradientElement->getStartPos().x() <<
            (float)m_pGradientElement->getStartPos().y();*/

    m_frameChanged = true;
}

//Cut In button clicked
void MainWindow::on_toolButtonCutIn_clicked(void)
{
    if( !m_fileLoaded ) return;
    if( ui->horizontalSliderPosition->value() + 1 > ui->spinBoxCutOut->value() )
    {
        QMessageBox::warning( this, tr( "MLV App" ), tr( "Can't set cut in after cut out!" ) );
    }
    else
    {
        ui->spinBoxCutIn->setValue( ui->horizontalSliderPosition->value() + 1 );
    }
}

//Cut Out button clicked
void MainWindow::on_toolButtonCutOut_clicked(void)
{
    if( !m_fileLoaded ) return;
    if( ui->horizontalSliderPosition->value() + 1 < ui->spinBoxCutIn->value() )
    {
        QMessageBox::warning( this, tr( "MLV App" ), tr( "Can't set cut out before cut in!" ) );
    }
    else
    {
        ui->spinBoxCutOut->setValue( ui->horizontalSliderPosition->value() + 1 );
    }
}

//Cut In Delete button clicked
void MainWindow::on_toolButtonCutInDelete_clicked(void)
{
    if( !m_fileLoaded ) return;
    ui->spinBoxCutIn->setValue( 1 );
    ui->spinBoxCutOut->setMinimum( 1 );
}

//Cut Out Delete button clicked
void MainWindow::on_toolButtonCutOutDelete_clicked()
{
    if( !m_fileLoaded ) return;
    ui->spinBoxCutOut->setValue( getMlvFrames( m_pMlvObject ) );
    ui->spinBoxCutIn->setMaximum( getMlvFrames( m_pMlvObject ) );
}

//Cut In Value changed
void MainWindow::on_spinBoxCutIn_valueChanged(int arg1)
{
    ui->spinBoxCutOut->setMinimum( arg1 );

    //Refresh Timecode Label
    if( m_fileLoaded && m_tcModeDuration )
    {
        QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->spinBoxCutOut->value() - ui->spinBoxCutIn->value() + 1, getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        m_pTcLabel->setPixmap( pic );
    }
}

//Cut Out Value changed
void MainWindow::on_spinBoxCutOut_valueChanged(int arg1)
{
    ui->spinBoxCutIn->setMaximum( arg1 );

    //Refresh Timecode Label
    if( m_fileLoaded && m_tcModeDuration )
    {
        QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->spinBoxCutOut->value() - ui->spinBoxCutIn->value() + 1, getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        m_pTcLabel->setPixmap( pic );
    }
}

//Input of Stretch Width (horizontal) Factor
void MainWindow::on_comboBoxHStretch_currentIndexChanged(int index)
{
    Q_UNUSED( index );
    m_pGradientElement->setStrechFactorX( getHorizontalStretchFactor(false) );
    if( !m_inOpeningProcess && !m_setSliders ) on_horizontalSliderVignetteRadius_valueChanged( ui->horizontalSliderVignetteRadius->value() );
    resultingResolution();
    m_zoomModeChanged = true;
    m_frameChanged = true;
}

//Input of Stretch Height (vertical) Factor
void MainWindow::on_comboBoxVStretch_currentIndexChanged(int index)
{
    Q_UNUSED( index );
    m_pGradientElement->setStrechFactorY( getVerticalStretchFactor(false) );
    if( !m_inOpeningProcess && !m_setSliders ) on_horizontalSliderVignetteRadius_valueChanged( ui->horizontalSliderVignetteRadius->value() );
    resultingResolution();
    m_zoomModeChanged = true;
    m_frameChanged = true;
}

//Load Lut button pressed
void MainWindow::on_toolButtonLoadLut_clicked()
{
    if( !m_fileLoaded ) return;

    QString path = QFileInfo( m_lastLutFileName ).absolutePath();
    if( !QDir( path ).exists() ) path = QDir::homePath();

    //Open File Dialog
    QString fileName = QFileDialog::getOpenFileName( this, tr("Open cube LUT (*.cube)..."),
                                                    path,
                                                    tr("Cube LUT (*.cube *.CUBE)") );

    if( QFileInfo( fileName ).exists() && fileName.endsWith( ".cube", Qt::CaseInsensitive ) )
    {
        ui->lineEditLutName->setText( fileName );
    }
}

//Next Lut button pressed
void MainWindow::on_toolButtonNextLut_clicked()
{
    if( !m_fileLoaded || ui->lineEditLutName->text() == "" ) return;

    // Get path of lut file
    QString path = QFileInfo( ui->lineEditLutName->text() ).absolutePath();
    // Create an iterator for cube files in the directory
    QDirIterator lutFileIt(path, {"*.cube"}, QDir::Files);

    //Sort
    QStringList lutFileList;
    while( lutFileIt.hasNext() ) lutFileList.append( lutFileIt.next() );
    lutFileList.sort();

    // Find the next file from the currently chosen one
    QString fileName = lutFileList.first();
    for( int i = 0; i < lutFileList.size(); i++ )
    {
        if( lutFileList[i] > ui->lineEditLutName->text() )
        {
            fileName = lutFileList[i];
            break;
        }
    }

    //Load
    if( QFileInfo( fileName ).exists() )
    {
        ui->lineEditLutName->setText( fileName );
    }
}

//Previous Lut button pressed
void MainWindow::on_toolButtonPrevLut_clicked()
{
    if( !m_fileLoaded || ui->lineEditLutName->text() == "" ) return;

    // Get path of lut file
    QString path = QFileInfo( ui->lineEditLutName->text() ).absolutePath();
    // Create an iterator for cube files in the directory
    QDirIterator lutFileIt(path, {"*.cube"}, QDir::Files);

    //Sort
    QStringList lutFileList;
    while( lutFileIt.hasNext() ) lutFileList.append( lutFileIt.next() );
    lutFileList.sort();

    // Find the previous file from the currently chosen one
    QString fileName = lutFileList.last();
    for( int i = lutFileList.size() - 1; i >= 0; i-- )
    {
        if( lutFileList[i] < ui->lineEditLutName->text() )
        {
            fileName = lutFileList[i];
            break;
        }
    }

    //Load
    if( QFileInfo( fileName ).exists() )
    {
        ui->lineEditLutName->setText( fileName );
    }
}

//LUT filename changed
void MainWindow::on_lineEditLutName_textChanged(const QString &arg1)
{
    if( !m_fileLoaded || !m_pProcessingObject ) return;

    if( QFileInfo( arg1 ).exists() && arg1.endsWith( ".cube", Qt::CaseInsensitive ) )
    {
#ifdef Q_OS_UNIX
        QByteArray lutName = arg1.toUtf8();
#else
        QByteArray lutName = arg1.toLatin1();
#endif
        char errorMessage[256] = { 0 };
        int ret = load_lut( m_pProcessingObject->lut, lutName.data(), errorMessage );
        if( ret < 0 )
        {
            QMessageBox::critical( this, tr( "Error" ), tr( "%1" ).arg( errorMessage ), QMessageBox::Cancel, QMessageBox::Cancel );
            ui->lineEditLutName->setText( "" );
            unload_lut( m_pProcessingObject->lut );
            return;
        }
        m_lastLutFileName = arg1;
    }
    else
    {
        unload_lut( m_pProcessingObject->lut );
        ui->lineEditLutName->setText( "" );
    }

    m_frameChanged = true;
}
