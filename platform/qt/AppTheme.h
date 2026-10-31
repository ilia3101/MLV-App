#ifndef APPTHEME_H
#define APPTHEME_H

#include <QProxyStyle>
#include <QGroupBox>

class QMainWindow;

class AppTheme
{
public:
    enum Theme { Light, Mid, Dark, Darker };
    static void set( QMainWindow *MainWindow, Theme theme );
    static Theme currentTheme( void );
    static QPixmap tintPixmap( const QPixmap &pixmap, QColor color = QColor(), qreal pixelRatio = 1 );

private:
    static Theme m_theme;
};

class IconRecolorStyle : public QProxyStyle
{
public:
    explicit IconRecolorStyle( QStyle *base = nullptr ) : QProxyStyle( base ) {}
    void drawPrimitive( PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr ) const override;
    void drawControl( ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr ) const override;
    virtual void drawItemPixmap( QPainter *painter, const QRect &rect, int alignment, const QPixmap &pixmap ) const override;

private:
    mutable QStyle::State m_state;
    mutable QColor m_recolor;
};

class AccordionGroupBox : public QGroupBox
{
    Q_OBJECT

public:
    explicit AccordionGroupBox( QWidget *parent = nullptr );

protected:
    void paintEvent( QPaintEvent* ) override;

private slots:
    void onToggled( bool checked );

private:
    void drawIndicator( QPainter *painter, const QStyleOptionGroupBox &opt ) const;
};

class CustomEventFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter( QObject *obj, QEvent *event ) override;
};

#endif  // APPTHEME_H
