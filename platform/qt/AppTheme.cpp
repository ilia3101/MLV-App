#include <AppTheme.h>

#include <QApplication>
#include <QMainWindow>
#include <QStyleFactory>
#include <QStyleOption>
#include <QAbstractButton>
#include <QStylePainter>

AppTheme::Theme AppTheme::m_theme = AppTheme::Dark;

void AppTheme::set( QMainWindow *MainWindow, Theme theme )
{
    m_theme = theme;

    qApp->setStyle( new IconRecolorStyle( QStyleFactory::create( "Fusion" ) ) );

    QString styleSheet = R"(
        QMainWindow::separator {
            background: hsla(0, 0%, 0%, 15%);
            width: 1px;
            height: 1px;
        }

        #menuBar, #mainToolBar {
            border: none;
            border-bottom: 1px solid hsla(0, 0%, 0%, 15%);
        }
        QListView, QTreeView, QTableView {
            border: 1px solid hsla(0, 0%, 0%, 15%);
        }
        #listViewSession, #tableViewSession, #horizontalSliderPositionLayout, #scrollArea, QStatusBar, QFrame[frameShape="4"] {
            border: none;
            border-top: 1px solid hsla(0, 0%, 0%, 15%);
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
        QDockWidget::close-button, QDockWidget::float-button {
            subcontrol-position: center right;
            subcontrol-origin: margin;
            width: 23px;
            height: 23px;
            icon-size: 23px; /* maximum icon size */
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
        #graphicsView, #labelAudioTrack, #labelScope {
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
            font-size: 11px;
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

        /* ==================================================== */
        QSlider::groove:horizontal {
            height: 5px;
            border-radius: 2px;
        }
        QSlider::add-page:horizontal {
            background: qlineargradient(spread:pad, x0:0, y0:0, x2:0, y2:1, stop:0 hsla(0, 0%, 0%, 15%), stop:1 hsla(0, 0%, 0%, 0%));
            border: 1px solid hsla(0, 0%, 0%, 24%);
            border-radius: 2px;
        }
        QSlider::sub-page:horizontal {
            background: palette(highlight);
            border: 1px solid hsl(210, 100%, 38%);
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: qlineargradient(spread:pad, x0:0, y0:0, x2:0, y2:1, stop:0 hsl(0, 0%, 100%), stop:1 hsl(0, 0%, 91%));
            border: 1px solid hsla(0, 0%, 0%, 24%);
            width: 11px;
            margin: -4px 0;
            border-radius: 2px;
        }
        #horizontalSliderTemperature::sub-page:horizontal, #horizontalSliderTemperature::add-page:horizontal, #horizontalSliderTint::sub-page:horizontal, #horizontalSliderTint::add-page:horizontal {
            background: none;
            border: 1px solid hsla(0, 0%, 0%, 38%);
        }
        #horizontalSliderTemperature::groove:horizontal {
            background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 hsl(210, 100%, 50%), stop:1 hsl(30, 100%, 50%));
        }
        #horizontalSliderTint::groove:horizontal {
            background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 hsl(120, 100%, 50%), stop:1 hsl(300, 100%, 50%));
        }

        /* ==================================================== */
        QMenu {
            padding: 5px 0;
            border: none;
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
            margin: 4px 14px;
        }

        QComboBox QAbstractItemView {
            background: palette(window);
            padding: 5px 0;
            border: none;
        }

        QToolTip {
            background: palette(window);
            padding: 1px 2px;
            margin: 0;
            border: none;
        }

        /* ==================================================== */
        QScrollBar {
            border: none
        }
        QScrollBar:vertical {
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 hsla(0, 0%, 0%, 6%), stop:1 hsla(0, 0%, 0%, 0%));
            width: 15px;
            padding: 0 -5px;
            border-left: 1px solid hsla(0, 0%, 0%, 15%);
        }
        QScrollBar:horizontal {
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 hsla(0, 0%, 0%, 6%), stop:1 hsla(0, 0%, 0%, 0%));
            height: 15px;
            border-top: 1px solid hsla(0, 0%, 0%, 15%);
        }
        QScrollBar::handle {
            background: hsla(0, 0%, 0%, 25%);
            margin: 3px;
            border: none;
            border-radius: 4px;
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
        QScrollBar::add-line, QScrollBar::sub-line {
            background: none;
            width: 0;
            height: 0;
            border: none;
        }
        QScrollBar::add-page, QScrollBar::sub-page {
            background: none;
        }
    )";

    QPalette palette;

    QColor window = QColor::fromHsl(0, 0, 232);
    QColor base = window;
    QColor alternateBase = QColor::fromHsl(0, 0, 0, 16);
    QColor text = QColor::fromHsl(0, 0, 32);
    QColor highlight = QColor::fromHsl(210, 255, 160);
    QColor disabled = QColor::fromHsl(0, 0, 128, 192);

    if( m_theme > Light )
    {
        QString borderColor = "hsla(0, 0%, 0%, 38%)";

        switch( m_theme )
        {
            case Mid:
                window = QColor::fromHsl(0, 0, 64);
                text = QColor::fromHsl(0, 0, 255);
                disabled = QColor::fromHsl(0, 0, 160, 192);
                break;
            case Dark:
                window = QColor::fromHsl(0, 0, 48);
                text = QColor::fromHsl(0, 0, 248);
                disabled = QColor::fromHsl(0, 0, 144, 192);
                borderColor = "hsla(0, 0%, 0%, 50%)";
                break;
            case Darker:
                window = QColor::fromHsl(0, 0, 32);
                text = QColor::fromHsl(0, 0, 240);
                disabled = QColor::fromHsl(0, 0, 128, 192);
                borderColor = "hsla(0, 0%, 0%, 76%)";
                break;
        }

        base = window;
        alternateBase = QColor::fromHsl(0, 0, 255, 16);
        highlight = QColor::fromHsl(210, 255, 144, 255-32);

        styleSheet += QString(R"(
            QMainWindow::separator {
                background: %1;
            }
            QMenu::separator {
                background: hsla(0, 0%, 100%, 15%);
            }
            #menuBar, #mainToolBar {
                border-bottom-color: %1;
            }
            QListView, QTreeView, QTableView {
                border-color: %1;
            }
            #listViewSession, #tableViewSession, #horizontalSliderPositionLayout, #scrollArea, QStatusBar {
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
            QSlider::handle:horizontal {
                background: qlineargradient(spread:pad, x0:0, y0:0, x2:0, y2:1, stop:0 hsl(0, 0%, 100%), stop:1 hsl(0, 0%, 85%));
                border-color: hsla(0, 0%, 0%, 76%);
                /*margin: -3px 0;*/
            }
            #horizontalSliderTemperature::sub-page:horizontal, #horizontalSliderTemperature::add-page:horizontal, #horizontalSliderTint::sub-page:horizontal, #horizontalSliderTint::add-page:horizontal {
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
    }

    double lightness = 0.5 - (0.5 * text.lightness() / 255);
    QColor link = QColor::fromHsl( highlight.hslHue(), highlight.hslSaturation(), (1.0 - lightness) * highlight.lightness() + lightness * text.lightness() );

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

    qApp->setPalette( palette );

    MainWindow->setStyleSheet( "" );
    qApp->setStyleSheet( styleSheet );
}

AppTheme::Theme AppTheme::currentTheme( void )
{
    return m_theme;
}

QPixmap AppTheme::tintPixmap( const QPixmap &pixmap, QColor color, qreal pixelRatio )
{
    QPixmap tinted( pixmap.size() );
    tinted.setDevicePixelRatio( pixelRatio );
    tinted.fill( Qt::transparent );

    QPainter painter( &tinted );
    painter.drawPixmap( 0, 0, pixmap );
    painter.setCompositionMode( QPainter::CompositionMode_SourceIn );

    if( !color.isValid() ) color = QApplication::palette().color( QPalette::Text );

    painter.fillRect( tinted.rect(), color );
    painter.end();

    return tinted;
}

void IconRecolorStyle::drawPrimitive( PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget ) const
{
    //qDebug() << option->state << element << widget;

    if( !widget->inherits( "QMenu" ) )
    {
        QProxyStyle::drawPrimitive( element, option, painter, widget );
        return;
    }

    QPalette palette = QApplication::palette();

    QColor enabled = m_state & State_Selected ? palette.color( QPalette::HighlightedText ) : palette.color( QPalette::Text );
    QColor color = option->state & State_Enabled ? enabled : palette.color( QPalette::Disabled, QPalette::Text );

    QRect r = option->rect;

    if( element == PE_IndicatorCheckBox )
    {
        if( !(option->state & State_On) ) return;

        int size = r.height();

        QIcon icon( ":/fontawesome/fontawesome/svgs-full/solid/check.svg" );
        QRect iconRect( r.center().x() - size / 2 + 1, r.center().y() - size / 2 + 1, size, size );

        QPixmap tinted = AppTheme::tintPixmap( icon.pixmap( iconRect.size() ), color, painter->device()->devicePixelRatio() );
        painter->drawPixmap( iconRect.topLeft(), tinted );
    }
    else if( element == PE_IndicatorArrowRight )
    {
        int size = r.height() + 2;

        QIcon icon( ":/fontawesome/fontawesome/svgs-full/solid/chevron-right.svg" );
        QRect iconRect( r.center().x() - size / 2 - 3, r.center().y() - size / 2 + 1, size, size );

        QPixmap tinted = AppTheme::tintPixmap( icon.pixmap( iconRect.size() ), color, painter->device()->devicePixelRatio() );
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
        if( element == CE_RubberBand ) QProxyStyle::drawControl( element, option, painter, widget );
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
    enabled.setAlpha( enabled.alpha() - 32 );
    QColor disabled = enabled;
    disabled.setAlpha( disabled.alpha() - 128 );

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
                QPixmap tinted = AppTheme::tintPixmap( pixmap, color, painter->device()->devicePixelRatio() );

                if( btn->text().isEmpty() )
                {
                    QProxyStyle::drawItemPixmap( painter, option->rect, Qt::AlignCenter, tinted );
                    return;
                }

                int iconSize = btn->iconSize().width();
                int spacing = 4;

                QFontMetrics fm( painter->font() );
                int textWidth = fm.horizontalAdvance( btn->text() );
                int totalWidth = iconSize + spacing + textWidth;
                int startX = btn->rect().center().x() - totalWidth / 2;

                QRect iconRect( startX, btn->rect().center().y() - iconSize / 2 + 1, iconSize, iconSize );
                painter->drawPixmap( iconRect.topLeft(), tinted );

                QRect textRect( startX + iconSize + spacing, btn->rect().top(), textWidth, btn->rect().height() );
                painter->drawText( textRect, Qt::AlignVCenter | Qt::AlignLeft, btn->text() );

                return;
            }
        }

        m_recolor = color;
    }
    else if( element == CE_MenuItem )
    {
        auto mi = qstyleoption_cast<const QStyleOptionMenuItem *>( option );

        if ( !mi ) return;

        // Draw only background (no text, no icon)
        QStyleOptionMenuItem opt = *mi;
        opt.icon = QIcon();
        opt.text = QString();

        // Don't draw checkmark if icon exist
        if( !mi->icon.isNull() ) opt.checkType = QStyleOptionMenuItem::NotCheckable;

        QProxyStyle::drawControl( element, &opt, painter, widget );

        int iconSize = pixelMetric( PM_SmallIconSize, option, widget );
        int spacing = 7;

        QColor iconEnabled = mi->state & State_Selected ? palette.color( QPalette::HighlightedText ) : enabled;
        QColor iconColor = mode == QIcon::Disabled ? disabled : iconEnabled;

        QColor textEnabled = mi->state & State_Selected ? palette.color( QPalette::HighlightedText ) : palette.color( QPalette::Text );
        QColor textColor = mi->state & State_Enabled ? textEnabled : palette.color( QPalette::Disabled, QPalette::Text );

        if( !mi->icon.isNull() )
        {
            QIcon::State state = mi->checked ? QIcon::On : QIcon::Off;

            QPixmap pixmap = mi->icon.pixmap( iconSize, iconSize, mode, state );
            QPixmap tinted = AppTheme::tintPixmap( pixmap, iconColor, painter->device()->devicePixelRatio() );

            QRect iconRect( mi->rect.left() + spacing, mi->rect.center().y() - iconSize / 2 + 1, iconSize, iconSize );
            painter->drawPixmap( iconRect, tinted );
        }

        QRect textRect = mi->rect.adjusted( spacing, 0, 0, 0 );

        if( !mi->icon.isNull() || widget->inherits( "QMenu" ) )
        {
            textRect = mi->rect.adjusted( iconSize + spacing * 2 - 2, 0, 0, 0 );
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
    if( m_recolor.isValid() ) tinted = AppTheme::tintPixmap( pixmap, m_recolor, painter->device()->devicePixelRatio() );
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
    QRect textRect = style()->subControlRect( QStyle::CC_GroupBox, &opt, QStyle::SC_GroupBoxLabel, this );

    int iconSize = std::max(textRect.height() * 0.75, 11.0);
    int iconTop = textRect.top() + ( textRect.height() - iconSize ) / 2;
    int margin = 3;

    QString svgPath = ":/fontawesome/fontawesome/svgs-full/solid/chevron-";
    svgPath += isChecked() ? "down.svg" : "right.svg";

    QIcon icon( svgPath );

    QColor color = palette().color( QPalette::Text );
    color.setAlpha( color.alpha() - 96 ) ;

    QRect iconRect( textRect.left() - iconSize - margin, iconTop, iconSize, iconSize );

    QPixmap tinted = AppTheme::tintPixmap( icon.pixmap( iconRect.size() ), color, painter->device()->devicePixelRatio() );
    painter->drawPixmap( iconRect.topLeft(), tinted );
}
