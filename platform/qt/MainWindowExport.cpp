/*!
 * \file MainWindowExport.cpp
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

//Start Export via Pipe
void MainWindow::startExportPipe(QString fileName)
{
    bool staberr = false;
    //ffmpeg existing?
    {
#if defined __linux__ && !defined APP_IMAGE
        QFile *file = new QFile( "ffmpeg" );
#elif __WIN32__
        QFile *file = new QFile( "ffmpeg.exe" );
#else
        QFile *file = new QFile( "ffmpeg" );
#endif
        if( !file->exists() )
        {
            QMessageBox::critical( this, APPNAME, tr( "Can't access encoder ffmpeg from MLVApp application path." ) );
            exportAbort();
            //Emit Ready-Signal
            emit exportReady();
            return;
        }
    }

    //Disable GUI drawing
    m_dontDraw = true;

    //chose if we want to get amaze frames for exporting, or bilinear
    if( m_exportDebayerMode == 0 )
    {
        setMlvDontAlwaysUseAmaze( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 1 )
    {
        setMlvAlwaysUseAmaze( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 2 )
    {
        setMlvUseLmmseDebayer( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 3 )
    {
        setMlvUseIgvDebayer( m_pMlvObject );
    }
    else
    {
        switch( m_exportQueue.first()->debayer() )
        {
        case ReceiptSettings::None:
            setMlvUseNoneDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::Simple:
            setMlvUseSimpleDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::Bilinear:
            setMlvDontAlwaysUseAmaze( m_pMlvObject );
            break;
        case ReceiptSettings::LMMSE:
            setMlvUseLmmseDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::IGV:
            setMlvUseIgvDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::AMaZE:
            setMlvAlwaysUseAmaze( m_pMlvObject );
            break;
        case ReceiptSettings::AHD:
            setMlvUseAhdDebayer( m_pMlvObject );
            break;
        default:
            break;
        }
    }
    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    llrpComputeStripesOn(m_pMlvObject);
    m_pMlvObject->current_cached_frame_active = 0;
    //enable low level raw fixes (if wanted)
    if( ui->checkBoxRawFixEnable->isChecked() ) m_pMlvObject->llrawproc->fix_raw = 1;

    //StatusDialog
    m_pStatusDialog->ui->progressBar->setMaximum( m_exportQueue.first()->cutOut() - m_exportQueue.first()->cutIn() + 1 );
    m_pStatusDialog->ui->progressBar->setValue( 0 );
    m_pStatusDialog->open();

    //Audio Export
    QString wavFileName = QString( "%1.wav" ).arg( fileName.left( fileName.lastIndexOf( "." ) ) );
    QString ffmpegAudioCommand;
    ffmpegAudioCommand.clear();
    if( m_audioExportEnabled && doesMlvHaveAudio( m_pMlvObject ) )
    {
#ifdef Q_OS_UNIX
        writeMlvAudioToWaveCut( m_pMlvObject, wavFileName.toUtf8().data(), m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut() );
#else
        writeMlvAudioToWaveCut( m_pMlvObject, wavFileName.toLatin1().data(), m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut() );
#endif
        if( m_codecProfile == CODEC_H264 || m_codecProfile == CODEC_H265_8 || m_codecProfile == CODEC_H265_10 || m_codecProfile == CODEC_H265_12 )
            ffmpegAudioCommand = QString( "-i \"%1\" -c:a aac " ).arg( wavFileName );
        else if( m_codecProfile == CODEC_VP9 ) ffmpegAudioCommand = QString( "-i \"%1\" -c:a libopus " ).arg( wavFileName );
        else ffmpegAudioCommand = QString( "-i \"%1\" -c:a copy " ).arg( wavFileName );
    }

    //If audio only, exit here
    if( m_codecProfile == CODEC_AUDIO_ONLY )
    {
        //Set Status
        m_pStatusDialog->ui->progressBar->setValue( (m_exportQueue.first()->cutOut() - 1) - ( m_exportQueue.first()->cutIn() - 1 ) + 1 );
        m_pStatusDialog->ui->progressBar->repaint();
        qApp->processEvents();

        if( !doesMlvHaveAudio( m_pMlvObject ) )
        {
            //Hide Status Dialog
            m_pStatusDialog->close();
            qApp->processEvents();
            //Then show error
            int ret = QMessageBox::critical( this,
                                             tr( "MLV App - Export file error" ),
                                             tr( "No audio track available in MLV for export.\nHow do you like to proceed?" ),
                                             tr( "Continue" ),
                                             tr( "Abort batch export" ),
                                             0, 0 );
            if( ret == 1 )
            {
                exportAbort();
            }
        }

        //Delete wav file if aborted
        if( m_exportAbortPressed )
        {
            QFile *file = new QFile( wavFileName );
            if( file->exists() ) file->remove();
            delete file;
        }

        //If we don't like amaze we switch it off again
        if( !ui->actionAlwaysUseAMaZE->isChecked() ) { setMlvDontAlwaysUseAmaze( m_pMlvObject ); }

        //Enable GUI drawing
        m_dontDraw = false;

        //Emit Ready-Signal
        emit exportReady();
        return;
    }

    //HDR detection check
    bool isHdrClip = false;
    if( m_hdrExport && ( getMlvFrames( m_pMlvObject ) >= 2 ) )
    {
        //Buffer
        uint32_t frameSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
        uint16_t * imgBuffer;
        imgBuffer = ( uint16_t* )malloc( frameSize * sizeof( uint16_t ) );

        //Take 2 pics from the middle of the clip
        uint8_t frameNum = getMlvFrames( m_pMlvObject ) / 2 - 1;

        //Get 1st picture, and lock render thread... there can only be one!
        m_pRenderThread->lock();
        getMlvProcessedFrame16( m_pMlvObject, frameNum, imgBuffer, QThread::idealThreadCount() );
        m_pRenderThread->unlock();
        double average1 = 0;
        for( uint32_t i = 0; i < frameSize; i++ ) average1 += imgBuffer[i];

        //Get 2nd picture, and lock render thread... there can only be one!
        m_pRenderThread->lock();
        getMlvProcessedFrame16( m_pMlvObject, frameNum+1, imgBuffer, QThread::idealThreadCount() );
        m_pRenderThread->unlock();
        double average2 = 0;
        for( uint32_t i = 0; i < frameSize; i++ ) average2 += imgBuffer[i];

        //Compare pictures
        if( average2 == 0 ) average2 = 1;
        double quot = average1 / average2;
        if( quot > 1.3 || quot < 0.7 ) isHdrClip = true;
        //qDebug() << average1 << average2 << quot;

        free( imgBuffer );
    }

    //Solving the . and , problem at fps in the command
    QLocale locale = QLocale(QLocale::English, QLocale::UnitedKingdom);
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    QString fps = locale.toString( getFramerate() );

    //Doing something against moiree
    QString moireeFilter = QString( "" );
    if( m_smoothFilterSetting != SMOOTH_FILTER_OFF )
    {
        //minterpolate, tblend and framestep are filters. The 1st does the oversampling.
        //The 2nd, the blended frames, and 3rd reduces the stream back to original fps.
        moireeFilter = QString( "minterpolate=%1,tblend=all_mode=average,framestep=2," )
                .arg( locale.toString( getFramerate() * 2.0 ) );
        if( m_smoothFilterSetting == SMOOTH_FILTER_3PASS_USM )
        {
            moireeFilter.append( QString( "unsharp=7:7:0.8:7:7:0," ) );
        }
        else if( m_smoothFilterSetting == SMOOTH_FILTER_3PASS_USM_BB )
        {
            moireeFilter.append( QString( "unsharp=5:5:0.6:5:5:0," ) );
        }
    }

    //HDR and blending
    QString hdrString = QString( "" );
    if( m_hdrExport && isHdrClip )
        hdrString = QString( ",tblend=all_mode=average" );

    if( m_codecProfile == CODEC_TIFF && m_codecOption == CODEC_TIFF_AVG )
    {
        int frames = m_exportQueue.first()->cutOut() - m_exportQueue.first()->cutIn() + 1;
        if( frames > 128 ) frames = 128;
        hdrString = QString( ",tmix=frames=%1" ).arg( frames );
    }

    //Vidstab, 2nd pass
    QString vidstabString = QString( "" );
#ifdef Q_OS_WIN
    QString vidstabFile = QString( "\"tmp_transform_vectors.trf\"" );
#elif defined( Q_OS_LINUX )
    QString vidstabFile = QString( "\"%1/tmp_transform_vectors.trf\"" )
            .arg( QFileInfo( m_exportQueue.first()->fileName() ).absolutePath() );
#else
    QString vidstabFile = QString( "\"%1/tmp_transform_vectors.trf\"" ).arg( QCoreApplication::applicationDirPath() );
#endif
    if( m_exportQueue.first()->vidStabEnabled() && m_codecProfile == CODEC_H264 )
    {
        if( m_exportQueue.first()->vidStabTripod() )
        {
            vidstabString = QString( ",vidstabtransform=input=%1:tripod=1" )
                .arg( vidstabFile );
        }
        else
        {
            vidstabString = QString( ",vidstabtransform=input=%1:zoom=%2:smoothing=%3" )
                .arg( vidstabFile )
                .arg( m_exportQueue.first()->vidStabZoom() )
                .arg( m_exportQueue.first()->vidStabSmoothing() );
        }
    }

    //Colorspace conversion (for getting right colors)
    QString resizeFilter = QString( "" );
    //a colorspace conversion is always needed to get right colors
    resizeFilter = QString( "-vf %1scale=in_color_matrix=bt601:out_color_matrix=bt709%2%3 " )
            .arg( moireeFilter )
            .arg( hdrString )
            .arg( vidstabString );
    //qDebug() << resizeFilter;

    //Color tag
    int colorTag;
    if( m_exportQueue.first()->profile() == PROFILE_STANDARD
     || m_exportQueue.first()->profile() == PROFILE_TONEMAPPED
     || m_exportQueue.first()->profile() == PROFILE_FILM
     || m_exportQueue.first()->profile() == PROFILE_SRGB
     || m_exportQueue.first()->profile() == PROFILE_REC709 )
        colorTag = SPACETAG_REC709;
    else
        colorTag = SPACETAG_UNKNOWN;

    //Dimension & scaling
    uint16_t width = getMlvWidth(m_pMlvObject);
    uint16_t height = getMlvHeight(m_pMlvObject);
    bool scaled = false;
    if( m_resizeFilterEnabled )
    {
        //Autocalc height
        if( m_resizeFilterHeightLocked )
        {
            height = (double)m_resizeWidth / (double)getMlvWidth( m_pMlvObject )
                    / m_exportQueue.first()->stretchFactorX()
                    * m_exportQueue.first()->stretchFactorY()
                    * (double)getMlvHeight( m_pMlvObject ) + 0.5;
        }
        else
        {
            height = m_resizeHeight;
        }
        width = m_resizeWidth;
        scaled = true;
    }
    else if( m_exportQueue.first()->stretchFactorX() != 1.0
          || m_exportQueue.first()->stretchFactorY() != 1.0 )
    {
        //Upscale only
        if( m_exportQueue.first()->stretchFactorY() == STRETCH_V_033 )
        {
            width = getMlvWidth( m_pMlvObject ) * 3;
            height = getMlvHeight( m_pMlvObject );
        }
        else
        {
            width = getMlvWidth( m_pMlvObject ) * m_exportQueue.first()->stretchFactorX();
            height = getMlvHeight( m_pMlvObject ) * m_exportQueue.first()->stretchFactorY();
        }
        scaled = true;
    }
    if( m_codecProfile == CODEC_H264
     || m_codecProfile == CODEC_H265_8 || m_codecProfile == CODEC_H265_10 || m_codecProfile == CODEC_H265_12 )
    {
        if( width != width + (width % 2) )
        {
            width += width % 2;
            scaled = true;
        }
        if( height != height + (height % 2) )
        {
            height += height % 2;
            scaled = true;
        }
    }
    else if( m_codecProfile == CODEC_CINEFORM_10 || m_codecProfile == CODEC_CINEFORM_12 ) // resolution must be multiple of 16
    {
        if( width != width + (width % 16) )
        {
            width += width % 16;
            scaled = true;
        }
        if( height != height + (height % 16) )
        {
            height += height % 16;
            scaled = true;
        }
    }

    //FFMpeg export
#if defined __linux__ && !defined APP_IMAGE
    QString program = QString( "ffmpeg" );
#elif __WIN32__
    QString program = QString( "ffmpeg" );
#else
    QString program = QCoreApplication::applicationDirPath();
    program.append( QString( "/ffmpeg\"" ) );
    program.prepend( QString( "\"" ) );
#endif

#ifdef STDOUT_SILENT
    program.append( QString( " -loglevel 0" ) );
#endif

    //We need it later for multipass
    QString ffmpegCommand = program;

    QString output = fileName.left( fileName.lastIndexOf( "." ) );
    QString resolution = QString( "%1x%2" ).arg( width ).arg( height );

    //VidStab: First pass
    if( m_exportQueue.first()->vidStabEnabled() && m_codecProfile == CODEC_H264 )
    {
        QString stabCmd;
        if( m_exportQueue.first()->vidStabTripod() )
        {
            stabCmd = QString( "%1 -r %2 -y -f rawvideo -s %3 -pix_fmt rgb48 -i - -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -i - -vf vidstabdetect=tripod=1:result=%4 -f null -" )
                        .arg( program )
                        .arg( fps )
                        .arg( resolution )
                        .arg( vidstabFile );
        }
        else
        {
            stabCmd = QString( "%1 -r %2 -y -f rawvideo -s %3 -pix_fmt rgb48 -i - -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -i - -vf vidstabdetect=stepsize=%5:shakiness=%6:accuracy=%7:result=%4 -f null -" )
                        .arg( program )
                        .arg( fps )
                        .arg( resolution )
                        .arg( vidstabFile )
                        .arg( m_exportQueue.first()->vidStabStepsize() )
                        .arg( m_exportQueue.first()->vidStabShakiness() )
                        .arg( m_exportQueue.first()->vidStabAccuracy() );
        }

        //Try to open pipe
        FILE *pPipeStab;
        //qDebug() << "Call ffmpeg:" << stabCmd;
    #ifdef Q_OS_UNIX
        if( !( pPipeStab = popen( stabCmd.toUtf8().data(), "w" ) ) )
    #else
        if( !( pPipeStab = popen( stabCmd.toLatin1().data(), "wb" ) ) )
    #endif
        {
            QMessageBox::critical( this, tr( "File export failed" ), tr( "Could not export with ffmpeg." ) );
        }
        else
        {
            //Buffer
            uint32_t frameSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
            uint16_t * imgBuffer;
            imgBuffer = ( uint16_t* )malloc( frameSize * sizeof( uint16_t ) );

            //Frames in the export queue?!
            int totalFrames = 0;
            for( int i = 0; i < m_exportQueue.size(); i++ )
            {
                totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
            }

            //Build buffer
            uint16_t * imgBufferScaled;
            imgBufferScaled = ( uint16_t* )malloc( width * height * 3 * sizeof( uint16_t ) );

            //Get all pictures and send to pipe
            for( uint32_t i = (m_exportQueue.first()->cutIn() - 1); i < m_exportQueue.first()->cutOut(); i++ )
            {
                if( m_codecProfile == CODEC_TIFF && m_codecOption == CODEC_TIFF_AVG && i > 128 ) break;

                if( scaled )
                {
                    //Get picture, and lock render thread... there can only be one!
                    m_pRenderThread->lock();
                    getMlvProcessedFrame16( m_pMlvObject, i, imgBuffer, QThread::idealThreadCount() );
                    m_pRenderThread->unlock();

                    avir_scale_thread_pool scaling_pool;
                    avir::CImageResizerVars vars; vars.ThreadPool = &scaling_pool;
                    avir::CImageResizerParamsUltra roptions;
                    avir::CImageResizer<> image_resizer( 16, 0, roptions );
                    image_resizer.resizeImage( imgBuffer,
                                               getMlvWidth(m_pMlvObject),
                                               getMlvHeight(m_pMlvObject), 0,
                                               imgBufferScaled,
                                               width,
                                               height,
                                               3, 0, &vars );

                    //Write to pipe
                    fwrite(imgBufferScaled, sizeof( uint16_t ), width * height * 3, pPipeStab);
                    fflush(pPipeStab);
                }
                else
                {
                    //Get picture, and lock render thread... there can only be one!
                    m_pRenderThread->lock();
                    getMlvProcessedFrame16( m_pMlvObject, i, imgBuffer, QThread::idealThreadCount() );
                    m_pRenderThread->unlock();

                    //Write to pipe
                    fwrite(imgBuffer, sizeof( uint16_t ), frameSize, pPipeStab);
                    fflush(pPipeStab);
                }

                //Set Status
                m_pStatusDialog->ui->progressBar->setValue( ( i - ( m_exportQueue.first()->cutIn() - 1 ) + 1 ) >> 1 );
                m_pStatusDialog->ui->progressBar->repaint();
                m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - ( ( i - ( m_exportQueue.first()->cutIn() - 1 ) + 1 ) >> 1 ) );
                qApp->processEvents();

                //Check diskspace
                checkDiskFull( fileName );
                //Abort pressed? -> End the loop
                if( m_exportAbortPressed ) break;
            }
            //Close pipe
            if( pclose( pPipeStab ) != 0 )
            {
                staberr = true;
                QMessageBox::critical( this, tr( "File export failed" ), tr( "FFmpeg closed unexpectedly during stabilization.\n\nFile %1 was not exported completely." ).arg( fileName ) );
            }
            free( imgBufferScaled );
            free( imgBuffer );
        }
    }

    if( m_codecProfile == CODEC_TIFF )
    {
        if( m_codecOption == CODEC_TIFF_SEQ )
        {
            //Creating a folder with the initial filename
            QString folderName = QFileInfo( fileName ).path();
            QString shortFileName = QFileInfo( fileName ).fileName();
            folderName.append( "/" )
                    .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) );

            QDir dir;
            dir.mkpath( folderName );

            //Now add the numbered filename
            output = folderName;
            output.append( "/" )
                    .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) )
                    .append( QString( "_%06d.tif" ) );

            program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v tiff -pix_fmt %3 -start_number %4 -color_primaries %5 -color_trc %5 -colorspace bt709 %6\"%7\"" )
                        .arg( fps )
                        .arg( resolution )
                        .arg( "rgb48" )
                        .arg( m_exportQueue.first()->cutIn() - 1 )
                        .arg( colorTag )
                        .arg( resizeFilter )
                        .arg( output ) );

            //copy wav to the location, ffmpeg does not like to do it for us :-(
            if( m_audioExportEnabled && doesMlvHaveAudio( m_pMlvObject ) )
            {
                QFile::copy( wavFileName, QString( "%1/%2.wav" ).arg( folderName ).arg( shortFileName.left( shortFileName.lastIndexOf( "." ) ) ) );
            }
            //Setup for scripting
            m_pScripting->setNextScriptInputTiff( getMlvFramerate( m_pMlvObject ), folderName );
        }
        else
        {
            output.append( QString( ".tif" ) );
            program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v tiff -pix_fmt %3 -color_primaries %4 -color_trc %4 -colorspace bt709 %5\"%6\"" )
                        .arg( fps )
                        .arg( resolution )
                        .arg( "rgb48" )
                        .arg( colorTag )
                        .arg( resizeFilter )
                        .arg( output ) );
        }
    }
    else if( m_codecProfile == CODEC_PNG )
    {
        //Creating a folder with the initial filename
        QString folderName = QFileInfo( fileName ).path();
        QString shortFileName = QFileInfo( fileName ).fileName();
        folderName.append( "/" )
                .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) );

        QDir dir;
        dir.mkpath( folderName );

        QString pngDepth;
        if( m_codecOption == CODEC_PNG_16 ) pngDepth = "rgb48";
        else pngDepth = "rgb24";

        //Now add the numbered filename
        output = folderName;
        output.append( "/" )
                .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) )
                .append( QString( "_%06d.png" ) );

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v png -pix_fmt %3 -start_number %4 -color_primaries %5 -color_trc %5 -colorspace bt709 %6\"%7\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( pngDepth )
                    .arg( m_exportQueue.first()->cutIn() - 1 )
                    .arg( colorTag )
                    .arg( resizeFilter )
                    .arg( output ) );

        //copy wav to the location, ffmpeg does not like to do it for us :-(
        if( m_audioExportEnabled && doesMlvHaveAudio( m_pMlvObject ) )
        {
            QFile::copy( wavFileName, QString( "%1/%2.wav" ).arg( folderName ).arg( shortFileName.left( shortFileName.lastIndexOf( "." ) ) ) );
        }
        //Setup for scripting
        m_pScripting->setNextScriptInputTiff( getMlvFramerate( m_pMlvObject ), folderName );
    }
    else if( m_codecProfile == CODEC_JPG2K && m_codecOption == CODEC_JPG2K_SEQ )
    {
        //Creating a folder with the initial filename
        QString folderName = QFileInfo( fileName ).path();
        QString shortFileName = QFileInfo( fileName ).fileName();
        folderName.append( "/" )
                .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) );

        QDir dir;
        dir.mkpath( folderName );

        //Now add the numbered filename
        output = folderName;
        output.append( "/" )
                .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) )
                .append( QString( "_%06d.jp2" ) );

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v jpeg2000 -pix_fmt %3 -start_number %4 -color_primaries %5 -color_trc %5 -colorspace bt709 %6\"%7\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( "yuv444p" )
                    .arg( m_exportQueue.first()->cutIn() - 1 )
                    .arg( colorTag )
                    .arg( resizeFilter )
                    .arg( output ) );

        //copy wav to the location, ffmpeg does not like to do it for us :-(
        if( m_audioExportEnabled && doesMlvHaveAudio( m_pMlvObject ) )
        {
            QFile::copy( wavFileName, QString( "%1/%2.wav" ).arg( folderName ).arg( shortFileName.left( shortFileName.lastIndexOf( "." ) ) ) );
        }
        //Setup for scripting
        m_pScripting->setNextScriptInputTiff( getMlvFramerate( m_pMlvObject ), folderName );
    }
    else if( m_codecProfile == CODEC_JPG2K && m_codecOption == CODEC_JPG2K_MOV )
    {
        output.append( QString( ".mov" ) );

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v jpeg2000 -pix_fmt %3 -color_primaries %4 -color_trc %4 -colorspace bt709 %5\"%6\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( "yuv444p" )
                    .arg( colorTag )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_AVI )
    {
        output.append( QString( ".avi" ) );

        QString option3;
        QString option4;

        switch( m_codecOption )
        {
            case CODEC_AVI_OPTION_YUV420:
                option3 = "rawvideo";
                option4 = "yuv420p";
                break;
            case CODEC_AVI_OPTION_V210:
                option3 = "v210";
                option4 = "yuv422p10le";
                break;
             case CODEC_AVI_OPTION_BGR24:
                option3 = "rawvideo";
                option4 = "bgr24";
                break;
        }

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v %3 -pix_fmt %4 %5\"%6\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( option3 )
                    .arg( option4 )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_MJPEG )
    {
        output.append( QString( ".avi" ) );
        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v %3 -pix_fmt %4 -q:v 2 -huffman optimal -an -vtag MJPG %5\"%6\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( "mjpeg" )
                    .arg( "yuvj444p" )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_FFVHUFF )
    {
        output.append( QString( ".avi" ) );

        QString option;

        switch( m_codecOption )
        {
            case CODEC_FFVHUFF_OPTION10:
                option = "yuv444p10le";
                break;
            case CODEC_FFVHUFF_OPTION12:
                option = "yuv444p12le";
                break;
            default: //16bit
                option = "yuv444p16le";
                break;
        }

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v %3 -pix_fmt %4 %5\"%6\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( "ffvhuff" )
                    .arg( option )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_H264 )
    {
        if( m_codecOption == CODEC_H264_H_MOV || m_codecOption == CODEC_H264_M_MOV ) output.append( QString( ".mov" ) );
        else if( m_codecOption == CODEC_H264_H_MP4 || m_codecOption == CODEC_H264_M_MP4 ) output.append( QString( ".mp4" ) );
        else output.append( QString( ".mkv" ) );

        int quality;
        if( m_codecOption == CODEC_H264_H_MOV || m_codecOption == CODEC_H264_H_MP4 || m_codecOption == CODEC_H264_H_MKV )
            quality = 14;
        else
            quality = 24;

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v libx264 -preset medium -crf %3 -pix_fmt %4 -color_primaries %5 -color_trc %5 -colorspace bt709 %6\"%7\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( quality )
                    .arg( "yuv420p" )
                    .arg( colorTag )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_H265_8 || m_codecProfile == CODEC_H265_10 || m_codecProfile == CODEC_H265_12 )
    {
        QString bitdepth;
        if( m_codecProfile == CODEC_H265_8 ) bitdepth = QString( "yuv420p" );
        else if( m_codecProfile == CODEC_H265_10 ) bitdepth = QString( "yuv420p10le" );
        else bitdepth = QString( "yuv444p12le" );

        if( m_codecOption == CODEC_H265_H_MOV || m_codecOption == CODEC_H265_M_MOV ) output.append( QString( ".mov" ) );
        else if( m_codecOption == CODEC_H265_H_MP4 || m_codecOption == CODEC_H265_M_MP4 ) output.append( QString( ".mp4" ) );
        else output.append( QString( ".mkv" ) );

        int quality;
        if( m_codecOption == CODEC_H265_H_MOV || m_codecOption == CODEC_H265_H_MP4 || m_codecOption == CODEC_H265_H_MKV )
            quality = 18;
        else
            quality = 24;

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v libx265 -preset medium -crf %3 -tag:v hvc1 -pix_fmt %4 -color_primaries %5 -color_trc %5 -colorspace bt709 %6\"%7\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( quality )
                    .arg( bitdepth )
                    .arg( colorTag )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_DNXHR )
    {
        output.append( QString( ".mov" ) );

        QString option;
        QString format;

        switch( m_codecOption )
        {
        case CODEC_DNXHR_444_1080p_10bit:
            format = "-pix_fmt yuv444p10";
            option = "-profile:v dnxhr_444 ";
            break;
        case CODEC_DNXHR_HQX_1080p_10bit:
            format = "-pix_fmt yuv422p10";
            option = "-profile:v dnxhr_hqx ";
            break;
        case CODEC_DNXHR_HQ_1080p_8bit:
            format = "-pix_fmt yuv422p";
            option = "-profile:v dnxhr_hq ";
            break;
        case CODEC_DNXHR_SQ_1080p_8bit:
            format = "-pix_fmt yuv422p";
            option = "-profile:v dnxhr_sq ";
            break;
        case CODEC_DNXHR_LB_1080p_8bit:
        default:
            format = "-pix_fmt yuv422p";
            option = "-profile:v dnxhr_lb ";
            break;
        }

        QString optionFps = "";
        if( fps == QString( "23.976" ) || fps == QString( "23,976" ) || getFramerate() == 24000.0/1001.0 ) optionFps = ",fps=24000/1001";
        else if( fps == QString( "29.97" ) || fps == QString( "29,97" ) || getFramerate() == 30000.0/1001.0 ) optionFps = ",fps=30000/1001";
        else if( fps == QString( "59.94" ) || fps == QString( "59,94" ) || getFramerate() == 60000.0/1001.0 ) optionFps = ",fps=60000/1001";
        resizeFilter.insert( resizeFilter.indexOf( "=bt709" )+6, optionFps );

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v dnxhd %3%4 -color_primaries %5 -color_trc %5 -colorspace bt709 %6\"%7\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( option )
                    .arg( format )
                    .arg( colorTag )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_DNXHD )
    {
        output.append( QString( ".mov" ) );

        QString option;
        QString option2;
        QString format;
        format = "format=yuv422p10";
        option2 = "";

        bool error = false;

        if( m_codecOption == CODEC_DNXHD_1080p_10bit )
        {
            if( getFramerate() == 25.0 )                option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=25,%1%2 -b:v 185M" ).arg( format ).arg( hdrString );
            else if( getFramerate() == 50.0 )           option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=50,%1%2 -b:v 365M" ).arg( format ).arg( hdrString );
            else if( fps == QString( "23.976" ) || fps == QString( "23,976" )
                     || getFramerate() == 24000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=24000/1001,%1%2 -b:v 175M" ).arg( format ).arg( hdrString );
            else if( fps == QString( "29.97" ) || fps == QString( "29,97" )
                     || getFramerate() == 30000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=30000/1001,%1%2 -b:v 220M" ).arg( format ).arg( hdrString );
            else if( fps == QString( "59.94" ) || fps == QString( "59,94" )
                     || getFramerate() == 60000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=60000/1001,%1%2 -b:v 440M" ).arg( format ).arg( hdrString );
            else error = true;
        }
        else if( m_codecOption == CODEC_DNXHD_1080p_8bit )
        {
            if( getFramerate() == 25.0 )                option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=25,format=yuv422p%1 -b:v 185M" ).arg( hdrString );
            else if( getFramerate() == 50.0 )           option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=50,format=yuv422p%1 -b:v 365M" ).arg( hdrString );
            else if( fps == QString( "23.976" ) || fps == QString( "23,976" )
                     || getFramerate() == 24000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=24000/1001,format=yuv422p%1 -b:v 175M" ).arg( hdrString );
            else if( fps == QString( "29.97" ) || fps == QString( "29,97" )
                     || getFramerate() == 30000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=30000/1001,format=yuv422p%1 -b:v 220M" ).arg( hdrString );
            else if( fps == QString( "59.94" ) || fps == QString( "59,94" )
                     || getFramerate() == 60000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=60000/1001,format=yuv422p%1 -b:v 440M" ).arg( hdrString );
            else error = true;
        }
        else if( m_codecOption == CODEC_DNXHD_720p_10bit )
        {
            if( getFramerate() == 25.0 )                option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=25,format=yuv422p10%1 -b:v 90M" ).arg( hdrString );
            else if( getFramerate() == 50.0 )           option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=50,format=yuv422p10%1 -b:v 180M" ).arg( hdrString );
            else if( fps == QString( "23.976" ) || fps == QString( "23,976" )
                     || getFramerate() == 24000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=24000/1001,format=yuv422p10%1 -b:v 90M" ).arg( hdrString );
            else if( fps == QString( "29.97" ) || fps == QString( "29,97" )
                     || getFramerate() == 30000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=30000/1001,format=yuv422p10%1 -b:v 110M" ).arg( hdrString );
            else if( fps == QString( "59.94" ) || fps == QString( "59,94" )
                     || getFramerate() == 60000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=60000/1001,format=yuv422p10%1 -b:v 220M" ).arg( hdrString );
            else error = true;
        }
        else //720p 8bit
        {
            if( getFramerate() == 25.0 )                option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=25,format=yuv422p%1 -b:v 90M" ).arg( hdrString );
            else if( getFramerate() == 50.0 )           option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=50,format=yuv422p%1 -b:v 180M" ).arg( hdrString );
            else if( fps == QString( "23.976" ) || fps == QString( "23,976" )
                     || getFramerate() == 24000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=24000/1001,format=yuv422p%1 -b:v 90M" ).arg( hdrString );
            else if( fps == QString( "29.97" ) || fps == QString( "29,97" )
                     || getFramerate() == 30000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=30000/1001,format=yuv422p%1 -b:v 110M" ).arg( hdrString );
            else if( fps == QString( "59.94" ) || fps == QString( "59,94" )
                     || getFramerate() == 60000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=60000/1001,format=yuv422p%1 -b:v 220M" ).arg( hdrString );
            else error = true;
        }

        if( error )
        {
            QMessageBox::critical( this, tr( "File export failed" ), tr( "Unsupported framerate!" ) );
            //Emit Ready-Signal
            emit exportReady();
            return;
        }

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v dnxhd %3 -color_primaries %4 -color_trc %4 -colorspace bt709 \"%5\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( option )
                    .arg( colorTag )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_CINEFORM_10 || m_codecProfile == CODEC_CINEFORM_12 )
    {
        output.append( QString( ".mov" ) );
        int quality = m_codecOption;
        QString mode;
        if( m_codecProfile == CODEC_CINEFORM_10 ) mode = "yuv422p10le"; //10bit
        else mode = "gbrp12le"; //12bit

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v cfhd -quality %3 -pix_fmt %4 -color_primaries %5 -color_trc %5 -colorspace bt709 %6\"%7\"" )
                           .arg( fps )
                           .arg( resolution )
                           .arg( quality )
                           .arg( mode )
                           .arg( colorTag )
                           .arg( resizeFilter )
                           .arg( output ) );
    }
    else if( m_codecProfile == CODEC_VP9 )
    {
        output.append( QString( ".webm" ) );

        QString quality;
        if( m_codecOption == CODEC_VP9_LOSSLESS )
            quality = "-lossless 1";
        else
            quality = "-crf 18 -b:v 0";

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v libvpx-vp9 %3 -pix_fmt %4 -color_primaries %5 -color_trc %5 -colorspace bt709 %6\"%7\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( quality )
                    .arg( "yuv420p" )
                    .arg( colorTag )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else
    {
        QString option;
        if( m_codecProfile <= CODEC_PRORES422HQ && m_codecOption == CODEC_PRORES_OPTION_AW ) option = QString( "prores_aw" );
        else option = QString( "prores_ks" );
        QString pixFmt;
        if( m_codecProfile <= CODEC_PRORES422HQ ) pixFmt = QString( "yuv422p10" );
        else pixFmt = QString( "yuv444p10" );

        output.append( QString( ".mov" ) );
        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v %3 -profile:v %4 -pix_fmt %5 -color_primaries %6 -color_trc %6 -colorspace bt709 %7\"%8\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( option )
                    .arg( m_codecProfile )
                    .arg( pixFmt )
                    .arg( colorTag )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    //There is a %5 in the string, so another arg is not possible - so do that:
    program.insert( program.indexOf( "-c:v" ), ffmpegAudioCommand );

    //Do 3pass filtering!
    if( m_smoothFilterSetting == SMOOTH_FILTER_3PASS || m_smoothFilterSetting == SMOOTH_FILTER_3PASS_USM )
    {
        QString pass3 = QString( "-vf minterpolate=%2,tblend=all_mode=average,framestep=2 -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -i - -vf minterpolate=%2,tblend=all_mode=average,framestep=2 -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -y -i - " ).arg( ffmpegCommand ).arg( locale.toString( getFramerate() * 2.0 ) );
        program.insert( program.indexOf( "-c:v" ), pass3 );
    }
    //Plus box blur
    else if( m_smoothFilterSetting == SMOOTH_FILTER_3PASS_USM_BB )
    {
        QString pass3 = QString( "-filter_complex \"[0:v] boxblur=1:cr=5:ar=5 [tmp]; [0:v][tmp] blend=all_mode='normal':all_opacity=0.7\" -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -i - -vf minterpolate=%2,tblend=all_mode=average,framestep=2 -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -i - -vf minterpolate=%2,tblend=all_mode=average,framestep=2 -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -y -i - " ).arg( ffmpegCommand ).arg( locale.toString( getFramerate() * 2.0 ) );
        program.insert( program.indexOf( "-c:v" ), pass3 );
    }

    if( ( m_exportQueue.first()->vidStabEnabled() && staberr == false ) || !m_exportQueue.first()->vidStabEnabled() )
    {
        //Try to open pipe
        FILE *pPipe;
        //qDebug() << "Call ffmpeg:" << program;
#ifdef Q_OS_UNIX
        if( !( pPipe = popen( program.toUtf8().data(), "w" ) ) )
#else
    if( !( pPipe = popen( program.toLatin1().data(), "wb" ) ) )
#endif
        {
            QMessageBox::critical( this, tr( "File export failed" ), tr( "Could not export with ffmpeg." ) );
        }
        else
        {
            //Buffer
            uint32_t frameSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
            uint16_t * imgBuffer;
            imgBuffer = ( uint16_t* )malloc( frameSize * sizeof( uint16_t ) );

            //Frames in the export queue?!
            int totalFrames = 0;
            for( int i = 0; i < m_exportQueue.size(); i++ )
            {
                totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
            }

            //Build buffer
            uint16_t * imgBufferScaled;
            imgBufferScaled = ( uint16_t* )malloc( width * height * 3 * sizeof( uint16_t ) );

            //Get all pictures and send to pipe
            for( uint32_t i = (m_exportQueue.first()->cutIn() - 1); i < m_exportQueue.first()->cutOut(); i++ )
            {
                if( m_codecProfile == CODEC_TIFF && m_codecOption == CODEC_TIFF_AVG && i > 128 ) break;

                if( scaled )
                {
                    //Get picture, and lock render thread... there can only be one!
                    m_pRenderThread->lock();
                    getMlvProcessedFrame16( m_pMlvObject, i, imgBuffer, QThread::idealThreadCount() );
                    m_pRenderThread->unlock();

                    avir_scale_thread_pool scaling_pool;
                    avir::CImageResizerVars vars; vars.ThreadPool = &scaling_pool;
                    avir::CImageResizerParamsUltra roptions;
                    avir::CImageResizer<> image_resizer( 16, 0, roptions );
                    image_resizer.resizeImage( imgBuffer,
                                               getMlvWidth(m_pMlvObject),
                                               getMlvHeight(m_pMlvObject), 0,
                                               imgBufferScaled,
                                               width,
                                               height,
                                               3, 0, &vars );

                    //Write to pipe
                    fwrite(imgBufferScaled, sizeof( uint16_t ), width * height * 3, pPipe);
                    fflush(pPipe);
                }
                else
                {
                    //Get picture, and lock render thread... there can only be one!
                    m_pRenderThread->lock();
                    getMlvProcessedFrame16( m_pMlvObject, i, imgBuffer, QThread::idealThreadCount() );
                    m_pRenderThread->unlock();

                    //Write to pipe
                    fwrite(imgBuffer, sizeof( uint16_t ), frameSize, pPipe);
                    fflush(pPipe);
                }

                //Set Status
                if( !( m_exportQueue.first()->vidStabEnabled() && m_codecProfile == CODEC_H264 ) )
                {
                    m_pStatusDialog->ui->progressBar->setValue( i - ( m_exportQueue.first()->cutIn() - 1 ) + 1 );
                    m_pStatusDialog->ui->progressBar->repaint();
                    m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - i + ( m_exportQueue.first()->cutIn() - 1 ) - 1 );
                }
                else
                {
                    m_pStatusDialog->ui->progressBar->setValue( ( totalFrames + i - ( m_exportQueue.first()->cutIn() - 1 ) + 1 ) >> 1 );
                    m_pStatusDialog->ui->progressBar->repaint();
                    m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - ( ( totalFrames + i - ( m_exportQueue.first()->cutIn() - 1 ) + 1 ) >> 1 ) );
                }
                qApp->processEvents();

                //Check diskspace
                checkDiskFull( fileName );
                //Abort pressed? -> End the loop
                if( m_exportAbortPressed ) break;
            }
            //Close pipe
            if( pclose( pPipe ) != 0 )
            {
                QMessageBox::critical( this, tr( "File export failed" ), tr( "FFmpeg closed unexpectedly during export.\n\nFile %1 was not exported completely." ).arg( fileName ) );
            }
            free( imgBufferScaled );
            free( imgBuffer );
        }
    }

    //Delete wav file
    QFile *file = new QFile( wavFileName );
    if( file->exists() ) file->remove();
    delete file;

    //Delete tmp vidstab file
    vidstabFile.replace( "\"", "" );
    file = new QFile( vidstabFile );
    if( file->exists() ) file->remove();
    delete file;

    //Delete file if aborted
    /*if( m_exportAbortPressed )
    {
        file = new QFile( fileName );
        if( file->exists() ) file->remove();
        delete file;
    }*/

    //If we don't like amaze we switch it off again
    if( !ui->actionAlwaysUseAMaZE->isChecked() ) { setMlvDontAlwaysUseAmaze( m_pMlvObject ); }

    //Enable GUI drawing
    m_dontDraw = false;

    //Emit Ready-Signal
    emit exportReady();
}

//CDNG output
void MainWindow::startExportCdng(QString fileName)
{
    //Disable GUI drawing
    m_dontDraw = true;

    // we always get amaze frames for exporting
    setMlvAlwaysUseAmaze( m_pMlvObject );
    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    llrpComputeStripesOn(m_pMlvObject);
    m_pMlvObject->current_cached_frame_active = 0;
    //enable low level raw fixes (if wanted)
    if( ui->checkBoxRawFixEnable->isChecked() ) m_pMlvObject->llrawproc->fix_raw = 1;

    //StatusDialog
    m_pStatusDialog->ui->progressBar->setMaximum( m_exportQueue.first()->cutOut() - m_exportQueue.first()->cutIn() + 1 );
    m_pStatusDialog->ui->progressBar->setValue( 0 );
    m_pStatusDialog->open();
    //Frames in the export queue?!
    int totalFrames = 0;
    for( int i = 0; i < m_exportQueue.size(); i++ )
    {
        totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
    }

    //Create folders and build name schemes
    QString pathName = QFileInfo( fileName ).path();
    fileName = QFileInfo( fileName ).fileName();
    fileName = fileName.left( fileName.indexOf( '.' ) );

    if( m_codecOption == CODEC_CNDG_DEFAULT ) pathName = pathName.append( "/%1" ).arg( fileName );
    else pathName = pathName.append( "/%1_1_%2-%3-%4_0001_C0000" )
            .arg( fileName )
            .arg( getMlvTmYear( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmMonth( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmDay( m_pMlvObject ), 2, 10, QChar('0') );

    //qDebug() << pathName << fileName;
    //Create folder
    QDir dir;
    dir.mkpath( pathName );

    //Output WAVE
    if( doesMlvHaveAudio( m_pMlvObject ) && m_audioExportEnabled )
    {
        QString wavFileName = pathName;
        if( m_codecOption == CODEC_CNDG_DEFAULT ) wavFileName = wavFileName.append( "/%1.wav" ).arg( fileName );
        else wavFileName = wavFileName.append( "/%1_1_%2-%3-%4_0001_C0000.wav" )
            .arg( fileName )
            .arg( getMlvTmYear( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmMonth( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmDay( m_pMlvObject ), 2, 10, QChar('0') );
        //qDebug() << wavFileName;
#ifdef Q_OS_UNIX
        writeMlvAudioToWaveCut( m_pMlvObject, wavFileName.toUtf8().data(), m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut() );
#else
        writeMlvAudioToWaveCut( m_pMlvObject, wavFileName.toLatin1().data(), m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut() );
#endif
    }

    //Set aspect ratio of the picture
    int32_t picAR[4] = { 0 };
    //Set horizontal stretch
    if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_125 )
    {
        picAR[0] = 5; picAR[1] = 4;
    }
    else if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_133 )
    {
        picAR[0] = 4; picAR[1] = 3;
    }
    else if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_150 )
    {
        picAR[0] = 3; picAR[1] = 2;
    }
    else if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_167 )
    {
        picAR[0] = 5; picAR[1] = 3;
    }
    else if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_175 )
    {
        picAR[0] = 7; picAR[1] = 4;
    }
    else if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_180 )
    {
        picAR[0] = 9; picAR[1] = 5;
    }
    else if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_200 )
    {
        picAR[0] = 2; picAR[1] = 1;
    }
    else
    {
        picAR[0] = 1; picAR[1] = 1;
    }
    //Set vertical stretch
    if(m_exportQueue.first()->stretchFactorY() == STRETCH_V_167)
    {
        picAR[2] = 5; picAR[3] = 3;
    }
    else if(m_exportQueue.first()->stretchFactorY() == STRETCH_V_300)
    {
        picAR[2] = 3; picAR[3] = 1;
    }
    else if(m_exportQueue.first()->stretchFactorY() == STRETCH_V_033)
    {
        picAR[2] = 1; picAR[3] = 1; picAR[0] *= 3; //Upscale only
    }
    else
    {
        picAR[2] = 1; picAR[3] = 1;
    }

    //Init DNG data struct
    dngObject_t * cinemaDng = initDngObject( m_pMlvObject, m_codecProfile - 6, getFramerate(), picAR);

    //Render one single frame for raw correction init
    uint32_t frameSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
    uint16_t * imgBuffer;
    imgBuffer = ( uint16_t* )malloc( frameSize * sizeof( uint16_t ) );
    getMlvProcessedFrame16( m_pMlvObject, 0, imgBuffer, QThread::idealThreadCount() );
    free( imgBuffer );

    //Output frames loop
    for( uint32_t frame = m_exportQueue.first()->cutIn() - 1; frame < m_exportQueue.first()->cutOut(); frame++ )
    {
        QString dngName;
        if( m_codecOption == CODEC_CNDG_DEFAULT ) dngName = dngName.append( "%1_%2.dng" )
                                                                                .arg( fileName )
                                                                                .arg( getMlvFrameNumber( m_pMlvObject, frame ), 6, 10, QChar('0') );
        else dngName = dngName.append( "%1_1_%2-%3-%4_0001_C0000_%5.dng" )
            .arg( fileName )
            .arg( getMlvTmYear( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmMonth( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmDay( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvFrameNumber( m_pMlvObject, frame ), 6, 10, QChar('0') );

        QString filePathNr = pathName;
        filePathNr = filePathNr.append( "/" + dngName );

        //Save cDNG frame
#ifdef Q_OS_UNIX
        QString properties_fn = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        properties_fn.append("/mlv-dng-params.txt");
        if( saveDngFrame( m_pMlvObject, cinemaDng, frame, filePathNr.toUtf8().data(), properties_fn.toUtf8().data() ) )
#else
        QString properties_fn = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        properties_fn.append("\\mlv-dng-params.txt");
        if( saveDngFrame( m_pMlvObject, cinemaDng, frame, filePathNr.toLatin1().data(), properties_fn.toLatin1().data() ) )
#endif
        {
            m_pStatusDialog->close();
            qApp->processEvents();
            int ret = QMessageBox::critical( this,
                                             tr( "MLV App - Export file error" ),
                                             tr( "Could not save: %1\nHow do you like to proceed?" ).arg( dngName ),
                                             tr( "Skip frame" ),
                                             tr( "Abort current export" ),
                                             tr( "Abort batch export" ),
                                             0, 2 );
            if( ret == 2 )
            {
                exportAbort();
            }
            if( ret > 0 )
            {
                break;
            }
        }

        //Set Status
        m_pStatusDialog->ui->progressBar->setValue( frame - ( m_exportQueue.first()->cutIn() - 1 ) + 1 );
        m_pStatusDialog->ui->progressBar->repaint();
        m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - frame + ( m_exportQueue.first()->cutIn() - 1 ) - 1 );
        qApp->processEvents();

        //Check diskspace
        checkDiskFull( filePathNr );
        //Abort pressed? -> End the loop
        if( m_exportAbortPressed ) break;
    }

    //Free DNG data struct
    freeDngObject( cinemaDng );

    //Enable GUI drawing
    m_dontDraw = false;

    //Emit Ready-Signal
    emit exportReady();
}

//MLV export
void MainWindow::startExportMlv(QString fileName)
{
    //Disable GUI drawing
    m_dontDraw = true;

    //StatusDialog
    m_pStatusDialog->ui->progressBar->setMaximum( m_exportQueue.first()->cutOut() - m_exportQueue.first()->cutIn() + 1 );
    m_pStatusDialog->ui->progressBar->setValue( 0 );
    m_pStatusDialog->open();
    //Frames in the export queue?!
    uint32_t totalFrames = 0;
    for( int i = 0; i < m_exportQueue.size(); i++ )
    {
        totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
    }

    //Create folders and build name schemes
    QString pathName = QFileInfo( fileName ).path();
    fileName = QFileInfo( fileName ).fileName();
    //fileName = fileName.left( fileName.indexOf( '.' ) );
    pathName = pathName.append( "/%1" ).arg( fileName );

    /* open .MLV file for writing */
#ifdef Q_OS_UNIX
    FILE* mlvOut = fopen(pathName.toUtf8().data(), "wb");
#else
    FILE* mlvOut = fopen(pathName.toLatin1().data(), "wb");
#endif
    if (!mlvOut)
    {
        return;
    }

    //Allocate buffer for averaging
    uint64_t * averagedImage = NULL;
    if( m_codecOption == CODEC_MLV_AVERAGED ) averagedImage = (uint64_t *)calloc( m_pMlvObject->RAWI.xRes * m_pMlvObject->RAWI.yRes * sizeof( uint64_t ), 1 );
    //Check if MLV has audio and it is requested to be exported
    int exportAudio = (doesMlvHaveAudio( m_pMlvObject ) && m_audioExportEnabled);
    //Error message string passed from backend
    char errorMessage[256] = { 0 };
    //Save MLV block headers
#ifdef Q_OS_UNIX
    int ret = saveMlvHeaders( m_pMlvObject, mlvOut, exportAudio, m_codecOption, m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut(), VERSION.toUtf8().data(), errorMessage );
#else
    int ret = saveMlvHeaders( m_pMlvObject, mlvOut, exportAudio, m_codecOption, m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut(), VERSION.toLatin1().data(), errorMessage );
#endif
    //Output frames loop
    for( uint32_t frame = m_exportQueue.first()->cutIn() - 1; frame < m_exportQueue.first()->cutOut(); frame++ )
    {
        //Save audio and video frames
        if( ret || saveMlvAVFrame( m_pMlvObject, mlvOut, exportAudio, m_codecOption, m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut(), frame , averagedImage, errorMessage) )
        {
            fclose(mlvOut); mlvOut = NULL;
            QFile( pathName ).remove();

            ret = QMessageBox::critical( this,
                                         tr( "MLV App - Export file error" ),
                                         tr( "%1" ).arg( errorMessage ),
                                         tr( "Abort current export" ),
                                         tr( "Abort batch export" ),
                                         0, 1 );
            if( ret ) exportAbort();
            else break;
        }
        else
        {
            //Set Status
            m_pStatusDialog->ui->progressBar->setValue( frame - ( m_exportQueue.first()->cutIn() - 1 ) + 1 );
            m_pStatusDialog->ui->progressBar->repaint();
            m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - frame + ( m_exportQueue.first()->cutIn() - 1 ) - 1 );
            qApp->processEvents();
        }
        //Abort pressed? -> End the loop
        if( m_exportAbortPressed || m_codecOption == CODEC_MLV_EXTRACT_DF) break;
    }
    //Clean up
    if( averagedImage ) free( averagedImage );
    if( mlvOut ) fclose(mlvOut);
    //Enable GUI drawing
    m_dontDraw = false;
    //Emit Ready-Signal
    emit exportReady();
}

//Export via AVFoundation
#ifdef Q_OS_MACX
void MainWindow::startExportAVFoundation(QString fileName)
{
    //Disable GUI drawing
    m_dontDraw = true;

    //chose if we want to get amaze frames for exporting, or bilinear
    if( m_exportDebayerMode == 0 )
    {
        setMlvDontAlwaysUseAmaze( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 1 )
    {
        setMlvAlwaysUseAmaze( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 2 )
    {
        setMlvUseLmmseDebayer( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 3 )
    {
        setMlvUseIgvDebayer( m_pMlvObject );
    }
    else
    {
        switch( m_exportQueue.first()->debayer() )
        {
        case ReceiptSettings::None:
            setMlvUseNoneDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::Simple:
            setMlvUseSimpleDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::Bilinear:
            setMlvDontAlwaysUseAmaze( m_pMlvObject );
            break;
        case ReceiptSettings::LMMSE:
            setMlvUseLmmseDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::IGV:
            setMlvUseIgvDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::AMaZE:
            setMlvAlwaysUseAmaze( m_pMlvObject );
            break;
        case ReceiptSettings::AHD:
            setMlvUseAhdDebayer( m_pMlvObject );
            break;
        default:
            break;
        }
    }
    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    llrpComputeStripesOn(m_pMlvObject);
    m_pMlvObject->current_cached_frame_active = 0;
    //enable low level raw fixes (if wanted)
    if( ui->checkBoxRawFixEnable->isChecked() ) m_pMlvObject->llrawproc->fix_raw = 1;

    //StatusDialog
    m_pStatusDialog->ui->progressBar->setMaximum( m_exportQueue.first()->cutOut() - m_exportQueue.first()->cutIn() + 1 );
    m_pStatusDialog->ui->progressBar->setValue( 0 );
    m_pStatusDialog->open();
    //Frames in the export queue?!
    int totalFrames = 0;
    for( int i = 0; i < m_exportQueue.size(); i++ )
    {
        totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
    }

    //If file exists, delete it!
    QFile *file = new QFile( fileName );
    if( file->exists() ) file->remove();
    delete file;

    //Codec?
    int avfCodec;
    if( m_codecProfile == CODEC_PRORES422ST ) avfCodec = AVF_CODEC_PRORES_422;
    else if( m_codecProfile == CODEC_H264 ) avfCodec = AVF_CODEC_H264;
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101300
    else if( m_codecProfile == CODEC_H265_8 ) avfCodec = AVF_CODEC_HEVC;
    else if( m_codecProfile == CODEC_PRORES422PROXY ) avfCodec = AVF_CODEC_PRORES_422_PROXY;
    else if( m_codecProfile == CODEC_PRORES422LT ) avfCodec = AVF_CODEC_PRORES_422_LT;
    else if( m_codecProfile == CODEC_PRORES422HQ ) avfCodec = AVF_CODEC_PRORES_422_HQ;
#endif
    else avfCodec = AVF_CODEC_PRORES_4444;

    //Dimension & scaling
    uint16_t width = getMlvWidth(m_pMlvObject);
    uint16_t height = getMlvHeight(m_pMlvObject);
    bool scaled = false;
    if( m_resizeFilterEnabled )
    {
        //Autocalc height
        if( m_resizeFilterHeightLocked )
        {
            height = (double)m_resizeWidth / (double)getMlvWidth( m_pMlvObject )
                    / m_exportQueue.first()->stretchFactorX()
                    * m_exportQueue.first()->stretchFactorY()
                    * (double)getMlvHeight( m_pMlvObject ) + 0.5;
        }
        else
        {
            height = m_resizeHeight;
        }
        width = m_resizeWidth;
        scaled = true;
    }
    else if( m_exportQueue.first()->stretchFactorX() != 1.0
          || m_exportQueue.first()->stretchFactorY() != 1.0 )
    {
        //Upscale only
        if( m_exportQueue.first()->stretchFactorY() == STRETCH_V_033 )
        {
            width = getMlvWidth( m_pMlvObject ) * 3;
            height = getMlvHeight( m_pMlvObject );
        }
        else
        {
            width = getMlvWidth( m_pMlvObject ) * m_exportQueue.first()->stretchFactorX();
            height = getMlvHeight( m_pMlvObject ) * m_exportQueue.first()->stretchFactorY();
        }
        scaled = true;
    }
    if( m_codecProfile == CODEC_H264
     || m_codecProfile == CODEC_H265_8
     || m_codecProfile == CODEC_H265_10
     || m_codecProfile == CODEC_H265_12 )
    {
        if( width != width + (width % 2) )
        {
            width += width % 2;
            scaled = true;
        }
        if( height != height + (height % 2) )
        {
            height += height % 2;
            scaled = true;
        }
    }

    //Init Encoder
    AVEncoder_t * encoder = initAVEncoder( width,
                                           height,
                                           avfCodec,
                                           AVF_COLOURSPACE_SRGB,
                                           getFramerate() );

    beginWritingVideoFile(encoder, fileName.toUtf8().data());

    //Build buffer
    uint32_t frameSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
    uint16_t * imgBuffer;
    imgBuffer = ( uint16_t* )malloc( frameSize * sizeof( uint16_t ) );
    uint16_t * imgBufferScaled;
    uint8_t * imgBufferScaled8;
    if( m_codecProfile == CODEC_H264 || m_codecProfile == CODEC_H265_8 ) imgBufferScaled8 = ( uint8_t* )malloc( width * height * 3 * sizeof( uint8_t ) );
    else imgBufferScaled = ( uint16_t* )malloc( width * height * 3 * sizeof( uint16_t ) );

    //Encoder frames
    for( uint64_t frame = ( m_exportQueue.first()->cutIn() - 1 ); frame < m_exportQueue.first()->cutOut(); frame++ )
    {
        //Get&Encode
        if( m_codecProfile == CODEC_H264 || m_codecProfile == CODEC_H265_8 )
        {
            getMlvProcessedFrame8( m_pMlvObject, frame, m_pRawImage, QThread::idealThreadCount() );
            if( scaled )
            {
                avir_scale_thread_pool scaling_pool;
                avir::CImageResizerVars vars; vars.ThreadPool = &scaling_pool;
                avir::CImageResizerParamsUltra roptions;
                avir::CImageResizer<> image_resizer( 8, 0, roptions );
                image_resizer.resizeImage( m_pRawImage,
                                           getMlvWidth(m_pMlvObject),
                                           getMlvHeight(m_pMlvObject), 0,
                                           imgBufferScaled8,
                                           width,
                                           height,
                                           3, 0, &vars );
                addFrameToVideoFile8bit( encoder, imgBufferScaled8 );
            }
            else
            {
                addFrameToVideoFile8bit( encoder, m_pRawImage );
            }
        }
        else
        {
            getMlvProcessedFrame16( m_pMlvObject, frame, imgBuffer, QThread::idealThreadCount() );
            if( scaled )
            {
                avir_scale_thread_pool scaling_pool;
                avir::CImageResizerVars vars; vars.ThreadPool = &scaling_pool;
                avir::CImageResizerParamsUltra roptions;
                avir::CImageResizer<> image_resizer( 16, 0, roptions );
                image_resizer.resizeImage( imgBuffer,
                                           getMlvWidth(m_pMlvObject),
                                           getMlvHeight(m_pMlvObject), 0,
                                           imgBufferScaled,
                                           width,
                                           height,
                                           3, 0, &vars );
                addFrameToVideoFile( encoder, imgBufferScaled );
            }
            else
            {
                addFrameToVideoFile( encoder, imgBuffer );
            }
        }

        //Set Status
        m_pStatusDialog->ui->progressBar->setValue( frame - ( m_exportQueue.first()->cutIn() - 1 ) + 1 );
        m_pStatusDialog->ui->progressBar->repaint();
        m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - frame + ( m_exportQueue.first()->cutIn() - 1 ) - 1 );
        qApp->processEvents();

        //Check diskspace
        checkDiskFull( fileName );
        //Abort pressed? -> End the loop
        if( m_exportAbortPressed ) break;
    }

    //Clean up
    if( m_codecProfile == CODEC_H264 || m_codecProfile == CODEC_H265_8 ) free( imgBufferScaled8 );
    else free( imgBufferScaled );
    free( imgBuffer );
    endWritingVideoFile(encoder);
    freeAVEncoder(encoder);

    //Audio
    if( m_audioExportEnabled && doesMlvHaveAudio( m_pMlvObject ) && !m_exportAbortPressed )
    {
        QString wavFileName = QString( "%1.wav" ).arg( fileName.left( fileName.lastIndexOf( "." ) ) );
        writeMlvAudioToWaveCut( m_pMlvObject, wavFileName.toUtf8().data(), m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut() );

        QString tempFileName = QString( "%1_temp.mov" ).arg( fileName.left( fileName.lastIndexOf( "." ) ) );
        QFile( fileName ).rename( tempFileName );

        //FFMpeg export
        QString ffmpegAudioCommand = QCoreApplication::applicationDirPath() + QString( "/ffmpeg" );
        QStringList ffmpegAudioCommandArguments;

#ifdef STDOUT_SILENT
        ffmpegAudioCommandArguments << QString( "-loglevel" ) << QString( "quiet" );
#endif

        ffmpegAudioCommandArguments << QString( "-y" )
                                    << QString( "-i" )
                                    << QString( "%1" ).arg( tempFileName )
                                    << QString( "-i" )
                                    << QString( "%1" ).arg( wavFileName )
                                    << QString( "-map" )
                                    << QString( "0:0" )
                                    << QString( "-map" )
                                    << QString( "1:0" )
                                    << QString( "-c" )
                                    << QString( "copy" )
                                    << QString( "%1" ).arg( fileName );

        QProcess *ffmpegProc = new QProcess( this );
        int i = 0;
        //Try 3x with delay. AVFoundation lib maybe isn't ready yet.
        while( ffmpegProc->execute( ffmpegAudioCommand, ffmpegAudioCommandArguments ) != 0 && i < 3 )
        {
            i++;
            QThread::msleep( 500 );
            //Abort pressed? -> End the loop
            if( m_exportAbortPressed ) break;
        }
        delete ffmpegProc;
        if( i < 3 && !m_exportAbortPressed )
        {
            QFile( tempFileName ).open( QIODevice::WriteOnly ); //AVFoundation seems to block the file - so we make it a 0Byte file -> free disk memory
            QFile( tempFileName ).close();
            QFile( tempFileName ).remove();
            QFile( wavFileName ).remove();
        }
        else QMessageBox::critical( this, APPNAME, tr( "Merging audio to AVFoundation video for %1 failed." ).arg( fileName ) );
    }

    //If we don't like amaze we switch it off again
    if( !ui->actionAlwaysUseAMaZE->isChecked() ) { setMlvDontAlwaysUseAmaze( m_pMlvObject ); }

    //Enable GUI drawing
    m_dontDraw = false;

    //Emit Ready-Signal
    emit exportReady();
}
#endif


//Add the clip in SessionList position "row" at last position in ExportQueue
void MainWindow::addClipToExportQueue(int row, QString fileName)
{
    //A file must be opened once before being able to be exported
    if( GET_RECEIPT( row )->wasNeverLoaded() )
    {
        m_pStatusDialog->ui->label->setText( "Preparing export..." );
        m_pStatusDialog->ui->labelEstimatedTime->setText( "" );
        m_pStatusDialog->ui->progressBar->setValue( 0 );
        m_pStatusDialog->open();
        if( showFileInEditor( row ) ) return; //Don't add to export queue when corrupted file
        qApp->processEvents();
        setReceipt( GET_RECEIPT( row ) );
    }

    ReceiptSettings *receipt = new ReceiptSettings();
    receipt->setExposure( GET_RECEIPT( row )->exposure() );
    receipt->setContrast( GET_RECEIPT( row )->contrast() );
    receipt->setPivot( GET_RECEIPT( row )->pivot() );
    receipt->setTemperature( GET_RECEIPT( row )->temperature() );
    receipt->setTint( GET_RECEIPT( row )->tint() );
    receipt->setClarity( GET_RECEIPT( row )->clarity() );
    receipt->setVibrance( GET_RECEIPT( row )->vibrance() );
    receipt->setSaturation( GET_RECEIPT( row )->saturation() );
    receipt->setDr( GET_RECEIPT( row )->dr() );
    receipt->setDs( GET_RECEIPT( row )->ds() );
    receipt->setLr( GET_RECEIPT( row )->lr() );
    receipt->setLs( GET_RECEIPT( row )->ls() );
    receipt->setLightening( GET_RECEIPT( row )->lightening() );
    receipt->setShadows( GET_RECEIPT( row )->shadows() );
    receipt->setHighlights( GET_RECEIPT( row )->highlights() );
    receipt->setGradationCurve( GET_RECEIPT( row )->gradationCurve() );
    receipt->setHueVsHue( GET_RECEIPT( row )->hueVsHue() );
    receipt->setHueVsSaturation( GET_RECEIPT( row )->hueVsSaturation() );
    receipt->setHueVsLuminance( GET_RECEIPT( row )->hueVsLuminance() );
    receipt->setLumaVsSaturation( GET_RECEIPT( row )->lumaVsSaturation() );

    receipt->setGradientEnabled( GET_RECEIPT( row )->isGradientEnabled() );
    receipt->setGradientExposure( GET_RECEIPT( row )->gradientExposure() );
    receipt->setGradientContrast( GET_RECEIPT( row )->gradientContrast() );
    receipt->setGradientStartX( GET_RECEIPT( row )->gradientStartX() );
    receipt->setGradientStartY( GET_RECEIPT( row )->gradientStartY() );
    receipt->setGradientLength( GET_RECEIPT( row )->gradientLength() );
    receipt->setGradientAngle( GET_RECEIPT( row )->gradientAngle() );

    receipt->setSharpen( GET_RECEIPT( row )->sharpen() );
    receipt->setShMasking( GET_RECEIPT( row )->shMasking() );
    receipt->setChromaBlur( GET_RECEIPT( row )->chromaBlur() );
    receipt->setHighlightReconstruction( GET_RECEIPT( row )->isHighlightReconstruction() );
    receipt->setCamMatrixUsed( GET_RECEIPT( row )->camMatrixUsed() );
    receipt->setChromaSeparation( GET_RECEIPT( row )->isChromaSeparation() );
    receipt->setProfile( GET_RECEIPT( row )->profile() );
    receipt->setAllowCreativeAdjustments( GET_RECEIPT( row )->allowCreativeAdjustments() );
    receipt->setExrMode( GET_RECEIPT( row )->exrMode() );
    receipt->setAgx( GET_RECEIPT( row )->agx() );
    receipt->setTonemap( GET_RECEIPT( row )->tonemap() );
    receipt->setTransferFunction( GET_RECEIPT( row )->transferFunction() );
    receipt->setGamut( GET_RECEIPT( row )->gamut() );
    receipt->setGamma( GET_RECEIPT( row )->gamma() );
    receipt->setDenoiserStrength( GET_RECEIPT( row )->denoiserStrength() );
    receipt->setDenoiserWindow( GET_RECEIPT( row )->denoiserWindow() );
    receipt->setRbfDenoiserLuma( GET_RECEIPT( row )->rbfDenoiserLuma() );
    receipt->setRbfDenoiserChroma( GET_RECEIPT( row )->rbfDenoiserChroma() );
    receipt->setRbfDenoiserRange( GET_RECEIPT( row )->rbfDenoiserRange() );
    receipt->setGrainStrength( GET_RECEIPT( row )->grainStrength() );
    receipt->setGrainLumaWeight( GET_RECEIPT( row )->grainLumaWeight() );

    receipt->setRawFixesEnabled( GET_RECEIPT( row )->rawFixesEnabled() );
    receipt->setVerticalStripes( GET_RECEIPT( row )->verticalStripes() );
    receipt->setFocusPixels( GET_RECEIPT( row )->focusPixels() );
    receipt->setFpiMethod( GET_RECEIPT( row )->fpiMethod() );
    receipt->setBadPixels( GET_RECEIPT( row )->badPixels() );
    receipt->setBpsMethod( GET_RECEIPT( row )->bpsMethod() );
    receipt->setBpiMethod( GET_RECEIPT( row )->bpiMethod() );
    receipt->setChromaSmooth( GET_RECEIPT( row )->chromaSmooth() );
    receipt->setPatternNoise( GET_RECEIPT( row )->patternNoise() );
    receipt->setDeflickerTarget( GET_RECEIPT( row )->deflickerTarget() );
    receipt->setDualIsoForced( GET_RECEIPT( row )->dualIsoForced() );
    receipt->setDualIso( GET_RECEIPT( row )->dualIso() );
    receipt->setDualIsoAutoCorrected( GET_RECEIPT( row )->dualIsoAutoCorrected() );
    receipt->setDualIsoPattern( GET_RECEIPT( row )->dualIsoPattern() );
    receipt->setDualIsoEvCorrection( GET_RECEIPT( row )->dualIsoEvCorrection() );
    receipt->setDualIsoBlackDelta( GET_RECEIPT( row )->dualIsoBlackDelta() );
    receipt->setDualIsoInterpolation( GET_RECEIPT( row )->dualIsoInterpolation() );
    receipt->setDualIsoAliasMap( GET_RECEIPT( row )->dualIsoAliasMap() );
    receipt->setDualIsoFrBlending( GET_RECEIPT( row )->dualIsoFrBlending() );
    receipt->setDualIsoWhite( GET_RECEIPT( row )->dualIsoWhite() );
    receipt->setDualIsoBlack( GET_RECEIPT( row )->dualIsoBlack() );
    receipt->setDarkFrameFileName( GET_RECEIPT( row )->darkFrameFileName() );
    receipt->setDarkFrameEnabled( GET_RECEIPT( row )->darkFrameEnabled() );
    receipt->setRawBlack( GET_RECEIPT( row )->rawBlack() );
    receipt->setRawWhite( GET_RECEIPT( row )->rawWhite() );

    receipt->setTone( GET_RECEIPT( row )->tone() );
    receipt->setToningStrength( GET_RECEIPT( row )->toningStrength() );

    receipt->setLutEnabled( GET_RECEIPT( row )->lutEnabled() );
    receipt->setLutName( GET_RECEIPT( row )->lutName() );
    receipt->setLutStrength( GET_RECEIPT( row )->lutStrength() );

    receipt->setFilterEnabled( GET_RECEIPT( row )->filterEnabled() );
    receipt->setFilterIndex( GET_RECEIPT( row )->filterIndex() );
    receipt->setFilterStrength( GET_RECEIPT( row )->filterStrength() );

    receipt->setVignetteStrength( GET_RECEIPT( row )->vignetteStrength() );
    receipt->setVignetteRadius( GET_RECEIPT( row )->vignetteRadius() );
    receipt->setVignetteShape( GET_RECEIPT( row )->vignetteShape() );
    receipt->setCaRed( GET_RECEIPT( row )->caRed() );
    receipt->setCaBlue( GET_RECEIPT( row )->caBlue() );
    receipt->setCaDesaturate( GET_RECEIPT( row )->caDesaturate() );
    receipt->setCaRadius( GET_RECEIPT( row )->caRadius() );

    receipt->setStretchFactorX( GET_RECEIPT( row )->stretchFactorX() );
    receipt->setStretchFactorY( GET_RECEIPT( row )->stretchFactorY() );
    receipt->setUpsideDown( GET_RECEIPT( row )->upsideDown() );
    receipt->setVidstabEnabled( GET_RECEIPT( row )->vidStabEnabled() );
    receipt->setVidstabStepsize( GET_RECEIPT( row )->vidStabStepsize() );
    receipt->setVidstabShakiness( GET_RECEIPT( row )->vidStabShakiness() );
    receipt->setVidstabAccuracy( GET_RECEIPT( row )->vidStabAccuracy() );
    receipt->setVidstabZoom( GET_RECEIPT( row )->vidStabZoom() );
    receipt->setVidstabSmoothing( GET_RECEIPT( row )->vidStabSmoothing() );
    receipt->setVidstabTripod( GET_RECEIPT( row )->vidStabTripod() );

    receipt->setDebayer( GET_RECEIPT( row )->debayer() );

    receipt->setFileName( GET_RECEIPT( row )->fileName() );
    receipt->setCutIn( GET_RECEIPT( row )->cutIn() );
    receipt->setCutOut( GET_RECEIPT( row )->cutOut() );
    receipt->setExportFileName( fileName );
    m_exportQueue.append( receipt );
}

//Export clip
void MainWindow::on_actionExport_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    //Save slider receipt
    setReceipt( ACTIVE_RECEIPT );

    //Save last active clip before export
    m_lastClipBeforeExport = SESSION_ACTIVE_CLIP_ROW;

    //Filename proposal in dependency to actual file
    QString saveFileName = ACTIVE_RECEIPT->fileName();
    //But take the folder from last export
    saveFileName = QString( "%1/%2" ).arg( m_lastExportPath ).arg( QFileInfo( saveFileName ).fileName() );

    QString fileType;
    QString fileEnding;
    saveFileName = saveFileName.left( saveFileName.lastIndexOf( "." ) );
    if( m_codecProfile == CODEC_AVI
     || m_codecProfile == CODEC_MJPEG
     || m_codecProfile == CODEC_FFVHUFF )
    {
        saveFileName.append( ".avi" );
        fileType = tr("Audio Video Interleave (*.avi)");
        fileEnding = ".avi";
    }
    else if( m_codecProfile == CODEC_CDNG
          || m_codecProfile == CODEC_CDNG_LOSSLESS
          || m_codecProfile == CODEC_CDNG_FAST )
    {
        saveFileName.append( ".dng" );
        fileType = tr("Cinema DNG (*.dng)");
        fileEnding = ".dng";
    }
    else if( m_codecProfile == CODEC_MLV )
    {
        saveFileName.append( ".MLV" );
        fileType = tr("Magic Lantern Video (*.MLV)");
        fileEnding = ".MLV";
    }
    else if( m_codecProfile == CODEC_TIFF )
    {
        saveFileName.append( ".tif" );
        fileType = tr("TIFF (*.tif)");
        fileEnding = ".tif";
    }
    else if( m_codecProfile == CODEC_JPG2K )
    {
        saveFileName.append( ".jp2" );
        fileType = tr("JPEG2000 (*.jp2)");
        fileEnding = ".jp2";
    }
    else if( m_codecProfile == CODEC_CINEFORM_10 || m_codecProfile == CODEC_CINEFORM_12 )
    {
        saveFileName.append( ".mov" );
        fileType = tr("Movie (*.mov)");
        fileEnding = ".mov";
    }
    else if( m_codecProfile == CODEC_AUDIO_ONLY )
    {
        saveFileName.append( ".wav" );
        fileType = tr("Audio Wave (*.wav)");
        fileEnding = ".wav";
    }
    else
    {
        if( ( m_codecProfile == CODEC_H264 || m_codecProfile == CODEC_H265_8 || m_codecProfile == CODEC_H265_10 || m_codecProfile == CODEC_H265_12 )
         && ( m_codecOption == CODEC_H264_H_MP4 || m_codecOption == CODEC_H265_H_MP4
           || m_codecOption == CODEC_H264_M_MP4 || m_codecOption == CODEC_H265_M_MP4 ) )
        {
            saveFileName.append( ".mp4" );
            fileType = tr("MPEG-4 (*.mp4)");
            fileEnding = ".mp4";
        }
        else if( ( m_codecProfile == CODEC_H264 || m_codecProfile == CODEC_H265_8 || m_codecProfile == CODEC_H265_10 || m_codecProfile == CODEC_H265_12 )
         && ( m_codecOption == CODEC_H264_H_MKV || m_codecOption == CODEC_H265_H_MKV
           || m_codecOption == CODEC_H264_M_MKV || m_codecOption == CODEC_H265_M_MKV) )
        {
            saveFileName.append( ".mkv" );
            fileType = tr("Matroska (*.mkv)");
            fileEnding = ".mkv";
        }
        else
        {
            saveFileName.append( ".mov" );
            fileType = tr("Movie (*.mov)");
            fileEnding = ".mov";
        }
    }

    //If one clip is selected, but is not a sequence
    if( ( selectedClipsList().size() <= 1 )
     && !isExportSequence() )
    {
        //File Dialog
        QString fileName = QFileDialog::getSaveFileName( this, tr("Export..."),
                                                        saveFileName,
                                                        fileType );

        //Exit if not an MOV/AVI file or aborted
        if( fileName == QString( "" )
                && !fileName.endsWith( fileEnding, Qt::CaseInsensitive ) ) return;

        //Save last path for next time
        m_lastExportPath = QFileInfo( fileName ).absolutePath();

        //Get receipt into queue
        addClipToExportQueue( SESSION_ACTIVE_CLIP_ROW, fileName );
    }
    //if multiple files selected or >= 1 sequence
    else
    {
        //Folder Dialog
        QString folderName = QFileDialog::getExistingDirectory(this, tr("Choose Export Folder"),
                                                          QFileInfo( saveFileName ).absolutePath(),
                                                          QFileDialog::ShowDirsOnly
                                                          | QFileDialog::DontResolveSymlinks);

        if( folderName.length() == 0 ) return;

        QStringList overwriteList;
        QModelIndexList selectedClips = selectedClipsList();

        for( int i = 0; i < selectedClips.size(); i++ )
        {
            //Do nothing for hidden clips
            if( ui->tableViewSession->isRowHidden( selectedClips.at( i ).row() ) ) continue;

            int row = m_pProxyModel->mapToSource( selectedClips.at( i ) ).row();
            //Sequences
            if( isExportSequence() )
            {
                QString fileName = GET_CLIP( row )->getName().replace( ".mlv", "", Qt::CaseInsensitive );
                fileName.prepend( "/" );
                fileName.prepend( folderName );

                if( QDir( fileName ).exists() ) overwriteList.append( fileName.append( "/..." ) );
            }
            //Clips
            else
            {
                QString fileName = GET_CLIP( row )->getName().replace( ".mlv", fileEnding, Qt::CaseInsensitive );
                fileName.prepend( "/" );
                fileName.prepend( folderName );

                if( QFileInfo( fileName ).exists() ) overwriteList.append( fileName );
            }
        }
        bool skip = false;
        if( !overwriteList.empty() )
        {
            //qDebug() << "Files will be overwritten:" << overwriteList;
            OverwriteListDialog *listDialog = new OverwriteListDialog( this );
            listDialog->ui->listWidget->addItems( overwriteList );
            int ret = listDialog->exec();
            if( 0 == ret ) //Abort
            {
                delete listDialog;
                return;
            }
            else if( 1 == ret ) //Overwrite
            {
            }
            else //Skip
            {
                skip = true;
            }
            delete listDialog;
        }

        //Save last path for next time
        m_lastExportPath = folderName;

        //for all selected
        for( int i = 0; i < selectedClips.size(); i++ )
        {
            bool skipFile = false;

            //Do nothing for hidden clips
            if( ui->tableViewSession->isRowHidden( selectedClips.at( i ).row() ) ) continue;

            int row = m_pProxyModel->mapToSource( selectedClips.at( i ) ).row();

            //Create Path+Name
            QString fileName = GET_CLIP( row )->getName().replace( ".mlv", fileEnding, Qt::CaseInsensitive );
            fileName.prepend( "/" );
            fileName.prepend( folderName );

            //Skip if wanted
            foreach( QString fileOverwriteName, overwriteList )
            {
                //qDebug() << skip << fileOverwriteName << fileName;
                if( skip == true && fileOverwriteName == fileName )
                {
                    skipFile = true;
                    continue;
                }
            }

            //Get receipt into queue
            if( !skipFile ) addClipToExportQueue( row, fileName );
        }
        if( m_exportQueue.isEmpty() )
        {
            QMessageBox::information( this, APPNAME, tr( "Skipped all files." ) );
            return;
        }
    }
    //Block GUI
    setEnabled( false );
    m_pStatusDialog->setEnabled( true );

    //Scripting class wants to know the export folder
    m_pScripting->setExportDir( QFileInfo( m_exportQueue.first()->exportFileName() ).absolutePath() );
    QStringList fileNames;
    for( int i = 0; i < m_exportQueue.size(); i++ )
    {
        fileNames.append( m_exportQueue.at( i )->fileName() );
    }
    m_pScripting->setMlvFileNames( fileNames );

    //startExport
    exportHandler();
}

//Export actual frame as 16bit png
void MainWindow::on_actionExportCurrentFrame_triggered()
{
    SingleFrameExportDialog *exportDialog = new SingleFrameExportDialog( this,
                                                                         m_pMlvObject,
                                                                         ACTIVE_RECEIPT->fileName(),
                                                                         ui->horizontalSliderPosition->value(),
                                                                         getHorizontalStretchFactor(true),
                                                                         getVerticalStretchFactor(true) );
    exportDialog->exec();
    delete exportDialog;
}

//Is the current export setting set to sequnce?
bool MainWindow::isExportSequence()
{
    if( ( m_codecProfile == CODEC_CDNG )
     || ( m_codecProfile == CODEC_CDNG_LOSSLESS )
     || ( m_codecProfile == CODEC_CDNG_FAST )
     || ( m_codecProfile == CODEC_PNG )
     || ( m_codecProfile == CODEC_JPG2K && m_codecOption == CODEC_JPG2K_SEQ )
     || ( m_codecProfile == CODEC_TIFF && m_codecOption == CODEC_TIFF_SEQ ) ) return true;
    else return false;
}

//Select the codec
void MainWindow::on_actionExportSettings_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    ExportSettingsDialog *pExportSettings = new ExportSettingsDialog( this,
                                                                      m_pScripting,
                                                                      m_codecProfile,
                                                                      m_codecOption,
                                                                      m_exportDebayerMode,
                                                                      m_resizeFilterEnabled,
                                                                      m_resizeWidth,
                                                                      m_resizeHeight,
                                                                      m_fpsOverride,
                                                                      m_frameRate,
                                                                      m_audioExportEnabled,
                                                                      m_resizeFilterHeightLocked,
                                                                      m_smoothFilterSetting,
                                                                      m_hdrExport );
    pExportSettings->exec();
    m_codecProfile = pExportSettings->encoderSetting();
    m_codecOption = pExportSettings->encoderOption();
    m_exportDebayerMode = pExportSettings->debayerMode();
    m_resizeFilterEnabled = pExportSettings->isResizeEnabled();
    m_resizeWidth = pExportSettings->resizeWidth();
    m_resizeHeight = pExportSettings->resizeHeight();
    m_fpsOverride = pExportSettings->isFpsOverride();
    m_frameRate = pExportSettings->getFps();
    m_audioExportEnabled = pExportSettings->isExportAudioEnabled();
    m_resizeFilterHeightLocked = pExportSettings->isHeightLocked();
    m_smoothFilterSetting = pExportSettings->smoothSetting();
    m_hdrExport = pExportSettings->hdrBlending();
    delete pExportSettings;

    if( m_fileLoaded )
    {
        //Restart timer with chosen framerate
        killTimer( m_timerId );
        m_timerId = startTimer( (int)( 1000.0 / getFramerate() ) );

        //Refresh Timecode Label
        if( m_tcModeDuration )
        {
            QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->spinBoxCutOut->value() - ui->spinBoxCutIn->value() + 1, getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                              30 * devicePixelRatio(),
                                                                                              Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
            pic.setDevicePixelRatio( devicePixelRatio() );
            m_pTcLabel->setPixmap( pic );
        }
        else
        {
            QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->horizontalSliderPosition->value(), getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                              30 * devicePixelRatio(),
                                                                                              Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
            pic.setDevicePixelRatio( devicePixelRatio() );
            m_pTcLabel->setPixmap( pic );
        }

    }

}

//Handles all export tasks, for batch export
//Must be called on export start
//Gets called when one export is ready
void MainWindow::exportHandler( void )
{
    static bool exportRunning = false;
    static int numberOfJobs = 1;
    static int jobNumber = 0;
    //Was started?
    if( exportRunning )
    {
        //Cut first job!
        if( !m_exportQueue.empty() ) //Only to avoid crashes
        {
            ReceiptSettings *receipt = m_exportQueue.takeFirst();
            delete receipt;
        }
    }
    else
    {
        //If not running save number of jobs
        numberOfJobs = m_exportQueue.size();
        m_exportAbortPressed = false;
        jobNumber = 0;
        int totalFrames = 0;
        for( int i = 0; i < numberOfJobs; i++ )
        {
            totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
        }
        m_pStatusDialog->setTotalFrames( totalFrames );
        m_pStatusDialog->startExportTime();
    }
    //Are there jobs?
    if( !m_exportQueue.empty() )
    {
        //Next job!
        exportRunning = true;
        jobNumber++;
        //Open file and settings
        if( openMlv( m_exportQueue.first()->fileName() ) )
        {
            //auto skip corrupted file
            emit exportReady();
            return;
        }
        //Set sliders to receipt
        setSliders( m_exportQueue.first(), false );
        //Fill label in StatusDialog
        m_pStatusDialog->ui->label->setText( tr( "%1/%2 - %3" )
                                             .arg( jobNumber )
                                             .arg( numberOfJobs )
                                             .arg( QFileInfo( m_exportQueue.first()->fileName() ).fileName() ) );

        //Start it, raw/rendered
        if( m_codecProfile == CODEC_CDNG
         || m_codecProfile == CODEC_CDNG_LOSSLESS
         || m_codecProfile == CODEC_CDNG_FAST )
        {
            //raw output
            startExportCdng( m_exportQueue.first()->exportFileName() );
        }
#ifdef Q_OS_MACX
        else if( ( m_codecProfile <= CODEC_PRORES4444 && m_codecOption == CODEC_PRORES_AVFOUNDATION )
              || ( m_codecProfile == CODEC_H264 && m_codecOption == CODEC_H264_AVFOUNDATION )
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101300
              || ( m_codecProfile == CODEC_H265_8 && m_codecOption == CODEC_H265_AVFOUNDATION )
#endif
               )
        {
            //AVFoundation
            startExportAVFoundation( m_exportQueue.first()->exportFileName() );
        }
#endif
        else if( m_codecProfile == CODEC_MLV )
        {
            //MLV output
            startExportMlv( m_exportQueue.first()->exportFileName() );
        }
        else
        {
            //rendered output
            startExportPipe( m_exportQueue.first()->exportFileName() ); //Pipe export
        }
        return;
    }
    //Else if all planned exports are ready
    else
    {
        //Hide Status Dialog
        m_pStatusDialog->close();
        //Open last file which was opened before export
        openMlv( GET_RECEIPT( m_lastClipBeforeExport )->fileName() );
        setSliders( GET_RECEIPT( m_lastClipBeforeExport ), false );
        SET_ACTIVE_CLIP_IDX( m_lastClipBeforeExport );
        //Unblock GUI
        setEnabled( true );
        //Export is ready
        exportRunning = false;

        if( !m_exportAbortPressed )
        {
            //Start export script when ready
            m_pScripting->executePostExportScript();
            if( ui->actionNotificationExportFinished->isChecked() )
                QMessageBox::information( this, tr( "Export" ), tr( "Export is ready." ) );
        }
        else if( ui->actionNotificationExportFinished->isChecked() )
            QMessageBox::information( this, tr( "Export" ), tr( "Export aborted." ) );

        //Caching is in which state? Set it!
        if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();
    }
}

//Abort pressed while exporting
void MainWindow::exportAbort( void )
{
    m_exportAbortPressed = true;
    m_exportQueue.clear();
}

//Check if disk nearly full
void MainWindow::checkDiskFull(QString path)
{
    QStorageInfo disk = QStorageInfo( QFileInfo( path ).path() );
    //qDebug() << QFileInfo( path ).path() << "availableSize:" << disk.bytesAvailable()/1024/1024 << "MB";
    if( 20 > disk.bytesAvailable()/1024/1024 )
    {
        QMessageBox::warning( this, APPNAME, tr( "Disk full. Export aborted." ) );
        m_exportAbortPressed = true;
    }
}
