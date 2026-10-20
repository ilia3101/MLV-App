/*!
 * \file TimeCodeLabel.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief Paint TimeCode Image
 */

#include "TimeCodeLabel.h"
#include <QRect>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFontDatabase>
#include <QString>
#include <QIcon>
#include <AppTheme.h>

//Constructor
TimeCodeLabel::TimeCodeLabel( int height, qreal pixelRatio )
{
    m_mode = false;
    m_height = height * pixelRatio;
    m_devicePixelRatio = pixelRatio;

    QFontDatabase::addApplicationFont( ":/Fonts/Fonts/DSEG7Modern-Regular.ttf" );
}

//Destructor
TimeCodeLabel::~TimeCodeLabel()
{
}

//Get the image for frameNumber at clipFps
QImage TimeCodeLabel::getTimeCodeLabel(uint32_t frameNumber, float clipFps)
{
    // Measure real font size
    QString em = "M";
    QFontMetrics fm( QFont( "Arial", 16 ) );
    double factor = fm.horizontalAdvance( em ) / 16.0;
    int fontSize = ( m_height - ( m_height / 3 ) ) * factor;

    QFont font1 = QFont( "DSEG7 Modern", fontSize, QFont::Bold );
    QFont font2 = QFont( "Arial", fontSize / 2, QFont::Bold );

    QString text1 = "88 : 88 : 88 . 88";

    QFontMetrics fm1( font1 );
    int fm1W = fm1.horizontalAdvance( text1 );
    int fm1H = fm1.height();

    QString text2 = "TC";

    QFontMetrics fm2( font2 );
    int fm2W = fm2.horizontalAdvance( text2 );
    int fm2H = fm2.height();

    // QString text3 = "[<–>]";

    // QFontMetrics fm3( font2 );
    // int fm3W = fm3.horizontalAdvance( text3 );
    // int fm3H = fm3.height();

    int padding = m_height / 2;

    int x1 = padding;
    int y1 = ( m_height - fm1H ) / 2;

    int x2 = x1 + fm1W + font2.pointSize() / 2;
    int y2 = y1 - 1;

    int x3 = x1 + fm1W + font2.pointSize() / 2;
    int y3 = y1 + 1 + fm1H - fm2H;

    QIcon icon( ":/fontawesome/fontawesome/svgs-full/solid/arrow-right-to-bracket.svg" );
    QRect iconRect( x3, y3, fm2H, fm2H );

    QImage tcImage = QImage( fm1W + font2.pointSize() / 2 + iconRect.width() * 2 - 1 + padding * 2, m_height, QImage::Format_ARGB32_Premultiplied );

    QColor background = QColor::fromHsl( 0, 0, 16, 208 );
    QColor active = QColor::fromHsl( 0, 0, 255, 208 );
    QColor inactive = QColor::fromHsl( 0, 0, 255, 48 );

    //Background
    tcImage.fill( Qt::transparent );

    QPainter painterTc( &tcImage );
    painterTc.setRenderHint( QPainter::Antialiasing );

    QPainterPath path;
    path.addRoundedRect( tcImage.rect(), 4 * m_devicePixelRatio, 4 * m_devicePixelRatio );
    painterTc.setClipPath( path );
    painterTc.fillPath( path, background );

    //Border
    QRectF r = tcImage.rect().adjusted( 1, 1, -1, -1 );
    QLinearGradient gradient( r.topLeft(), r.bottomLeft() );
    gradient.setColorAt( 0.0, QColor::fromHsl( 0, 0, 255, 0 ) );
    gradient.setColorAt( 1.0, QColor::fromHsl( 0, 0, 255, 96 ) );

    QPen pen( QBrush( gradient ), 2 );
    painterTc.setPen( pen );
    painterTc.setBrush( Qt::NoBrush );
    painterTc.drawPath( path );

    painterTc.setFont( font2 );

    if( !m_mode )
    {
        painterTc.setPen( QPen( active ) );
        painterTc.drawText( x2, y2, fm2W, fm2H, 0, text2 );
        // painterTc.setPen( QPen( inactive ) );
        // painterTc.drawText( x3, y3, fm3W, fm3H, 0, text3 );
    }
    else
    {
        painterTc.setPen( QPen( inactive ) );
        painterTc.drawText( x2, y2, fm2W, fm2H, 0, text2 );
        // painterTc.setPen( QPen( active ) );
        // painterTc.drawText( x3, y3, fm3W, fm3H, 0, text3 );
    }

    QPixmap tinted = AppTheme::tintPixmap( icon.pixmap( iconRect.size() ), m_mode ? active : inactive, m_devicePixelRatio );
    QTransform transform;
    transform.scale( -1, 1 );
    QPixmap flipped = tinted.transformed( transform );

    painterTc.drawPixmap( iconRect.topLeft(), flipped );
    painterTc.drawPixmap( iconRect.adjusted( iconRect.width() - 1, 0, 0, 0 ).topLeft(), tinted );

    painterTc.setFont( font1 );

    //Shadow
    painterTc.setPen( QPen( inactive ) );
    painterTc.drawText( x1, y1, fm1W, fm1H, 0, text1 );

    //Foreground
    painterTc.setPen( QPen( active ) );
    painterTc.drawText( x1, y1, fm1W, fm1H, 0, getTimeCodeFromFps( frameNumber, clipFps, true ) );

    return tcImage;
}

//Generate Timecode String
QString TimeCodeLabel::getTimeCodeFromFps(uint32_t frameNumber, float clipFps, bool space)
{
    int frame = 0;
    //Invalid
    if( clipFps == 0.0f ) clipFps = 1.0f;
    //Normal
    else if( clipFps >= 1.0 )
    {
        frame = (int) (frameNumber % (int)(clipFps+0.5));
        frameNumber /= (int)(clipFps+0.5);
    }
    //Very slow fps
    else
    {
        frameNumber = (int)(frameNumber / clipFps);
    }
    int seconds = (int) (frameNumber % 60);
    frameNumber /= 60;
    int minutes = (int) (frameNumber % 60);
    frameNumber /= 60;
    int hours = (int) (frameNumber);

    if( space )
    {
        return QString( "%1 : %2 : %3 . %4" )
                          .arg( hours, 2, 10, QChar('0') )
                          .arg( minutes, 2, 10, QChar('0') )
                          .arg( seconds, 2, 10, QChar('0') )
                          .arg( frame, 2, 10, QChar('0') );
    }
    else
    {
        return QString( "%1:%2:%3" )
                          .arg( hours, 2, 10, QChar('0') )
                          .arg( minutes, 2, 10, QChar('0') )
                          .arg( seconds, 2, 10, QChar('0') );
    }
}

//Switch mode
void TimeCodeLabel::setTimeDurationMode(bool mode)
{
    m_mode = mode;
}
