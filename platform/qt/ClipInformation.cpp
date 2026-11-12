/*!
 * \file ClipInformation.h
 * \author masc4ii
 * \copyright 2020
 * \brief This class represents one clip in the session: metadata + receipt
 */

#include "ClipInformation.h"
#include <QVariant>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

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
                                 QString focusDistance,
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
    m_focusDistance( focusDistance ),
    m_shutter( shutter ),
    m_aperture( aperture ),
    m_iso( iso ),
    m_dualIso( dualIso ),
    m_bitDepth( bitDepth ),
    m_dateTime( dateTime ),
    m_audio( audio ),
    m_backgroundColor( backgroundColor )
{
    QPixmap pixmap = QPixmap( m_previewMinWidth * 2, m_previewMinWidth * 2 / m_previewRatio );
    pixmap.fill( Qt::black );
    setPreview( pixmap );

    m_pReceipt = new ReceiptSettings();
    m_pReceipt->setFileName( path );
}

//Constructor
ClipInformation::ClipInformation(QString name,
                                 QString path) :
    m_name( name ),
    m_path( path ),
    m_camera( "–" ),
    m_lens( "–" ),
    m_resolution( "–" ),
    m_duration( "–" ),
    m_frames( "–" ),
    m_frameRate( "–" ),
    m_focalLength( "–" ),
    m_focusDistance( "–" ),
    m_shutter( "–" ),
    m_aperture( "–" ),
    m_iso( "–" ),
    m_dualIso( "–" ),
    m_bitDepth( "–" ),
    m_dateTime( "–" ),
    m_audio( "–" ),
    m_backgroundColor( QColor( 0, 0, 0, 0 ) )
{
    QPixmap pixmap = QPixmap( m_previewMinWidth * 2, m_previewMinWidth * 2 / m_previewRatio );
    pixmap.fill( Qt::black );
    setPreview( pixmap );

    m_pReceipt = new ReceiptSettings();
    m_pReceipt->setFileName( path );
}

void ClipInformation::setPreview( const QPixmap &src )
{
    int width = m_previewMinWidth * 2 * src.devicePixelRatio();
    width = src.width() < width ? width : src.width();
    int height = width / m_previewRatio;

    qreal factor = static_cast<qreal>( width ) / m_previewMinWidth;

    // Create thumbnail canvas
    qreal paddingTop = m_previewPaddingTop * factor;
    QPixmap thumbnail( width, height + paddingTop );

    thumbnail.fill( Qt::transparent );

    // Scale source to fit (or fill with Qt::KeepAspectRatioByExpanding)
    QPixmap scaled = src.scaled( width, width / m_previewRatio + 0.5, Qt::KeepAspectRatio, Qt::SmoothTransformation );
    scaled.setDevicePixelRatio( 1 );

    // Optional rounded corners
    qreal radius = m_radius * factor;

    QPainter painter( &thumbnail );
    painter.setRenderHint( QPainter::Antialiasing );

    QPainterPath path;
    path.addRoundedRect( 0, paddingTop, width, height, radius, radius );
    painter.setClipPath( path );

    // Center the pixmap
    qreal x = ( width - scaled.width() ) / 2.0;
    qreal y = ( height - scaled.height() ) / 2.0;

    // Only fill black if needed to prevent ugly corners
    if( x > 1 || y > 1 ) painter.fillPath( path, Qt::black );

    painter.drawPixmap( x, y + paddingTop, scaled );

    // Border
    // QPen pen( Qt::black, 2 );
    // painter.setPen( pen );
    // painter.setBrush( Qt::NoBrush );
    // painter.drawPath( path );

    m_preview = QIcon( thumbnail );
}

//Destructor
ClipInformation::~ClipInformation()
{
    delete m_pReceipt;
}

//Set filename and path - use when file will be renamed
void ClipInformation::setPathName(QString name, QString path)
{
    m_name = name;
    m_path = path;
}

//Read saved metadata
QVariant ClipInformation::getElement(int element) const
{
    QString frameRate = m_frameRate;
    frameRate.chop(4);

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
        return m_focusDistance;
    case 10:
        return m_shutter;
    case 11:
        return m_aperture;
    case 12:
        return m_iso;
    case 13:
        return m_dualIso;
    case 14:
        return m_bitDepth;
    case 15:
        return m_dateTime;
    case 16:
        return m_audio;
    case 17: //size
        return QString( "%1 MB" ).arg( clipSize() / 1024 / 1024 );
    case 18: //datarate
        if( m_frames == "-" || m_frameRate == "-" || 0.0 == m_frames.toDouble() || 0.0 == frameRate.toDouble() ) return QString( "-" );
        else return QString( "%1 MB/s" ).arg( (double)clipSize() / ( m_frames.toDouble() / frameRate.toDouble() ) / 1024 / 1024, 0, 'f', 2 );
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
        break;
    case 2:
        m_camera = value.toString();
        break;
    case 3:
        m_lens = value.toString();
        break;
    case 4:
        m_resolution = value.toString();
        break;
    case 5:
        m_duration = value.toString();
        break;
    case 6:
        m_frames = value.toString();
        break;
    case 7:
        m_frameRate = value.toString();
        break;
    case 8:
        m_focalLength = value.toString();
        break;
    case 9:
        m_focusDistance = value.toString();
        break;
    case 10:
        m_shutter = value.toString();
        break;
    case 11:
        m_aperture = value.toString();
        break;
    case 12:
        m_iso = value.toString();
        break;
    case 13:
        m_dualIso = value.toString();
        break;
    case 14:
        m_bitDepth = value.toString();
        break;
    case 15:
        m_dateTime = value.toString();
        break;
    case 16:
        m_audio = value.toString();
        break;
    default:
        break;
    }
}

//Update missing metadata to the class
void ClipInformation::updateMetadata(QString camera, QString lens, QString resolution, QString duration,
                                     QString frames, QString frameRate, QString focalLength, QString focusDistance, QString shutter,
                                     QString aperture, QString iso, QString dualIso, QString bitDepth, QString dateTime,
                                     QString audio)
{
    m_camera = camera.isEmpty() ? "–" : camera;
    m_lens = lens.isEmpty() ? "–" : lens;
    m_resolution = resolution.isEmpty() ? "–" : resolution;
    m_duration = duration.isEmpty() ? "–" : duration;
    m_frames = frames.isEmpty() ? "–" : frames;
    m_frameRate = frameRate.isEmpty() ? "–" : frameRate;
    m_focalLength = focalLength.isEmpty() ? "–" : focalLength;
    m_focusDistance = focusDistance.isEmpty() ? "–" : focusDistance;
    m_shutter = shutter.isEmpty() ? "–" : shutter;
    m_aperture = aperture.isEmpty() ? "–" : aperture;
    m_iso = iso.isEmpty() ? "–" : iso;
    m_dualIso = dualIso.isEmpty() ? "–" : dualIso;
    m_bitDepth = bitDepth.isEmpty() ? "–" : bitDepth;
    m_dateTime = dateTime.isEmpty() ? "–" : dateTime;
    m_audio = audio.isEmpty() ? "–" : audio;
}

//Calc the clip size
quint64 ClipInformation::clipSize() const
{
    quint64 calcSize = 0;
    QString mName = m_path;
    calcSize += QFileInfo( mName ).size();
    for( int nr = 0; nr < 100; nr++ )
    {
        mName.chop( 2 );
        mName.append( QString( "%1" ).arg( nr, 2, 10, QChar( '0' ) ) );
        if( QFileInfo( mName ).exists() )
        {
            calcSize += QFileInfo( mName ).size();
        }
        else
        {
            break;
        }
    }
    return calcSize;
}
