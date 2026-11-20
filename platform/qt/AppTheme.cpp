#include <AppTheme.h>

#include <QStyleHints>
#include <QApplication>
#include <QMainWindow>
#include <QStyleFactory>
#include <QStyleOption>
#include <QAbstractButton>
#include <QStylePainter>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QMenu>
#include <QDebug>

AppTheme::Theme AppTheme::m_theme = AppTheme::Dark;

void AppTheme::set( Theme theme )
{
    m_theme = theme;

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    int dockWidgetIconSize = 23;
#else
    int dockWidgetIconSize = 14;
#endif

    QString styleSheet = QString(R"(
        QMainWindow::separator {
            background: hsla(0, 0%, 0%, 15%);
            width: 1px;
            height: 1px;
        }

        #menuBar,
        #mainToolBar {
            border: none;
            border-bottom: 1px solid hsla(0, 0%, 0%, 15%);
        }

        QListView,
        QTreeView,
        QTableView {
            border: 1px solid hsla(0, 0%, 0%, 15%);
        }

        QHeaderView::section {
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 hsla(0, 0%, 100%, 100%), stop:1 hsla(0, 0%, 0%, 0%));
            font-size: 10pt;
            padding: 0 7px;
            border: none;
            /*border-right: 1px solid hsla(0, 0%, 0%, 15%);*/
            border-bottom: 1px solid hsla(0, 0%, 0%, 15%);
        }

        QHeaderView::section:last {
            border-right: none;
        }

        #listViewSession,
        #tableViewSession,
        #horizontalSliderPositionLayout,
        #scrollArea,
        QStatusBar,
        QFrame[frameShape="4"] {
            border: none;
            border-top: 1px solid hsla(0, 0%, 0%, 15%);
        }

        #listViewSession,
        #tableViewSession {
            font-size: 11pt;
        }

        /*QListView::item {
            padding: 4px 0;
        }
        QListView::item:icon {
            padding: 2px 0;
        }*/

        /* ==================================================== */
        QToolBar {
            border: none;
            padding: 2px;
            spacing: 2px;
        }

        QToolBar QToolButton {
            padding: 1px 0 0 1px;
        }

        QToolBar::separator {
            background: hsla(0, 0%, 0%, 15%);
            width: 1px;
            height: 1px;
            margin: 4px 1px;
        }

        #mainToolBarPlayback.QToolBar::separator {
            background: hsla(0, 0%, 0%, 0%);
            margin-right: 0;
        }

        QToolBar QToolButton#qt_toolbar_ext_button {
            margin: 0;
            qproperty-icon: url(:/fontawesome/fontawesome/svgs-full/solid/angles-right.svg);
            qproperty-iconSize: 12px;
        }

        /* ==================================================== */
        QDockWidget {
            titlebar-close-icon: url(:/fontawesome/fontawesome/svgs-full/solid/close.svg);
            titlebar-normal-icon: url(:/fontawesome/fontawesome/svgs-full/regular/window-restore.svg);
        }

        QDockWidget::title {
            background: none;
            text-align: left;
            padding: 0 6px;
            border: none;
        }

        QDockWidget::close-button,
        QDockWidget::float-button {
            subcontrol-position: center right;
            subcontrol-origin: margin;
            width: 23px;
            height: 23px;
            icon-size: %3px; /* maximum icon size */
            padding: -5px;
            margin: 0px;
            position: absolute;
        }

        QDockWidget::close-button {
            right: 6px;
        }

        QDockWidget::float-button {
            right: 24px;
        }

        /* ==================================================== */
        #graphicsView,
        #labelAudioTrack,
        #labelScope {
            background: hsl(0, 0%, 0%);
        }

        /* ==================================================== */
        QGroupBox {
            background: hsla(0, 0%, 0%, 6%);
            padding: 0;
            margin-top: 20px;
            border: 1px solid hsla(0, 0%, 0%, 15%);
            border-radius: 4px;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            margin: 0;
        }

        #dockWidgetEdit QGroupBox {
            font-size: 11pt;
            font-weight: bold;
            text-transform: uppercase;
            margin-top: 32px;
            border-left: none;
            border-right: none;
            border-radius: 0;
        }

        #dockWidgetEdit QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            margin: 13px 0 0 5px;
        }

        #dockWidgetEdit QGroupBox::indicator {
            width: 12px;
            height: 12px;
            margin-right: -3px;
        }

        #dockWidgetEdit QGroupBox::indicator {
            image: none;
        }

        /*
        #dockWidgetEdit QGroupBox::indicator:unchecked {
            image: url(:/fontawesome/fontawesome/svgs-full/solid/chevron-right.svg);
        }

        #dockWidgetEdit QGroupBox::indicator:checked {
            image: url(:/fontawesome/fontawesome/svgs-full/solid/chevron-down.svg);
        }

        #dockWidgetEdit QGroupBox::indicator:unchecked:disabled {
            image: url(:/fontawesome/fontawesome/svgs-full/solid/chevron-right.svg);
        }

        #dockWidgetEdit QGroupBox::indicator:checked:disabled {
            image: url(:/fontawesome/fontawesome/svgs-full/solid/chevron-down.svg);
        }
        */

        #dockWidgetEdit QLabel,
        #dockWidgetEdit QAbstractButton,
        #dockWidgetEdit QComboBox,
        #dockWidgetEdit QCheckBox,
        #dockWidgetEdit QLineEdit,
        #dockWidgetEdit QAbstractSpinBox {
            font-size: 10pt;
        }

        /* ==================================================== */
        QSlider::groove:horizontal {
            height: 5px;
            border-radius: 2px;
        }

        QSlider::add-page:horizontal {
            background: qlineargradient(spread:pad, x0:0, y0:0, x2:0, y2:1, stop:0 hsla(0, 0%, 0%, 15%), stop:1 hsla(0, 0%, 100%, 9%));
            border: 1px solid hsla(0, 0%, 15%, 24%);
            border-radius: 2px;
        }

        QSlider::sub-page:horizontal {
            background: palette(highlight);
            border: 1px solid hsl(210, 100%, 38%);
            border-radius: 2px;
        }

        QSlider::sub-page:horizontal:disabled,
        #horizontalSliderTemperature::sub-page:horizontal:disabled,
        #horizontalSliderTemperature::add-page:horizontal:disabled,
        #horizontalSliderTint::sub-page:horizontal:disabled,
        #horizontalSliderTint::add-page:horizontal:disabled {
            background: hsla(0, 0%, 0%, 15%);
            border-color: hsla(0, 0%, 0%, 24%);
        }

        QSlider::handle:horizontal {
            background: qlineargradient(spread:pad, x0:0, y0:0, x2:0, y2:1, stop:0 hsl(0, 0%, 100%), stop:1 hsl(0, 0%, 91%));
            border: 1px solid hsl(0, 0%, 62%);
            width: 11px;
            margin: -4px 0;
            border-radius: 2px;
        }

        QSlider::handle:horizontal:disabled {
            background: hsl(0, 0%, 91%);
            border-color: hsla(0, 0%, 62%, 76%);
        }

        #horizontalSliderTemperature::sub-page:horizontal,
        #horizontalSliderTemperature::add-page:horizontal,
        #horizontalSliderTint::sub-page:horizontal,
        #horizontalSliderTint::add-page:horizontal {
            background: none;
            border: 1px solid hsla(0, 0%, 0%, 38%);
        }

        #horizontalSliderTemperature::groove:horizontal {
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 hsl(210, 100%, 50%), stop:1 hsl(30, 100%, 50%));
        }

        #horizontalSliderTint::groove:horizontal {
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 hsl(120, 100%, 50%), stop:1 hsl(300, 100%, 50%));
        }

        #horizontalSliderTemperature::groove:horizontal:disabled,
        #horizontalSliderTint::groove:horizontal:disabled {
            background: none;
        }

        /* ==================================================== */
        QMenu {
            padding: 5px 0;
            border: none;
            border-radius: 5px;
        }

        /*QMenu {
            background: palette(window);
            padding: 5px 0;
            border: none;
            icon-size: 0;
        }

        QMenu::item {
            padding: 2px 32px 3px 26px;
        }

        QMenu::item:selected {
            background: palette(highlight);
            color: palette(highlighted-text);
        }

        QMenu::indicator {
            width: 14px;
            height: 14px;
            margin: 0 -24px 0 5px;
        }

        QMenu::indicator:checked {
            image: url(:/fontawesome/fontawesome/svgs-full/solid/check.svg);
            margin: 0 -24px 0 5px;
        }

        QMenu::right-arrow {
            image: url(:/fontawesome/fontawesome/svgs-full/solid/chevron-right.svg);
            width: 14px;
            height: 14px;
            margin-right: 10px;
        }*/

        QMenu::separator {
            background: hsla(0, 0%, 0%, 15%);
            height: 1px;
            margin: 5px 14px;
        }

        QComboBox QAbstractItemView {
            background: palette(window);
            padding: 5px 0;
            border: none;
        }

        QToolTip {
            background: palette(window);
            font-size: 11pt;
            padding: 1px 2px;
            margin: 0;
            border: none;
            border-radius: 3px;
        }

        /* ==================================================== */
        QScrollBar {
            border: none
        }

        QScrollBar:vertical {
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 hsla(0, 0%, 0%, 6%), stop:1 hsla(0, 0%, 0%, 0%));
            width: %1px;
            padding: 0 -5px;
            border-left: 1px solid hsla(0, 0%, 0%, 15%);
        }

        QScrollBar:horizontal {
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 hsla(0, 0%, 0%, 6%), stop:1 hsla(0, 0%, 0%, 0%));
            height: %1px;
            border-top: 1px solid hsla(0, 0%, 0%, 15%);
        }

        QScrollBar::handle {
            background: hsla(0, 0%, 0%, 25%);
            margin: 3px;
            border: none;
            border-radius: %2px;
        }

        QScrollBar::handle:vertical {
            min-height: 20px;
            margin: 3px 8px;
        }

        QScrollBar::handle:horizontal {
            min-width: 20px;
        }

        QScrollBar::handle:hover {
            background: hsla(0, 0%, 0%, 50%);
            border: none;
        }

        QScrollBar::add-line,
        QScrollBar::sub-line {
            background: none;
            width: 0;
            height: 0;
            border: none;
        }

        QScrollBar::add-page,
        QScrollBar::sub-page {
            background: none;
        }
    )").arg( getScrollBarExtent() ).arg( m_scrollBarHandleSize / 2 ).arg( dockWidgetIconSize );

    const int textContrast = 255 * 0.75;

    QColor window = QColor::fromHsl(0, 0, 232);
    QColor base = window;
    QColor alternateBase = QColor::fromHsl(0, 0, std::max(base.lightness() - 12, 0));
    QColor text = QColor::fromHsl(0, 0, std::max(window.lightness() - textContrast, 0));
    QColor highlight = QColor::fromHsl(210, 255, 160);
    QColor disabled = QColor::fromHsl(0, 0, window.lightness() * 0.6 + text.lightness() * 0.4);

    if( m_theme > Light )
    {
        QString borderColor = "hsla(0, 0%, 0%, 38%)";

        switch( m_theme )
        {
            case Mid:
                window = QColor::fromHsl(0, 0, 64);
                break;
            case Dark:
                window = QColor::fromHsl(0, 0, 48);
                borderColor = "hsla(0, 0%, 0%, 50%)";
                break;
            case Darker:
                window = QColor::fromHsl(0, 0, 32);
                borderColor = "hsla(0, 0%, 0%, 76%)";
                break;
        }

        text = QColor::fromHsl(0, 0, std::min(window.lightness() + textContrast, 255));
        disabled = QColor::fromHsl(0, 0, window.lightness() * 0.6 + text.lightness() * 0.4);

        base = window;
        alternateBase = QColor::fromHsl(0, 0, std::min(base.lightness() + 12, 255));
        highlight = QColor::fromHsl(210, 255, 144, 255-32);

        styleSheet += QString(R"(
            QMainWindow::separator {
                background: %1;
            }

            QMenu,
            QComboBox QAbstractItemView,
            QToolTip {
                border: 1px solid hsla(0, 0%, 100%, 15%);
            }

            QMenu::separator {
                background: hsla(0, 0%, 100%, 15%);
            }

            #menuBar,
            #mainToolBar {
                border-bottom-color: %1;
            }

            QListView,
            QTreeView,
            QTableView {
                border-color: %1;
            }

            QHeaderView::section {
                background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 hsla(0, 0%, 100%, 9%), stop:1 hsla(0, 0%, 0%, 0%));
                /*border-right-color: %1;*/
                border-bottom-color: %1;
            }

            #listViewSession,
            #tableViewSession,
            #horizontalSliderPositionLayout,
            #scrollArea,
            QStatusBar {
                border-top-color: %1;
            }

            QFrame[frameShape="4"]  {
                border-top-color: hsla(0, 0%, 100%, 15%);
            }

            QToolBar::separator {
                background: hsla(0, 0%, 100%, 15%);
            }

            QGroupBox {
                background: hsla(0, 0%, 100%, 6%);
                border-color: %1;
            }

            /*QSlider::groove:horizontal {
                height: 7px;
            }*/

            QSlider::add-page:horizontal {
                background: qlineargradient(spread:pad, x0:0, y0:0, x2:0, y2:1, stop:0 hsla(0, 0%, 0%, 38%), stop:1 hsla(0, 0%, 0%, 15%));
                border-color: hsla(0, 0%, 0%, 50%);
            }

            QSlider::sub-page:horizontal {
                border-color: hsl(210, 100%, 15%);
            }

            QSlider::sub-page:horizontal:disabled,
            #horizontalSliderTemperature::sub-page:horizontal:disabled,
            #horizontalSliderTemperature::add-page:horizontal:disabled,
            #horizontalSliderTint::sub-page:horizontal:disabled,
            #horizontalSliderTint::add-page:horizontal:disabled {
                background: hsla(0, 0%, 100%, 24%);
                border-color: hsla(0, 0%, 0%, 62%);
            }

            QSlider::handle:horizontal {
                background: qlineargradient(spread:pad, x0:0, y0:0, x2:0, y2:1, stop:0 hsl(0, 0%, 100%), stop:1 hsl(0, 0%, 76%));
                border-color: hsla(0, 0%, 0%, 76%);
                /*margin: -3px 0;*/
            }

            QSlider::handle:horizontal:disabled {
                background: hsl(0, 0%, 62%);
                border-color: hsla(0, 0%, 0%, 62%);
            }

            #horizontalSliderTemperature::sub-page:horizontal,
            #horizontalSliderTemperature::add-page:horizontal,
            #horizontalSliderTint::sub-page:horizontal,
            #horizontalSliderTint::add-page:horizontal {
                border-color: hsla(0, 0%, 0%, 76%);
            }

            QScrollBar:vertical {
                background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 hsla(0, 0%, 0%, 9%), stop:1 hsla(0, 0%, 0%, 0%));
                border-left-color: %1;
            }

            QScrollBar:horizontal {
                background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 hsla(0, 0%, 0%, 9%), stop:1 hsla(0, 0%, 0%, 0%));
                border-top-color: %1;
            }

            QScrollBar::handle {
                background: hsla(0, 0%, 100%, 25%);
            }

            QScrollBar::handle:hover {
                background: hsla(0, 0%, 100%, 50%);
            }
        )").arg( borderColor );

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        qApp->styleHints()->setColorScheme( Qt::ColorScheme::Dark );
#endif
    }
    else
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        qApp->styleHints()->setColorScheme( Qt::ColorScheme::Light );
#endif
    }

    const qreal alpha = 0.5 - ( 0.25 * text.lightness() / 255.0 );

    QColor link = QColor::fromHsl(
        highlight.hslHue(),
        highlight.hslSaturation(),
        ( 1.0 - alpha ) * highlight.lightness() + alpha * text.lightness()
    );

    QPalette palette;

    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);

    palette.setColor(QPalette::Button, window);
    // palette.setColor(QPalette::Light, window);
    // palette.setColor(QPalette::Midlight, window);
    // palette.setColor(QPalette::Dark, window);
    // palette.setColor(QPalette::Mid, window);

    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);

    palette.setColor(QPalette::BrightText, Qt::red);

    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);

    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::Disabled, QPalette::Base, window);

    palette.setColor(QPalette::Window, window);
    // palette.setColor(QPalette::Shadow, window);

    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(255, 255, 255, 64));

    palette.setColor(QPalette::HighlightedText, QColor::fromHsl(0, 0, 255));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);

    palette.setColor(QPalette::Link, link);
    palette.setColor(QPalette::LinkVisited, QColor::fromHsl(310, 128, link.lightness()));
    palette.setColor(QPalette::AlternateBase, alternateBase);
    palette.setColor(QPalette::ToolTipBase, window);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, disabled);
    // palette.setColor(QPalette::Accent, highlight);

    qApp->setStyle( new IconRecolorStyle( QStyleFactory::create( "Fusion" ) ) );
    qApp->setPalette( palette );

    QFont font = QFont();

    // Reset the StyleStrategy to fix some font rendering bugs
    font.setStyleStrategy( QFont::StyleStrategy( QFont::PreferAntialias ) );
    font.setPointSize( 13 );

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Disable kerning to fix Qt 5 punctuation spacing issues
    font.setKerning( false );
#endif

    qApp->setFont( font );

    for( QWidget *widget : qApp->topLevelWidgets() )
    {
        // Reset the window stylesheet just in case
        if( QMainWindow *mainWindow = qobject_cast<QMainWindow*>( widget ) )
        {
            mainWindow->setStyleSheet( "" );
        }
    }

    qApp->setStyleSheet( styleSheet );

    static CustomEventFilter customEventFilter;
    qApp->installEventFilter( &customEventFilter );
}

AppTheme::Theme AppTheme::currentTheme()
{
    return m_theme;
}

int AppTheme::getScrollBarExtent()
{
    return m_scrollBarHandleSize + 7;
}

QPixmap AppTheme::tintPixmap( const QPixmap &pixmap, const QColor &color, qreal pixelRatio )
{
    const QColor tint = color.isValid() ? color : QApplication::palette().color( QPalette::Text );

    QPixmap tinted( pixmap.size() );
    tinted.setDevicePixelRatio( pixelRatio );
    tinted.fill( Qt::transparent );

    QPainter painter( &tinted );
    painter.drawPixmap( 0, 0, pixmap );
    painter.setCompositionMode( QPainter::CompositionMode_SourceIn );
    painter.fillRect( tinted.rect(), tint );

    return tinted;
}

void IconRecolorStyle::drawPrimitive( PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget ) const
{
    //qDebug() << option->state << element << widget;

    // Prevent drawing an ugly rectangle behind a QComboBox menu
    if( element == PE_PanelMenu ) return;

    if( !widget || !widget->inherits( "QMenu" ) )
    {
        QProxyStyle::drawPrimitive( element, option, painter, widget );
        return;
    }

    QPalette palette = QApplication::palette();

    QColor enabled = m_state & State_Selected ? palette.color( QPalette::HighlightedText ) : palette.color( QPalette::Text );
    QColor color = option->state & State_Enabled ? enabled : palette.color( QPalette::Disabled, QPalette::Text );

    QRectF r = option->rect;

    if( element == PE_IndicatorCheckBox )
    {
        if( !(option->state & State_On) ) return;

        const qreal size = r.height();

        QIcon icon( ":/fontawesome/fontawesome/svgs-full/solid/check.svg" );
        QRectF iconRect( r.center().x() - size / 2.0, r.center().y() - size / 2.0, size, size );

        QPixmap tinted = AppTheme::tintPixmap( icon.pixmap( iconRect.size().toSize() ), color, painter->device()->devicePixelRatioF() );
        painter->drawPixmap( iconRect.topLeft(), tinted );
    }
    else if( element == PE_IndicatorArrowRight )
    {
        const qreal size = r.height() + 2;

        QIcon icon( ":/fontawesome/fontawesome/svgs-full/solid/chevron-right.svg" );
        QRectF iconRect( r.center().x() - size / 2.0 - 4, r.center().y() - size / 2.0, size, size );

        QPixmap tinted = AppTheme::tintPixmap( icon.pixmap( iconRect.size().toSize() ), color, painter->device()->devicePixelRatioF() );
        painter->drawPixmap( iconRect.topLeft(), tinted );
    }
}

void IconRecolorStyle::drawControl( ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget ) const
{
    m_state = option->state;
    m_recolor = QColor();

    //qDebug() << element << widget;

    if( !widget )
    {
        if( element == CE_RubberBand || element == CE_MenuScroller ) QProxyStyle::drawControl( element, option, painter, widget );
        return;
    }

    QIcon::Mode mode;

    if( !(option->state & State_Enabled) )
        mode = QIcon::Disabled;
    else if( option->state & State_Sunken || option->state & State_On )
        mode = QIcon::Active;
    else if( option->state & State_MouseOver )
        mode = QIcon::Selected;
    else
        mode = QIcon::Normal;

    QPalette palette = QApplication::palette();

    QColor enabled = palette.color( QPalette::Text );
    enabled.setAlpha( std::max( enabled.alpha() - 32, 0 ) );
    QColor disabled = enabled;
    disabled.setAlpha( std::max( disabled.alpha() - 128, 0 ) );

    QColor color = mode == QIcon::Disabled ? disabled : enabled;

    if( element == CE_PushButtonLabel || element == CE_ToolButtonLabel || element == CE_ComboBoxLabel )
    {
        if( element == CE_PushButtonLabel )
        {
            const QAbstractButton* btn = qobject_cast<const QAbstractButton*>( widget );
            //qDebug() << btn;

            if( btn && !btn->icon().isNull() /*&& btn->property( "recolorIcon" ).toBool()*/ )
            {
                QIcon::State state = option->state & State_On ? QIcon::On : QIcon::Off;

                QPixmap pixmap = btn->icon().pixmap( btn->iconSize(), mode, state );
                QPixmap tinted = AppTheme::tintPixmap( pixmap, color, painter->device()->devicePixelRatioF() );

                if( btn->text().isEmpty() )
                {
                    QProxyStyle::drawItemPixmap( painter, option->rect, Qt::AlignCenter, tinted );
                    return;
                }

                const qreal iconSize = btn->iconSize().width();
                const qreal spacing = 4.0;

                QFontMetricsF fm( painter->font() );
                const qreal textWidth = fm.horizontalAdvance( btn->text() );
                const qreal totalWidth = iconSize + spacing + textWidth;
                const qreal startX = btn->rect().center().x() - totalWidth / 2.0;

                QRectF iconRect( startX, btn->rect().center().y() - iconSize / 2.0 + 1, iconSize, iconSize );
                painter->drawPixmap( iconRect.topLeft(), tinted );

                QRectF textRect( startX + iconSize + spacing, btn->rect().top(), textWidth, btn->rect().height() );
                painter->drawText( textRect, Qt::AlignVCenter | Qt::AlignLeft, btn->text() );

                return;
            }
        }

        m_recolor = color;
    }
    else if( element == CE_MenuItem )
    {
        auto mi = qstyleoption_cast<const QStyleOptionMenuItem *>( option );

        if( !mi ) return;

        // Draw only background (no text, no icon)
        QStyleOptionMenuItem opt = *mi;
        opt.icon = QIcon();
        opt.text = QString();

        // Don't draw checkmark if icon exist
        if( !mi->icon.isNull() ) opt.checkType = QStyleOptionMenuItem::NotCheckable;

        QProxyStyle::drawControl( element, &opt, painter, widget );

        const qreal iconSize = pixelMetric( PM_SmallIconSize, option, widget );
        const qreal spacing = 7.0;

        QColor iconEnabled = mi->state & State_Selected ? palette.color( QPalette::HighlightedText ) : enabled;
        QColor iconColor = mode == QIcon::Disabled ? disabled : iconEnabled;

        QColor textEnabled = mi->state & State_Selected ? palette.color( QPalette::HighlightedText ) : palette.color( QPalette::Text );
        QColor textColor = mi->state & State_Enabled ? textEnabled : palette.color( QPalette::Disabled, QPalette::Text );

        if( !mi->icon.isNull() )
        {
            QIcon::State state = mi->checked ? QIcon::On : QIcon::Off;

            QPixmap pixmap = mi->icon.pixmap( iconSize, iconSize, mode, state );
            QPixmap tinted = AppTheme::tintPixmap( pixmap, iconColor, painter->device()->devicePixelRatioF() );

            QRectF iconRect( mi->rect.left() + spacing, mi->rect.center().y() - iconSize / 2.0 + 1, iconSize, iconSize );
            painter->drawPixmap( iconRect.topLeft(), tinted );
        }

        QRectF textRect = mi->rect.adjusted( spacing, 0, 0, 0 );

        if( !mi->icon.isNull() || widget->inherits( "QMenu" ) )
        {
            textRect = mi->rect.adjusted( iconSize + spacing * 2.0 - 2, 0, 0, 0 );
        }

        painter->setPen( textColor );
        painter->drawText( textRect, Qt::AlignVCenter | Qt::AlignLeft, mi->text );

        return;
    }

    QProxyStyle::drawControl( element, option, painter, widget );
}

void IconRecolorStyle::drawItemPixmap( QPainter *painter, const QRect &rect, int alignment, const QPixmap &pixmap ) const
{
    QPixmap tinted = pixmap;
    if( m_recolor.isValid() ) tinted = AppTheme::tintPixmap( pixmap, m_recolor, painter->device()->devicePixelRatioF() );
    QProxyStyle::drawItemPixmap( painter, rect, alignment, tinted );
}

AccordionGroupBox::AccordionGroupBox( QWidget *parent ) : QGroupBox( parent )
{
    connect( this, &QGroupBox::toggled, this, &AccordionGroupBox::onToggled );
}

void AccordionGroupBox::onToggled( bool checked )
{
    if( checked ) setStyleSheet( "" );

    const auto childWidgets = findChildren<QWidget*>( QString(), Qt::FindDirectChildrenOnly );

    for( QWidget* w : childWidgets )
    {
        w->setVisible( checked );
    }

    if( !checked ) setStyleSheet( "background: none; border-bottom: none; max-height: 0;" );
}

void AccordionGroupBox::paintEvent( QPaintEvent* )
{
    QStylePainter painter( this );
    QStyleOptionGroupBox option;
    initStyleOption( &option );

    painter.drawComplexControl( QStyle::CC_GroupBox, option );
    drawIndicator( &painter, option );
}

void AccordionGroupBox::drawIndicator( QPainter *painter, const QStyleOptionGroupBox &opt ) const
{
    QFontMetricsF fm( this->font() );

    QRectF textRect = style()->subControlRect( QStyle::CC_GroupBox, &opt, QStyle::SC_GroupBoxLabel, this );

    const qreal iconSize = std::max( fm.height() - 1.0, 0.0 );
    const qreal iconTop = textRect.center().y() - iconSize / 2.0;
    const qreal margin = ( textRect.left() * 0.86 - iconSize ) / 2.0;

    QString svgPath = ":/fontawesome/fontawesome/svgs-full/solid/chevron-";
    svgPath += isChecked() ? "down.svg" : "right.svg";

    QIcon icon( svgPath );

    QColor color = palette().color( QPalette::Text );
    color.setAlpha( color.alpha() - 96 ) ;

    QRectF iconRect( textRect.left() - iconSize - margin, iconTop, iconSize, iconSize );

    QPixmap tinted = AppTheme::tintPixmap( icon.pixmap( iconRect.size().toSize() ), color, painter->device()->devicePixelRatioF() );
    painter->drawPixmap( iconRect.topLeft(), tinted );
}

bool CustomEventFilter::eventFilter( QObject *obj, QEvent *event )
{
    if( event->type() == QEvent::Show )
    {
        static bool startup = true;

        if( startup && qobject_cast<QMainWindow*>( obj ) )
        {
            startup = false;

            // Just to fix some rendering bugs
            AppTheme::set( AppTheme::currentTheme() );
        }
    }

    if( event->type() == QEvent::Polish )
    {
        QWidget *widget = qobject_cast<QWidget*>( obj );

        if( widget )
        {
            // qDebug() << widget << widget->windowType();

            // Fix QSS border-radius with QMenu and QToolTip
            if( qobject_cast<QMenu*>( widget ) || widget->windowType() == Qt::ToolTip )
            {
                widget->setAttribute( Qt::WA_TranslucentBackground );
                widget->setWindowFlags( widget->windowFlags() | Qt::FramelessWindowHint );
                // qDebug() << widget;
            }
        }
    }

    if( event->type() == QEvent::Paint )
    {
        QPushButton *pushButton = qobject_cast<QPushButton*>( obj );
        QToolButton *toolButton = qobject_cast<QToolButton*>( obj );
        QComboBox *comboBox = qobject_cast<QComboBox*>( obj );

        QWidget *widget = pushButton ? static_cast<QWidget*>( pushButton )
                        : toolButton ? static_cast<QWidget*>( toolButton )
                                     : static_cast<QWidget*>( comboBox );

        if( widget )
        {
            QPalette palette = QPalette();
            QColor windowColor = QApplication::palette().color( QPalette::Window );

            if( AppTheme::currentTheme() > AppTheme::Light )
            {
                palette.setColor( QPalette::Window, QColor::fromHsl( 0, 0, std::max( windowColor.lightness() - 32, 0 ), 128 ) );
            }
            else
            {
                palette.setColor( QPalette::Window, QColor::fromHsl( 0, 0, 0, 56 ) );
            }

            if( toolButton && toolButton->isCheckable() && toolButton->isChecked() )
            {
                if( AppTheme::currentTheme() > AppTheme::Light )
                {
                    QColor textColor = QApplication::palette().color( QPalette::Text );
                    QColor buttonColor = QApplication::palette().color( QPalette::Button );

                    // if( toolButton->isEnabled() )
                    // {
                    //     QColor highlightColor = QApplication::palette().color( QPalette::Highlight );

                    //     palette.setColor( QPalette::Button, QColor::fromHsl(
                    //         highlightColor.hslHue(),
                    //         highlightColor.hslSaturation(),
                    //         ( buttonColor.lightness() + highlightColor.lightness() - 32 ) / 2
                    //     ));
                    // }
                    // else
                    // {
                        buttonColor = QColor::fromHsl(
                            buttonColor.hslHue(),
                            buttonColor.hslSaturation(),
                            std::min( buttonColor.lightness() + 48, 255 )
                        );

                        palette.setColor( QPalette::Window, QColor::fromHsl( 0, 0, std::max( windowColor.lightness() - 32, 0 ), 160 ) );
                        palette.setColor( QPalette::Button, buttonColor );
                    // }

                    palette.setColor( QPalette::Disabled, QPalette::ButtonText, QColor::fromHsl(
                        0, 0, buttonColor.lightness() * 0.6 + textColor.lightness() * 0.4
                    ));
                }
                else
                {
                    palette.setColor( QPalette::Window, QColor::fromHsl( 0, 0, 0, 40 ) );
                }
            }

            widget->setPalette( palette );
        }
    }

    return QObject::eventFilter( obj, event );
}
