/*!
 * \file MainWindowSessionLifecycle.cpp
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
//Adds the fileName to the Session List
void MainWindow::addFileToSession(QString fileName)
{
    //Save settings of actual clip (if there is one)
    if( SESSION_CLIP_COUNT > 0 )
    {
        if( !ACTIVE_RECEIPT->wasNeverLoaded() )
        {
            setReceipt( ACTIVE_RECEIPT );
        }
    }

    //Add to session list (empty Pixmap is just spacer)
    ClipInformation *clipInfo = new ClipInformation( QFileInfo(fileName).fileName(), fileName );
    m_pModel->append( clipInfo );
    m_pModel->setActiveRow( SESSION_CLIP_COUNT - 1 );

    if( ui->actionUseDefaultReceipt->isChecked() ) resetReceiptWithDefault( ACTIVE_RECEIPT );
    ACTIVE_RECEIPT->setDualIsoForced( DISO_FORCED );

    //Update App
    listViewSessionUpdate();
    qApp->processEvents();
}

int MainWindow::askToSaveCurrentSession()
{
    switch( QMessageBox::warning( this,
                                  APPNAME,
                                  tr( "Do you want to save the current session?" ),
                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                  QMessageBox::Cancel ) )
    {
    //Save
    case QMessageBox::Save:
        on_actionSaveSession_triggered();
        //Saving was aborted -> abort quit
        if( m_sessionFileName.size() == 0 )
        {
            return 1;
        }
        break;
    //Don't save
    case QMessageBox::Discard:
        break;
    //Cancel
    case QMessageBox::Escape:
    case QMessageBox::Cancel:
    default:
        return 1;
        break;
    }

    return 0;
}

//Open a session file
void MainWindow::openSession(QString fileNameSession)
{
    bool abort = false;
    bool skipAll = false;
    QXmlStreamReader Rxml;
    QFile file(fileNameSession);
    if( !file.open(QIODevice::ReadOnly | QFile::Text) )
    {
        return;
    }

    //Version of settings (values may be interpreted differently)
    int versionMasxml = 0;

    //Clear the last session
    deleteSession();

    //Parse
    Rxml.setDevice(&file);
    while( !Rxml.atEnd() )
    {
        Rxml.readNext();
        //qDebug() << "InWhile";
        if( Rxml.isStartElement() && Rxml.name() == QString( "mlv_files" ) )
        {
            //Read version string, if there is one
            if( Rxml.attributes().size() != 0 )
            {
                //qDebug() << "masxmlVersion" << Rxml.attributes().at(0).value().toInt();
                versionMasxml = Rxml.attributes().at(0).value().toInt();
            }
            //qDebug() << "StartElem";
            while( !Rxml.atEnd() && !Rxml.isEndElement() && !abort )
            {
                Rxml.readNext();
                if( Rxml.isStartElement() && Rxml.name() == QString( "clip" ) )
                {
                    //qDebug() << "Clip!" << Rxml.attributes().at(0).name() << Rxml.attributes().at(0).value();
                    QString fileName = Rxml.attributes().at(0).value().toString();
                    //If file is not there, search at alternative relative path for file
                    if( !QFile( fileName ).exists() )
                    {
                        if( Rxml.attributes().size() > 1 )
                        {
                            QString relativeName = Rxml.attributes().at(1).value().toString();
                            fileName = QDir( QFileInfo( fileNameSession ).path() ).filePath( relativeName );
                        }
                    }

                    while( !QFile( fileName ).exists() && !skipAll )
                    {
                        if( !skipAll )
                        {
                            int ret = QMessageBox::critical( this,
                                                            tr( "Open Session Error" ),
                                                            tr( "File not found: \r\n%1" ).arg( fileName ),
                                                            tr( "Skip" ),
                                                            tr( "Skip all"),
                                                            tr( "Search" )/*,
                                                            tr( "Abort" ) */);
                            if( ret == 1 )
                            {
                                skipAll = true;
                                break;
                            }
                            else if( ret == 2 )
                            {
                                QString fn = QFileDialog::getOpenFileName( this,
                                                                       tr("Search MLV path"),
                                                                       fileName,
                                                                       QFileInfo( fileName ).fileName() );
                                if( QFile( fn ).exists() )
                                {
                                    fileName = fn;
                                }
                            }
                            /*else if( ret == 3 )
                            {
                                abort = true;
                                break;
                            }*/
                            else
                            {
                                break;
                            }
                        }
                    }

                    //Mark
                    uint8_t mark = 0;
                    if( Rxml.attributes().hasAttribute( "mark" ) )
                    {
                        mark = Rxml.attributes().value( "mark" ).toUShort();
                    }

                    if( QFile( fileName ).exists() )
                    {
                        //Save last file name
                        m_lastSessionFileName = fileName;
                        //Add file to Sessionlist
                        addFileToSession( fileName );
                        //Open the file
                        if( ui->actionFastOpen->isChecked() ) openMlvForPreview( fileName );
                        else openMlv( fileName );
                        SESSION_LAST_CLIP->setFileName( fileName );
                        SESSION_LAST_CLIP->setMark( mark );

                        readXmlElementsFromFile( &Rxml, SESSION_LAST_CLIP, versionMasxml );
                        setSliders( SESSION_LAST_CLIP, false );
                        previewPicture( SESSION_CLIP_COUNT - 1 );
                        setMarkColor( SESSION_CLIP_COUNT - 1, mark );
                        m_pModel->setActiveRow( SESSION_CLIP_COUNT - 1 );
                    }
                    else
                    {
                        //If file does not exist we just parse uninteresting data in the right way
                        while( !Rxml.atEnd() && !Rxml.isEndElement() )
                        {
                            Rxml.readNext();
                            if( Rxml.isStartElement() ) //future features
                            {
                                Rxml.readElementText();
                                Rxml.readNext();
                            }
                        }
                    }
                    Rxml.readNext();
                }
                else if( Rxml.isEndElement() )
                {
                    //qDebug() << "EndElement! (clip)";
                    Rxml.readNext();
                }
            }
        }
    }

    file.close();

    //Caching is in which state? Set it!
    if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();

    if (Rxml.hasError())
    {
        QMessageBox::critical( this, tr( "Open Session" ), tr( "Error: Failed to parse file! %1" )
                               .arg( Rxml.errorString() ) );
        return;
    }
    else if (file.error() != QFile::NoError)
    {
        QMessageBox::critical( this, tr( "Open Session" ), tr( "Error: Cannot read file! %1" ).arg( file.errorString() ) );
        return;
    }

    m_pRecentFilesMenu->addRecentFile( QDir::toNativeSeparators( fileNameSession ) );
}

//Save Session
void MainWindow::saveSession(QString fileName)
{
    //Save slider receipt
    setReceipt( ACTIVE_RECEIPT );

    QFile file(fileName);
    file.open(QIODevice::WriteOnly);

    QXmlStreamWriter xmlWriter(&file);
    xmlWriter.setAutoFormatting(true);
    xmlWriter.writeStartDocument();

    xmlWriter.writeStartElement( "mlv_files" );
    xmlWriter.writeAttribute( "version", "4" );
    xmlWriter.writeAttribute( "mlvapp", VERSION );
    for( int i = 0; i < SESSION_CLIP_COUNT; i++ )
    {
        xmlWriter.writeStartElement( "clip" );
        xmlWriter.writeAttribute( "file", GET_CLIP(i)->getPath() );
        xmlWriter.writeAttribute( "relative", QDir( QFileInfo( fileName ).path() ).relativeFilePath( GET_CLIP(i)->getPath() ) );
        xmlWriter.writeAttribute( "mark", QString( "%1" ).arg( GET_RECEIPT(i)->mark() ) );
        writeXmlElementsToFile( &xmlWriter, GET_RECEIPT(i) );
        xmlWriter.writeEndElement();
    }
    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();

    file.close();

    m_pRecentFilesMenu->addRecentFile( QDir::toNativeSeparators( fileName ) );
}

//Read all receipt elements from xml
void MainWindow::readXmlElementsFromFile(QXmlStreamReader *Rxml, ReceiptSettings *receipt, int version)
{
    //Compatibility for Cam Matrix (files without the tag will disable it
    receipt->setCamMatrixUsed( 0 );

    //Compatibility for old saved dual iso projects
    receipt->setDualIsoForced( DISO_FORCED );
    receipt->setDualIsoAutoCorrected( 1 );
    receipt->setDualIsoPattern( 0 );
    receipt->setDualIsoEvCorrection( 1 );
    receipt->setDualIsoBlackDelta( -1 );

    //Read tags
    while( !Rxml->atEnd() && !Rxml->isEndElement() )
    {
        Rxml->readNext();

        if( Rxml->isStartElement() && Rxml->name() == QString( "exposure" ) )
        {
            receipt->setExposure( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "contrast" ) )
        {
            receipt->setContrast( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "pivot" ) )
        {
            receipt->setPivot( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "temperature" ) )
        {
            receipt->setTemperature( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "tint" ) )
        {
            receipt->setTint( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "clarity" ) )
        {
            receipt->setClarity( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vibrance" ) )
        {
            receipt->setVibrance( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "saturation" ) )
        {
            if( version < 2 ) receipt->setSaturation( ( Rxml->readElementText().toInt() * 2.0 ) - 100.0 );
            else receipt->setSaturation( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "ls" ) )
        {
            if( version < 2 ) receipt->setLs( Rxml->readElementText().toInt() * 10.0 / FACTOR_LS );
            else receipt->setLs( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lr" ) )
        {
            receipt->setLr( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "ds" ) )
        {
            if( version < 2 ) receipt->setDs( Rxml->readElementText().toInt() * 10.0 / FACTOR_DS );
            else receipt->setDs( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dr" ) )
        {
            receipt->setDr( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lightening" ) )
        {
            if( version < 2 ) receipt->setLightening( Rxml->readElementText().toInt() / FACTOR_LIGHTEN );
            else receipt->setLightening( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "shadows" ) )
        {
            receipt->setShadows( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "highlights" ) )
        {
            receipt->setHighlights( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradationCurve" ) )
        {
            receipt->setGradationCurve( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "hueVsHue" ) )
        {
            receipt->setHueVsHue( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "hueVsSaturation" ) )
        {
            receipt->setHueVsSaturation( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "hueVsLuminance" ) )
        {
            receipt->setHueVsLuminance( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lumaVsSaturation" ) )
        {
            receipt->setLumaVsSaturation( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientEnabled" ) )
        {
            receipt->setGradientEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientExposure" ) )
        {
            receipt->setGradientExposure( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientContrast" ) )
        {
            receipt->setGradientContrast( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientStartX" ) )
        {
            receipt->setGradientStartX( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientStartY" ) )
        {
            receipt->setGradientStartY( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientLength" ) )
        {
            receipt->setGradientLength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gradientAngle" ) )
        {
            receipt->setGradientAngle( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "sharpen" ) )
        {
            receipt->setSharpen( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "sharpenMasking" ) )
        {
            receipt->setShMasking( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "chromaBlur" ) )
        {
            receipt->setChromaBlur( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "highlightReconstruction" ) )
        {
            receipt->setHighlightReconstruction( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "camMatrixUsed" ) )
        {
            receipt->setCamMatrixUsed( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "chromaSeparation" ) )
        {
            receipt->setChromaSeparation( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "profile" ) )
        {
            uint8_t profile = (uint8_t)Rxml->readElementText().toUInt();
            if( version < 2 && profile > 1 ) receipt->setProfile( profile + 2 );
            else if( version == 2 )
            {
                receipt->setProfile( profile + 1 );
                receipt->setGamut( GAMUT_Rec709 );
                if( ( profile != PROFILE_ALEXA_LOG )
                 && ( profile != PROFILE_CINEON_LOG )
                 && ( profile != PROFILE_SONY_LOG_3 )
                 && ( profile != PROFILE_SRGB )
                 && ( profile != PROFILE_REC709 )
                 && ( profile != PROFILE_DWG_INT ) )
                {
                    receipt->setAllowCreativeAdjustments( true );
                }
                else
                {
                    receipt->setAllowCreativeAdjustments( false );
                }
                switch( profile )
                {
                case PROFILE_STANDARD:
                case PROFILE_TONEMAPPED:
                    receipt->setGamma( 315 );
                    break;
                case PROFILE_FILM:
                    receipt->setGamma( 346 );
                    break;
                default:
                    receipt->setGamma( 100 );
                    break;
                }
            }
            //else receipt->setProfile( profile ); //never load for v3, because we now have single settings
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "tonemap" ) )
        {
            receipt->setTonemap( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "transferFunction" ) )
        {
            receipt->setTransferFunction( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gamut" ) )
        {
            receipt->setGamut( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "gamma" ) )
        {
            receipt->setGamma( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "allowCreativeAdjustments" ) )
        {
            receipt->setAllowCreativeAdjustments( (bool)Rxml->readElementText().toInt() );
            if( version == 2 )
            {
                int profile = receipt->profile();
                if( ( profile != PROFILE_ALEXA_LOG )
                 && ( profile != PROFILE_CINEON_LOG )
                 && ( profile != PROFILE_SONY_LOG_3 )
                 && ( profile != PROFILE_SRGB )
                 && ( profile != PROFILE_REC709 )
                 && ( profile != PROFILE_DWG_INT ) )
                {
                    receipt->setAllowCreativeAdjustments( true );
                }
            }
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "exrMode" ) )
        {
            receipt->setExrMode( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "agx" ) )
        {
            receipt->setAgx( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "denoiserWindow" ) )
        {
            receipt->setDenoiserWindow( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "denoiserStrength" ) )
        {
            receipt->setDenoiserStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rbfDenoiserLuma" ) )
        {
            receipt->setRbfDenoiserLuma( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rbfDenoiserChroma" ) )
        {
            receipt->setRbfDenoiserChroma( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rbfDenoiserRange" ) )
        {
            receipt->setRbfDenoiserRange( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "grainStrength" ) )
        {
            receipt->setGrainStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "grainLumaWeight" ) )
        {
            receipt->setGrainLumaWeight( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rawFixesEnabled" ) )
        {
            receipt->setRawFixesEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "verticalStripes" ) )
        {
            receipt->setVerticalStripes( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "focusPixels" ) )
        {
            receipt->setFocusPixels( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "fpiMethod" ) )
        {
            receipt->setFpiMethod( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "badPixels" ) )
        {
            receipt->setBadPixels( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "bpsMethod" ) )
        {
            receipt->setBpsMethod( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "bpiMethod" ) )
        {
            receipt->setBpiMethod( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "chromaSmooth" ) )
        {
            receipt->setChromaSmooth( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "patternNoise" ) )
        {
            receipt->setPatternNoise( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "deflickerTarget" ) )
        {
            receipt->setDeflickerTarget( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoForced" ) )
        {
            receipt->setDualIsoForced( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIso" ) )
        {
            receipt->setDualIso( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoPattern" ) )
        {
            receipt->setDualIsoPattern( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoEvCorrection" ) )
        {
            receipt->setDualIsoEvCorrection( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoBlackDelta" ) )
        {
            receipt->setDualIsoBlackDelta( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoInterpolation" ) )
        {
            receipt->setDualIsoInterpolation( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoAliasMap" ) )
        {
            receipt->setDualIsoAliasMap( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoFrBlending" ) )
        {
            receipt->setDualIsoFrBlending( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoWhite" ) )
        {
            receipt->setDualIsoWhite( Rxml->readElementText().toUInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "dualIsoBlack" ) )
        {
            receipt->setDualIsoBlack( Rxml->readElementText().toUInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "darkFrameFileName" ) )
        {
            receipt->setDarkFrameFileName( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "darkFrameEnabled" ) )
        {
            receipt->setDarkFrameEnabled( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rawBlack" ) )
        {
            if( version < 4 ) receipt->setRawBlack( Rxml->readElementText().toInt() * 10 );
            else receipt->setRawBlack( Rxml->readElementText().toInt() );

            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "rawWhite" ) )
        {
            receipt->setRawWhite( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "tone" ) )
        {
            receipt->setTone( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "toningStrength" ) )
        {
            receipt->setToningStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lutEnabled" ) )
        {
            receipt->setLutEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lutName" ) )
        {
            receipt->setLutName( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "lutStrength" ) )
        {
            receipt->setLutStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "filterEnabled" ) )
        {
            receipt->setFilterEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "filterIndex" ) )
        {
            receipt->setFilterIndex( Rxml->readElementText().toUInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "filterStrength" ) )
        {
            receipt->setFilterStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vignetteStrength" ) )
        {
            receipt->setVignetteStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vignetteRadius" ) )
        {
            receipt->setVignetteRadius( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vignetteShape" ) )
        {
            receipt->setVignetteShape( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "caRed" ) )
        {
            receipt->setCaRed( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "caBlue" ) )
        {
            receipt->setCaBlue( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "caDesaturate" ) )
        {
            receipt->setCaDesaturate( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "caRadius" ) )
        {
            receipt->setCaRadius( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "stretchFactorX" ) )
        {
            receipt->setStretchFactorX( Rxml->readElementText().toDouble() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "stretchFactorY" ) )
        {
            receipt->setStretchFactorY( Rxml->readElementText().toDouble() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "upsideDown" ) )
        {
            receipt->setUpsideDown( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabEnable" ) )
        {
            receipt->setVidstabEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabStepsize" ) )
        {
            receipt->setVidstabStepsize( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabShakiness" ) )
        {
            receipt->setVidstabShakiness( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabAccuracy" ) )
        {
            receipt->setVidstabAccuracy( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabZoom" ) )
        {
            receipt->setVidstabZoom( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabSmoothing" ) )
        {
            receipt->setVidstabSmoothing( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "vidstabTripod" ) )
        {
            receipt->setVidstabTripod( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "cutIn" ) )
        {
            receipt->setCutIn( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "cutOut" ) )
        {
            receipt->setCutOut( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == QString( "debayer" ) )
        {
            receipt->setDebayer( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() ) //future features
        {
            Rxml->readElementText();
            Rxml->readNext();
        }
    }
}

//Write all receipt elements to xml
void MainWindow::writeXmlElementsToFile(QXmlStreamWriter *xmlWriter, ReceiptSettings *receipt)
{
    xmlWriter->writeTextElement( "exposure",                QString( "%1" ).arg( receipt->exposure() ) );
    xmlWriter->writeTextElement( "contrast",                QString( "%1" ).arg( receipt->contrast() ) );
    xmlWriter->writeTextElement( "pivot",                   QString( "%1" ).arg( receipt->pivot() ) );
    xmlWriter->writeTextElement( "temperature",             QString( "%1" ).arg( receipt->temperature() ) );
    xmlWriter->writeTextElement( "tint",                    QString( "%1" ).arg( receipt->tint() ) );
    xmlWriter->writeTextElement( "clarity",                 QString( "%1" ).arg( receipt->clarity() ) );
    xmlWriter->writeTextElement( "vibrance",                QString( "%1" ).arg( receipt->vibrance() ) );
    xmlWriter->writeTextElement( "saturation",              QString( "%1" ).arg( receipt->saturation() ) );
    xmlWriter->writeTextElement( "ds",                      QString( "%1" ).arg( receipt->ds() ) );
    xmlWriter->writeTextElement( "dr",                      QString( "%1" ).arg( receipt->dr() ) );
    xmlWriter->writeTextElement( "ls",                      QString( "%1" ).arg( receipt->ls() ) );
    xmlWriter->writeTextElement( "lr",                      QString( "%1" ).arg( receipt->lr() ) );
    xmlWriter->writeTextElement( "lightening",              QString( "%1" ).arg( receipt->lightening() ) );
    xmlWriter->writeTextElement( "gradationCurve",          QString( "%1" ).arg( receipt->gradationCurve() ) );
    xmlWriter->writeTextElement( "hueVsHue",                QString( "%1" ).arg( receipt->hueVsHue() ) );
    xmlWriter->writeTextElement( "hueVsSaturation",         QString( "%1" ).arg( receipt->hueVsSaturation() ) );
    xmlWriter->writeTextElement( "hueVsLuminance",          QString( "%1" ).arg( receipt->hueVsLuminance() ) );
    xmlWriter->writeTextElement( "lumaVsSaturation",        QString( "%1" ).arg( receipt->lumaVsSaturation() ) );
    xmlWriter->writeTextElement( "shadows",                 QString( "%1" ).arg( receipt->shadows() ) );
    xmlWriter->writeTextElement( "highlights",              QString( "%1" ).arg( receipt->highlights() ) );
    xmlWriter->writeTextElement( "gradientEnabled",         QString( "%1" ).arg( receipt->isGradientEnabled() ) );
    xmlWriter->writeTextElement( "gradientExposure",        QString( "%1" ).arg( receipt->gradientExposure() ) );
    xmlWriter->writeTextElement( "gradientContrast",        QString( "%1" ).arg( receipt->gradientContrast() ) );
    xmlWriter->writeTextElement( "gradientStartX",          QString( "%1" ).arg( receipt->gradientStartX() ) );
    xmlWriter->writeTextElement( "gradientStartY",          QString( "%1" ).arg( receipt->gradientStartY() ) );
    xmlWriter->writeTextElement( "gradientLength",          QString( "%1" ).arg( receipt->gradientLength() ) );
    xmlWriter->writeTextElement( "gradientAngle",           QString( "%1" ).arg( receipt->gradientAngle() ) );
    xmlWriter->writeTextElement( "sharpen",                 QString( "%1" ).arg( receipt->sharpen() ) );
    xmlWriter->writeTextElement( "sharpenMasking",          QString( "%1" ).arg( receipt->shMasking() ) );
    xmlWriter->writeTextElement( "chromaBlur",              QString( "%1" ).arg( receipt->chromaBlur() ) );
    xmlWriter->writeTextElement( "highlightReconstruction", QString( "%1" ).arg( receipt->isHighlightReconstruction() ) );
    xmlWriter->writeTextElement( "camMatrixUsed",           QString( "%1" ).arg( receipt->camMatrixUsed() ) );
    xmlWriter->writeTextElement( "chromaSeparation",        QString( "%1" ).arg( receipt->isChromaSeparation() ) );
    //xmlWriter->writeTextElement( "profile",                 QString( "%1" ).arg( receipt->profile() ) );
    xmlWriter->writeTextElement( "tonemap",                 QString( "%1" ).arg( receipt->tonemap() ) );
    xmlWriter->writeTextElement( "transferFunction",        QString( "%1" ).arg( receipt->transferFunction() ) );
    xmlWriter->writeTextElement( "gamut",                   QString( "%1" ).arg( receipt->gamut() ) );
    xmlWriter->writeTextElement( "gamma",                   QString( "%1" ).arg( receipt->gamma() ) );
    xmlWriter->writeTextElement( "allowCreativeAdjustments",QString( "%1" ).arg( receipt->allowCreativeAdjustments() ) );
    xmlWriter->writeTextElement( "exrMode",                 QString( "%1" ).arg( receipt->exrMode() ) );
    xmlWriter->writeTextElement( "agx",                     QString( "%1" ).arg( receipt->agx() ) );
    xmlWriter->writeTextElement( "denoiserStrength",        QString( "%1" ).arg( receipt->denoiserStrength() ) );
    xmlWriter->writeTextElement( "denoiserWindow",          QString( "%1" ).arg( receipt->denoiserWindow() ) );
    xmlWriter->writeTextElement( "rbfDenoiserLuma",         QString( "%1" ).arg( receipt->rbfDenoiserLuma() ) );
    xmlWriter->writeTextElement( "rbfDenoiserChroma",       QString( "%1" ).arg( receipt->rbfDenoiserChroma() ) );
    xmlWriter->writeTextElement( "rbfDenoiserRange",        QString( "%1" ).arg( receipt->rbfDenoiserRange() ) );
    xmlWriter->writeTextElement( "grainStrength",           QString( "%1" ).arg( receipt->grainStrength() ) );
    xmlWriter->writeTextElement( "grainLumaWeight",         QString( "%1" ).arg( receipt->grainLumaWeight() ) );
    xmlWriter->writeTextElement( "rawFixesEnabled",         QString( "%1" ).arg( receipt->rawFixesEnabled() ) );
    xmlWriter->writeTextElement( "verticalStripes",         QString( "%1" ).arg( receipt->verticalStripes() ) );
    xmlWriter->writeTextElement( "focusPixels",             QString( "%1" ).arg( receipt->focusPixels() ) );
    xmlWriter->writeTextElement( "fpiMethod",               QString( "%1" ).arg( receipt->fpiMethod() ) );
    xmlWriter->writeTextElement( "badPixels",               QString( "%1" ).arg( receipt->badPixels() ) );
    xmlWriter->writeTextElement( "bpsMethod",               QString( "%1" ).arg( receipt->bpsMethod() ) );
    xmlWriter->writeTextElement( "bpiMethod",               QString( "%1" ).arg( receipt->bpiMethod() ) );
    xmlWriter->writeTextElement( "chromaSmooth",            QString( "%1" ).arg( receipt->chromaSmooth() ) );
    xmlWriter->writeTextElement( "patternNoise",            QString( "%1" ).arg( receipt->patternNoise() ) );
    xmlWriter->writeTextElement( "deflickerTarget",         QString( "%1" ).arg( receipt->deflickerTarget() ) );
    xmlWriter->writeTextElement( "dualIsoForced",           QString( "%1" ).arg( receipt->dualIsoForced() ) );
    xmlWriter->writeTextElement( "dualIso",                 QString( "%1" ).arg( receipt->dualIso() ) );
    xmlWriter->writeTextElement( "dualIsoPattern",          QString( "%1" ).arg( receipt->dualIsoPattern() ) );
    xmlWriter->writeTextElement( "dualIsoEvCorrection",     QString( "%1" ).arg( receipt->dualIsoEvCorrection() ) );
    xmlWriter->writeTextElement( "dualIsoBlackDelta",       QString( "%1" ).arg( receipt->dualIsoBlackDelta() ) );
    xmlWriter->writeTextElement( "dualIsoInterpolation",    QString( "%1" ).arg( receipt->dualIsoInterpolation() ) );
    xmlWriter->writeTextElement( "dualIsoAliasMap",         QString( "%1" ).arg( receipt->dualIsoAliasMap() ) );
    xmlWriter->writeTextElement( "dualIsoFrBlending",       QString( "%1" ).arg( receipt->dualIsoFrBlending() ) );
    xmlWriter->writeTextElement( "dualIsoWhite",            QString( "%1" ).arg( receipt->dualIsoWhite() ) );
    xmlWriter->writeTextElement( "dualIsoBlack",            QString( "%1" ).arg( receipt->dualIsoBlack() ) );
    xmlWriter->writeTextElement( "darkFrameFileName",       QString( "%1" ).arg( receipt->darkFrameFileName() ) );
    xmlWriter->writeTextElement( "darkFrameEnabled",        QString( "%1" ).arg( receipt->darkFrameEnabled() ) );
    xmlWriter->writeTextElement( "rawBlack",                QString( "%1" ).arg( receipt->rawBlack() ) );
    xmlWriter->writeTextElement( "rawWhite",                QString( "%1" ).arg( receipt->rawWhite() ) );
    xmlWriter->writeTextElement( "tone",                    QString( "%1" ).arg( receipt->tone() ) );
    xmlWriter->writeTextElement( "toningStrength",          QString( "%1" ).arg( receipt->toningStrength() ) );
    xmlWriter->writeTextElement( "lutEnabled",              QString( "%1" ).arg( receipt->lutEnabled() ) );
    xmlWriter->writeTextElement( "lutName",                 QString( "%1" ).arg( receipt->lutName() ) );
    xmlWriter->writeTextElement( "lutStrength",             QString( "%1" ).arg( receipt->lutStrength() ) );
    xmlWriter->writeTextElement( "filterEnabled",           QString( "%1" ).arg( receipt->filterEnabled() ) );
    xmlWriter->writeTextElement( "filterIndex",             QString( "%1" ).arg( receipt->filterIndex() ) );
    xmlWriter->writeTextElement( "filterStrength",          QString( "%1" ).arg( receipt->filterStrength() ) );
    xmlWriter->writeTextElement( "vignetteStrength",        QString( "%1" ).arg( receipt->vignetteStrength() ) );
    xmlWriter->writeTextElement( "vignetteRadius",          QString( "%1" ).arg( receipt->vignetteRadius() ) );
    xmlWriter->writeTextElement( "vignetteShape",           QString( "%1" ).arg( receipt->vignetteShape() ) );
    xmlWriter->writeTextElement( "caRed",                   QString( "%1" ).arg( receipt->caRed() ) );
    xmlWriter->writeTextElement( "caBlue",                  QString( "%1" ).arg( receipt->caBlue() ) );
    xmlWriter->writeTextElement( "caDesaturate",            QString( "%1" ).arg( receipt->caDesaturate() ) );
    xmlWriter->writeTextElement( "caRadius",                QString( "%1" ).arg( receipt->caRadius() ) );
    xmlWriter->writeTextElement( "stretchFactorX",          QString( "%1" ).arg( receipt->stretchFactorX() ) );
    xmlWriter->writeTextElement( "stretchFactorY",          QString( "%1" ).arg( receipt->stretchFactorY() ) );
    xmlWriter->writeTextElement( "upsideDown",              QString( "%1" ).arg( receipt->upsideDown() ) );
    xmlWriter->writeTextElement( "vidstabEnable",           QString( "%1" ).arg( receipt->vidStabEnabled() ) );
    xmlWriter->writeTextElement( "vidstabStepsize",         QString( "%1" ).arg( receipt->vidStabStepsize() ) );
    xmlWriter->writeTextElement( "vidstabShakiness",        QString( "%1" ).arg( receipt->vidStabShakiness() ) );
    xmlWriter->writeTextElement( "vidstabAccuracy",         QString( "%1" ).arg( receipt->vidStabAccuracy() ) );
    xmlWriter->writeTextElement( "vidstabZoom",             QString( "%1" ).arg( receipt->vidStabZoom() ) );
    xmlWriter->writeTextElement( "vidstabSmoothing",        QString( "%1" ).arg( receipt->vidStabSmoothing() ) );
    xmlWriter->writeTextElement( "vidstabTripod",           QString( "%1" ).arg( receipt->vidStabTripod() ) );
    xmlWriter->writeTextElement( "cutIn",                   QString( "%1" ).arg( receipt->cutIn() ) );
    xmlWriter->writeTextElement( "cutOut",                  QString( "%1" ).arg( receipt->cutOut() ) );
    xmlWriter->writeTextElement( "debayer",                 QString( "%1" ).arg( receipt->debayer() ) );
}

//Delete all clips from Session
void MainWindow::deleteSession()
{
    //Clear the memory
    m_pModel->clear();

    //Set window title to filename
    this->setWindowTitle( QString( "MLV App" ) );

    //disable drawing and kill old timer and old WaveFormMonitor
    m_fileLoaded = false;
    m_dontDraw = true;

    //Set Labels black
    ui->labelScope->setScope( NULL, 0, 0, false, false, ScopesLabel::None );
    m_pGraphicsItem->setPixmap( QPixmap( ":/IMG/IMG/TransDummy.png" ) );
    m_pScene->setSceneRect( 0, 0, 10, 10 );

    //Fake no audio track
    paintAudioTrack();

    resetSliders();

    //Export not possible without mlv file
    ui->actionExport->setEnabled( false );
    ui->actionExportCurrentFrame->setEnabled( false );

    //Set Clip Info to Dialog
    int rowCount = m_pInfoDialog->ui->tableWidget->rowCount();

    for( int i = 0; i < rowCount; i++ )
    {
        m_pInfoDialog->ui->tableWidget->item( i, 1 )->setText( "–" );
    }

    ui->label_resResolution->setText( "0 x 0 pixels" );

    //Adapt slider to clip and move to position 0
    ui->horizontalSliderPosition->setValue( 0 );

    //Set label
    drawFrameNumberLabel();

    //If no clip loaded, import receipt is disabled
    ui->actionImportReceipt->setEnabled( false );
    ui->actionExportReceipt->setEnabled( false );
    //If no clip loaded, disable session save
    ui->actionSaveSession->setEnabled( false );
    ui->actionSaveAsSession->setEnabled( false );
    ui->actionSaveSessionMetadata->setEnabled( false );
    //Disable select all and delete clip actions
    ui->actionSelectAllClips->setEnabled( false );
    ui->actionDeleteSelectedClips->setEnabled( false );

    //Disable Gradient
    ui->checkBoxGradientEnable->setChecked( false );
    ui->checkBoxGradientEnable->setEnabled( false );
    ui->toolButtonGradientPaint->setEnabled( false );

    //Cut In & Out
    initCutInOut( -1 );

    //Draw TimeCode
    QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( 0, 25 ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
    pic.setDevicePixelRatio( devicePixelRatio() );
    m_pTcLabel->setPixmap( pic );

    //Reset session name
    m_sessionFileName.clear();
}

//Open one of the recent sessions
void MainWindow::openRecentSession(QString fileName)
{
    if( !QFileInfo( fileName ).exists() )
    {
        m_pRecentFilesMenu->removeRecentFile( fileName );
        return;
    }

    if( SESSION_CLIP_COUNT && askToSaveCurrentSession() ) return;

    m_inOpeningProcess = true;
    openSession( fileName );
    //Show last imported file
    showFileInEditor( SESSION_CLIP_COUNT - 1 );
    m_sessionFileName = fileName;
    m_lastSessionFileName = fileName;
    m_inOpeningProcess = false;
    selectDebayerAlgorithm();
}
