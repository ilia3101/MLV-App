#ifndef APPTHEME_H
#define APPTHEME_H

#include <QProxyStyle>
#include <QGroupBox>

class QMainWindow;

class AppTheme
{
public:
    enum Theme { Light, Mid, Dark, Darker };
    static void set( QMainWindow *mainWindow, Theme theme );
    static Theme currentTheme();
    static int getScrollBarExtent();
    static QPixmap tintPixmap( const QPixmap &pixmap, const QColor &color = QColor(), qreal pixelRatio = 1 );

private:
    static Theme m_theme;
    static const int m_scrollBarHandleSize = 8;
};

class IconRecolorStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;
    void drawPrimitive( PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr ) const override;
    void drawControl( ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr ) const override;
    void drawItemPixmap( QPainter *painter, const QRect &rect, int alignment, const QPixmap &pixmap ) const override;

private:
    mutable QStyle::State m_state{};
    mutable QColor m_recolor{};
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
protected:
    bool eventFilter( QObject *obj, QEvent *event ) override;
};

#endif // APPTHEME_H
