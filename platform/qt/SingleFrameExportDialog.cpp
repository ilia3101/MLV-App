/*!
 * \file SingleFrameExportDialog.cpp
 * \author masc4ii, bouncyball
 * \copyright 2018
 * \brief A class for exporting single frames
 */

#include "SingleFrameExportDialog.h"
#include "ui_SingleFrameExportDialog.h"
#include "StretchFactors.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDebug>
#include "avir/avirthreadpool.h"

//Constructor
SingleFrameExportDialog::SingleFrameExportDialog(QWidget *parent,
                                                 mlvObject_t *pMlvObject,
                                                 QString fileName,
                                                 int frameNr,
                                                 double stretchX,
                                                 double stretchY) :
    QDialog(parent),
    ui(new Ui::SingleFrameExportDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint );

    m_pMlvObject = pMlvObject;
    m_fileName = fileName;
    m_frameNr = frameNr;
    m_stretchX = stretchX;
    m_stretchY = stretchY;

    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    ui->comboBoxCodec->setCurrentIndex( set.value( "singleFrameExportCodec", 0 ).toUInt() );
    m_lastPath = set.value( "lastSingleFramePath", QDir::homePath() ).toString();
}

//Destructor
SingleFrameExportDialog::~SingleFrameExportDialog()
{
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    set.setValue( "singleFrameExportCodec", ui->comboBoxCodec->currentIndex() );
    set.setValue( "lastSingleFramePath", m_lastPath );
    delete ui;
}

//Cancel dialog
void SingleFrameExportDialog::on_pushButtonCancel_clicked()
{
    this->reject();
}

//Ok and Export
void SingleFrameExportDialog::on_pushButtonOk_clicked()
{
    if( ui->comboBoxCodec->currentIndex() > 1 ) exportViaQt();
    else if( ui->comboBoxCodec->currentIndex() <= 1 ) exportDng();
    this->accept();
}

//Qt export channel
void SingleFrameExportDialog::exportViaQt()
{
    double stretchX = m_stretchX;
    double stretchY = m_stretchY;
    if( m_stretchY == STRETCH_V_033 ) //Upscale only
    {
        stretchY = 1.0;
        stretchX *= 3.0;
    }

    //File name proposal
    QString saveFileName = m_fileName;
    saveFileName = saveFileName.left( saveFileName.lastIndexOf( "." ) );
    if( ui->comboBoxCodec->currentIndex() == 2 )
        saveFileName.append( QString( "_frame_%1.png" ).arg( m_frameNr + 1 ) );
    else
        saveFileName.append( QString( "_frame_%1.jpg" ).arg( m_frameNr + 1 ) );

    //But take the folder from last export
    saveFileName = QString( "%1/%2" ).arg( m_lastPath ).arg( QFileInfo( saveFileName ).fileName() );

    //File Dialog
    QString fileName;
    if( ui->comboBoxCodec->currentIndex() == 2 )
        fileName = QFileDialog::getSaveFileName( this, tr("Export..."),
                                                 saveFileName,
                                                 "8bit PNG (*.png)" );
    else
        fileName = QFileDialog::getSaveFileName( this, tr("Export..."),
                                                 saveFileName,
                                                 "8bit JPG (*.jpg)" );

    //Exit if not an PNG file or aborted
    if( fileName == QString( "" )
            || !( fileName.endsWith( ".png", Qt::CaseInsensitive )
               || fileName.endsWith( ".jpg", Qt::CaseInsensitive ) ) ) return;

    //Save last path for next time
    m_lastPath = QFileInfo( fileName ).absolutePath();

    //Get frame from library
    uint8_t *pRawImage = (uint8_t*)malloc( 3 * getMlvWidth(m_pMlvObject) * getMlvHeight(m_pMlvObject) * sizeof( uint8_t ) );
    getMlvProcessedFrame8( m_pMlvObject, m_frameNr, pRawImage, 1 );

    uint8_t * imgBufferScaled8;
    imgBufferScaled8 = ( uint8_t* )malloc( getMlvWidth(m_pMlvObject) * stretchX * getMlvHeight(m_pMlvObject) * stretchY * 3 * sizeof( uint8_t ) );

    avir_scale_thread_pool scaling_pool;
    avir::CImageResizerVars vars; vars.ThreadPool = &scaling_pool;
    avir::CImageResizerParamsUltra roptions;
    avir::CImageResizer<> image_resizer( 8, 0, roptions );
    image_resizer.resizeImage( pRawImage,
                                getMlvWidth(m_pMlvObject),
                                getMlvHeight(m_pMlvObject), 0,
                                imgBufferScaled8,
                                getMlvWidth(m_pMlvObject) * stretchX,
                                getMlvHeight(m_pMlvObject) * stretchY,
                                3, 0, &vars );

    if( ui->comboBoxCodec->currentIndex() == 2 )
        QImage( ( unsigned char *) imgBufferScaled8, getMlvWidth(m_pMlvObject) * stretchX, getMlvHeight(m_pMlvObject) * stretchY, QImage::Format_RGB888 )
                .save( fileName, "png", -1 );
    else
        QImage( ( unsigned char *) imgBufferScaled8, getMlvWidth(m_pMlvObject) * stretchX, getMlvHeight(m_pMlvObject) * stretchY, QImage::Format_RGB888 )
                .save( fileName, "jpg", -1 );

    free( pRawImage );
}

//DNG export channel
void SingleFrameExportDialog::exportDng()
{
    //File name proposal
    QString saveFileName = m_fileName;
    saveFileName = saveFileName.left( saveFileName.lastIndexOf( "." ) );
    saveFileName.append( QString( "_frame_%1.dng" ).arg( m_frameNr + 1 ) );
    //But take the folder from last export
    saveFileName = QString( "%1/%2" ).arg( m_lastPath ).arg( QFileInfo( saveFileName ).fileName() );

    //File Dialog
    QString fileName = QFileDialog::getSaveFileName( this, tr("Export..."), saveFileName, "Raw frame as CinemaDNG (*.dng)" );

    //Exit if not an PNG file or aborted
    if( fileName == QString( "" )
            || !fileName.endsWith( ".dng", Qt::CaseInsensitive ) ) return;

    //Save last path for next time
    m_lastPath = QFileInfo( fileName ).absolutePath();

    //Set aspect ratio of the picture
    int32_t picAR[4] = { 0 };
    //Set horizontal stretch
    if( m_stretchX == STRETCH_H_133 )
    {
        picAR[0] = 4; picAR[1] = 3;
    }
    else if( m_stretchX == STRETCH_H_150 )
    {
        picAR[0] = 3; picAR[1] = 2;
    }
    else if( m_stretchX == STRETCH_H_175 )
    {
        picAR[0] = 7; picAR[1] = 4;
    }
    else if( m_stretchX == STRETCH_H_180 )
    {
        picAR[0] = 9; picAR[1] = 5;
    }
    else if( m_stretchX == STRETCH_H_200 )
    {
        picAR[0] = 2; picAR[1] = 1;
    }
    else
    {
        picAR[0] = 1; picAR[1] = 1;
    }
    //Set vertical stretch
    if( m_stretchY == STRETCH_V_167)
    {
        picAR[2] = 5; picAR[3] = 3;
    }
    else if( m_stretchY == STRETCH_V_300)
    {
        picAR[2] = 3; picAR[3] = 1;
    }
    else if( m_stretchY == STRETCH_V_033)
    {
        picAR[2] = 1; picAR[3] = 1; picAR[0] *= 3; //Upscale only
    }
    else
    {
        picAR[2] = 1; picAR[3] = 1;
    }

    //Init DNG data struct
    dngObject_t * cinemaDng = initDngObject( m_pMlvObject, 0, ui->comboBoxCodec->currentIndex(), picAR ); // 2nd param: 0 = uncompresed, 1 = lossless

    //Save cDNG frame
#ifdef Q_OS_UNIX
    if( saveDngFrame( m_pMlvObject, cinemaDng, m_frameNr, fileName.toUtf8().data() ) )
#else
    if( saveDngFrame( m_pMlvObject, cinemaDng, m_frameNr, fileName.toLatin1().data() ) )
#endif
    {
        QMessageBox::critical( this, tr( "MLV App - Export file error" ), tr( "Could not save: %1\n" ).arg( fileName ), tr( "Cancel" ), 0, 0);
    }

    //Free DNG data struct
    freeDngObject( cinemaDng );
}
