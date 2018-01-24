/******************************************************************************
 * QSkinny - Copyright (C) 2016 Uwe Rathmann
 * This file may be used under the terms of the QSkinny License, Version 1.0
 *****************************************************************************/

#include "QskPopup.h"
#include "QskAspect.h"
#include <QQuickWindow>
#include <QtMath>

QSK_QT_PRIVATE_BEGIN
#include <private/qquickitem_p.h>
#include <private/qquickwindow_p.h>
QSK_QT_PRIVATE_END

QSK_SUBCONTROL( QskPopup, Overlay )

static void qskSetFocus( QQuickItem* item, bool on )
{
    if ( item->window() == nullptr )
        return;

    /*
        For unknown reasons Qt::PopupFocusReason is blocked inside of
        QQuickItem::setFocus. So let's bypass it calling
        QQuickWindowPrivate::setFocusInScope/clearFocusInScope directly,
     */

    if ( const auto scope = qskNearestFocusScope( item ) )
    {
        auto dw = QQuickWindowPrivate::get( item->window() );

        if ( on )
            dw->setFocusInScope( scope, item, Qt::PopupFocusReason );
        else
            dw->clearFocusInScope( scope, item, Qt::PopupFocusReason );
    }
}

namespace
{
    class InputGrabber final : public QQuickItem
    {
        using Inherited = QQuickItem;

    public:
        InputGrabber( QskPopup* popup ):
            Inherited( popup )
        {
            setObjectName( QStringLiteral( "QskPopupInputGrabber" ) );

            setAcceptedMouseButtons( Qt::AllButtons );
            setAcceptHoverEvents( true );
            QQuickItemPrivate::get( this )->setTransparentForPositioner( true );

            setFlag( QQuickItem::ItemHasContents, false );

            /*
               The grabber has to be adjusted to the geometry of
               the layoutItem, but being a child of he popup its
               coordinate system is relative to it and needs to be adjusted
               when the position of the popup changes.
             */

            auto method = &InputGrabber::updateGeometry;
            connect( popup, &QQuickItem::xChanged, this, method );
            connect( popup, &QQuickItem::yChanged, this, method );

            resetGeometryConnection();
            updateGeometry();
        }

        void resetGeometryConnection()
        {
            // maybe using QQuickItemChangeListener instead ???

            for ( int i = 0; i < 4; i++ )
                disconnect( m_connections[i] );

            if ( QQuickItem* item = parentItem()->parentItem() )
            {
                auto method = &InputGrabber::updateGeometry;

                m_connections[0] = connect( item, &QQuickItem::xChanged, this, method );
                m_connections[1] = connect( item, &QQuickItem::yChanged, this, method );
                m_connections[2] = connect( item, &QQuickItem::widthChanged, this, method );
                m_connections[3] = connect( item, &QQuickItem::heightChanged, this, method );
            }
        }

    protected:
        virtual bool event( QEvent* event ) override final
        {
            bool doSwallow = false;

            switch ( event->type() )
            {
                case QEvent::KeyPress:
                case QEvent::KeyRelease:
                {
                    doSwallow = true;
                    break;
                }
                case QEvent::MouseButtonPress:
                case QEvent::MouseButtonRelease:
                case QEvent::MouseMove:
                case QEvent::Wheel:
                case QEvent::HoverEnter:
                case QEvent::HoverLeave:
                {
                    // swallow all events of the parent of the popup
                    // but not being seen by the popup itself

                    QPointF pos;
                    if ( event->type() == QEvent::Wheel )
                    {
                        pos = static_cast< QWheelEvent* >( event )->pos();
                    }
                    else if ( ( event->type() == QEvent::HoverEnter ) ||
                        ( event->type() == QEvent::HoverLeave ) )
                    {
                        pos = static_cast< QHoverEvent* >( event )->posF();
                    }
                    else
                    {
                        pos = static_cast< QMouseEvent* >( event )->localPos();
                    }

                    QQuickItem* popup = parentItem();
                    doSwallow = !popup->boundingRect().contains(
                        popup->mapFromItem( this, pos ) );

                    break;
                }

                default:
                    ;
            }

            if ( doSwallow )
            {
                event->accept();
                return true;
            }

            return Inherited::event( event );
        }

    private:
        void updateGeometry()
        {
            if ( QskPopup* popup = qobject_cast< QskPopup* >( parentItem() ) )
            {
                const QRectF r = popup->overlayRect();
                if ( r != boundingRect() )
                {
                    setPosition( r.topLeft() );
                    setSize( r.size() );

                    // the overlay needs to be repainted
                    popup->update();
                }
            }
        }

        QMetaObject::Connection m_connections[4];
    };
}

class QskPopup::PrivateData
{
public:
    PrivateData():
        inputGrabber( nullptr ),
        isModal( false ),
        isOpen( false ),
        autoGrabFocus( true ),
        handoverFocus( true )
    {
    }

    InputGrabber* inputGrabber;

    bool isModal : 1;
    bool isOpen : 1;
    bool autoGrabFocus : 1;
    bool handoverFocus : 1;
};

QskPopup::QskPopup( QQuickItem* parent ):
    Inherited( parent ),
    m_data( new PrivateData() )
{
    setAcceptedMouseButtons( Qt::AllButtons );

    // we don't want to be resized by layout code
    setTransparentForPositioner( true );

    setFlag( ItemIsFocusScope, true );
    setTabFence( true );
    setFocusPolicy( Qt::StrongFocus );
}

QskPopup::~QskPopup()
{
}

QRectF QskPopup::overlayRect() const
{
    const auto item = parentItem();

    if ( item && isVisible() && m_data->isModal && hasOverlay() )
    {
        const QPointF pos = mapFromItem( item, QPointF() );
        return QRectF( pos, QSizeF( item->width(), item->height() ) );
    }

    return QRectF();
}

void QskPopup::updateInputGrabber()
{
    if ( parentItem() && isVisible() && m_data->isModal )
    {
        if ( m_data->inputGrabber == nullptr )
            m_data->inputGrabber = new InputGrabber( this );
    }
    else
    {
        delete m_data->inputGrabber;
        m_data->inputGrabber = nullptr;
    }
}

void QskPopup::setModal( bool on )
{
    if ( on == m_data->isModal )
        return;

    m_data->isModal = on;
    updateInputGrabber();

    Q_EMIT modalChanged();
}

bool QskPopup::isModal() const
{
    return m_data->isModal;
}

void QskPopup::setOverlay( bool on )
{
    if ( hasOverlay() != on )
    {
        const auto subControl = effectiveSubcontrol( QskPopup::Overlay );
        setFlagHint( subControl | QskAspect::Style, on );

        update();
        Q_EMIT overlayChanged();
    }
}

bool QskPopup::hasOverlay() const
{
    return flagHint< bool >( QskPopup::Overlay | QskAspect::Style, true );
}

void QskPopup::grabFocus( bool on )
{
    if ( on == hasFocus() )
        return;

    if ( on )
    {
        qskSetFocus( this, true );
    }
    else
    {
        QQuickItem* successor = nullptr;

        if ( m_data->handoverFocus )
        {
            /*
                Qt/Quick does not handover the focus to another item,
                when the active focus gets lost. For the situation of
                a popup being closed we try to do it.
             */
            successor = focusSuccessor();
        }

        if ( successor )
            qskSetFocus( successor, true );

        if ( hasFocus() )
            qskSetFocus( this, false );
    }
}

bool QskPopup::event( QEvent* event )
{
    bool ok = Inherited::event( event );

    switch( event->type() )
    {
        case QEvent::Wheel:
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        {
            // swallow the event
            event->accept();
            break;
        }
        default:
        {
            break;
        }
    }

    return ok;
}

QQuickItem* QskPopup::focusSuccessor() const
{
    if ( const auto scope = qskNearestFocusScope( this ) )
    {
        const auto children = qskPaintOrderChildItems( scope );
        for ( auto it = children.crbegin(); it != children.crend(); ++it)
        {
            auto child = *it;

            if ( ( child != this ) && child->isFocusScope()
                && child->activeFocusOnTab() && child->isVisible() )
            {
                return child;
            }
        }
    }

    return nullptr;
}

void QskPopup::updateLayout()
{
    if ( !m_data->isOpen )
    {
        if ( m_data->autoGrabFocus )
            grabFocus( true );

        m_data->isOpen = true;
    }

    Inherited::updateLayout();
}

void QskPopup::itemChange( QQuickItem::ItemChange change,
    const QQuickItem::ItemChangeData& value )
{
    Inherited::itemChange( change, value );

    switch ( change )
    {
        case QQuickItem::ItemVisibleHasChanged:
        {
            updateInputGrabber();
            if ( !value.boolValue )
            {
                m_data->isOpen = false;
                grabFocus( false );
            }

            break;
        }
        case QQuickItem::ItemParentHasChanged:
        {
            delete m_data->inputGrabber;
            m_data->inputGrabber = nullptr;

            updateInputGrabber();

            break;
        }

        default:
            ;
    }
}

#include "moc_QskPopup.cpp"
