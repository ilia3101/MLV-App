/*!
 * \file ClipInformation.h
 * \author masc4ii
 * \copyright 2020
 * \brief This class represents one clip in the session: metadata + receipt
 */

#include "ClipInformation.h"
#include <QVariant>

//Constructor
ClipInformation::ClipInformation(QString name,
                                 QString path,
                                 QString camera,
                                 QString lens,
                                 QString resolution,
                                 QString duration,
                                 QString frames,
                                 QString frameRate,
                                 QString focalLength,
                                 QString shutter,
                                 QString aperture,
                                 QString iso,
                                 QString dualIso,
                                 QString bitDepth,
                                 QString dateTime,
                                 QString audio,
                                 QColor backgroundColor) :
    m_name( name ),
    m_path( path ),
    m_camera( camera ),
    m_lens( lens ),
    m_resolution( resolution ),
    m_duration( duration ),
    m_frames( frames ),
    m_frameRate( frameRate ),
    m_focalLength( focalLength ),
    m_shutter( shutter ),
    m_aperture( aperture ),
    m_iso( iso ),
    m_dualIso( dualIso ),
    m_bitDepth( bitDepth ),
    m_dateTime( dateTime ),
    m_audio( audio ),
    m_backgroundColor( backgroundColor )
{
    QPixmap pixmap = QPixmap( 100, 50 );
    pixmap.fill( Qt::black );
    m_preview = QIcon( pixmap );
    m_pReceipt = new ReceiptSettings();
    m_pReceipt->setFileName( path );
}

//Constructor
ClipInformation::ClipInformation(QString name,
                                 QString path) :
    m_name( name ),
    m_path( path ),
    m_camera( "-" ),
    m_lens( "-" ),
    m_resolution( "-"),
    m_duration( "-" ),
    m_frames( "-" ),
    m_frameRate( "-" ),
    m_focalLength( "-" ),
    m_shutter( "-" ),
    m_aperture( "-" ),
    m_iso( "-" ),
    m_dualIso( "-" ),
    m_bitDepth( "-" ),
    m_dateTime( "-" ),
    m_audio( "-" ),
    m_backgroundColor( QColor( 0, 0, 0, 0 ) )
{
    QPixmap pixmap = QPixmap( 100, 50 );
    pixmap.fill( Qt::black );
    m_preview = QIcon( pixmap );
    m_pReceipt = new ReceiptSettings();
    m_pReceipt->setFileName( path );
}

//Destructor
ClipInformation::~ClipInformation()
{
    delete m_pReceipt;
}

//Read saved metadata
QVariant ClipInformation::getElement(int element) const
{
    switch( element )
    {
    case 0:
        return m_name;
    case 1:
        return m_path;
    case 2:
        return m_camera;
    case 3:
        return m_lens;
    case 4:
        return m_resolution;
    case 5:
        return m_duration;
    case 6:
        return m_frames;
    case 7:
        return m_frameRate;
    case 8:
        return m_focalLength;
    case 9:
        return m_shutter;
    case 10:
        return m_aperture;
    case 11:
        return m_iso;
    case 12:
        return m_dualIso;
    case 13:
        return m_bitDepth;
    case 14:
        return m_dateTime;
    case 15:
        return m_audio;
    default:
        return QVariant();
    }
    return QVariant();
}

//Save metadata to the class
void ClipInformation::setElement(int element, QVariant value)
{
    switch ( element ) {
    case 0:
        m_name = value.toString();
        break;
    case 1:
        m_path = value.toString();
    case 2:
        m_camera = value.toString();
    case 3:
        m_lens = value.toString();
    case 4:
        m_resolution = value.toString();
    case 5:
        m_duration = value.toString();
    case 6:
        m_frames = value.toString();
    case 7:
        m_frameRate = value.toString();
    case 8:
        m_focalLength = value.toString();
    case 9:
        m_shutter = value.toString();
    case 10:
        m_aperture = value.toString();
    case 11:
        m_iso = value.toString();
    case 12:
        m_dualIso = value.toString();
    case 13:
        m_bitDepth = value.toString();
    case 14:
        m_dateTime = value.toString();
    case 15:
        m_audio = value.toString();
    default:
        break;
    }
}

//Update missing metadata to the class
void ClipInformation::updateMetadata(QString camera, QString lens, QString resolution, QString duration,
                                     QString frames, QString frameRate, QString focalLength, QString shutter,
                                     QString aperture, QString iso, QString dualIso, QString bitDepth, QString dateTime,
                                     QString audio)
{
    m_camera = camera;
    m_lens = lens;
    m_resolution = resolution;
    m_duration = duration;
    m_frames = frames;
    m_frameRate = frameRate;
    m_focalLength = focalLength;
    m_shutter = shutter;
    m_aperture = aperture;
    m_iso = iso;
    m_dualIso = dualIso;
    m_bitDepth = bitDepth;
    m_dateTime = dateTime;
    m_audio = audio;
}
