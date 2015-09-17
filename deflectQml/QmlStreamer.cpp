/*********************************************************************/
/* Copyright (c) 2015, EPFL/Blue Brain Project                       */
/*                     Daniel.Nachbaur <daniel.nachbaur@epfl.ch>     */
/* All rights reserved.                                              */
/*                                                                   */
/* Redistribution and use in source and binary forms, with or        */
/* without modification, are permitted provided that the following   */
/* conditions are met:                                               */
/*                                                                   */
/*   1. Redistributions of source code must retain the above         */
/*      copyright notice, this list of conditions and the following  */
/*      disclaimer.                                                  */
/*                                                                   */
/*   2. Redistributions in binary form must reproduce the above      */
/*      copyright notice, this list of conditions and the following  */
/*      disclaimer in the documentation and/or other materials       */
/*      provided with the distribution.                              */
/*                                                                   */
/*    THIS  SOFTWARE IS PROVIDED  BY THE  UNIVERSITY OF  TEXAS AT    */
/*    AUSTIN  ``AS IS''  AND ANY  EXPRESS OR  IMPLIED WARRANTIES,    */
/*    INCLUDING, BUT  NOT LIMITED  TO, THE IMPLIED  WARRANTIES OF    */
/*    MERCHANTABILITY  AND FITNESS FOR  A PARTICULAR  PURPOSE ARE    */
/*    DISCLAIMED.  IN  NO EVENT SHALL THE UNIVERSITY  OF TEXAS AT    */
/*    AUSTIN OR CONTRIBUTORS BE  LIABLE FOR ANY DIRECT, INDIRECT,    */
/*    INCIDENTAL,  SPECIAL, EXEMPLARY,  OR  CONSEQUENTIAL DAMAGES    */
/*    (INCLUDING, BUT  NOT LIMITED TO,  PROCUREMENT OF SUBSTITUTE    */
/*    GOODS  OR  SERVICES; LOSS  OF  USE,  DATA,  OR PROFITS;  OR    */
/*    BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON ANY THEORY OF    */
/*    LIABILITY, WHETHER  IN CONTRACT, STRICT  LIABILITY, OR TORT    */
/*    (INCLUDING NEGLIGENCE OR OTHERWISE)  ARISING IN ANY WAY OUT    */
/*    OF  THE  USE OF  THIS  SOFTWARE,  EVEN  IF ADVISED  OF  THE    */
/*    POSSIBILITY OF SUCH DAMAGE.                                    */
/*                                                                   */
/* The views and conclusions contained in the software and           */
/* documentation are those of the authors and should not be          */
/* interpreted as representing official policies, either expressed   */
/* or implied, of The University of Texas at Austin.                 */
/*********************************************************************/

#include "QmlStreamer.h"
#include "EventHandler.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>


class RenderControl : public QQuickRenderControl
{
public:
    RenderControl( QWindow* w )
        : _window( w )
    {}

    QWindow* renderWindow( QPoint* offset ) final
    {
        if( offset )
            *offset = QPoint( 0, 0 );
        return _window;
    }

private:
    QWindow* _window;
};

namespace deflect
{

QmlStreamer::QmlStreamer( const QString& qmlFile, const std::string& streamName,
                          const std::string& streamHost, const QSize& size_ )
    : QWindow()
    , _context( new QOpenGLContext )
    , _offscreenSurface( new QOffscreenSurface )
    , _renderControl( new RenderControl( this ))
    // Create a QQuickWindow that is associated with out render control. Note
    // that this window never gets created or shown, meaning that it will never
    // get an underlying native (platform) window.
    , _quickWindow( new QQuickWindow( _renderControl ))
    , _qmlEngine( new QQmlEngine )
    , _qmlComponent( new QQmlComponent(_qmlEngine, QUrl(qmlFile)))
    , _rootItem( nullptr )
    , _fbo( nullptr )
    , _stream( streamName, streamHost )
    , _eventHandler( nullptr )
    , _streaming( true )
{
    setSurfaceType( QSurface::OpenGLSurface );

    // Qt Quick may need a depth and stencil buffer
    QSurfaceFormat format_;
    format_.setDepthBufferSize( 16 );
    format_.setStencilBufferSize( 8 );
    setFormat( format_ );

    _context->setFormat( format_ );
    _context->create();

    // Pass m_context->format(), not format. Format does not specify and color
    // buffer sizes, while the context, that has just been created, reports a
    // format that has these values filled in. Pass this to the offscreen
    // surface to make sure it will be compatible with the context's
    // configuration.
    _offscreenSurface->setFormat( _context->format( ));
    _offscreenSurface->create();

    if( !_qmlEngine->incubationController( ))
        _qmlEngine->setIncubationController( _quickWindow->incubationController( ));

    // When Quick says there is a need to render, we will not render
    // immediately. Instead, a timer with a small interval is used to get better
    // performance.
    _updateTimer.setSingleShot( true );
    _updateTimer.setInterval( 5 );
    connect( &_updateTimer, &QTimer::timeout, this, &QmlStreamer::_render );

    // Now hook up the signals. For simplicy we don't differentiate between
    // renderRequested (only render is needed, no sync) and sceneChanged (polish
    // and sync is needed too).
    connect( _quickWindow, &QQuickWindow::sceneGraphInitialized,
             this, &QmlStreamer::_createFbo );
    connect( _quickWindow, &QQuickWindow::sceneGraphInvalidated,
             this, &QmlStreamer::_destroyFbo );
    connect( _renderControl, &QQuickRenderControl::renderRequested,
             this, &QmlStreamer::_requestUpdate );
    connect( _renderControl, &QQuickRenderControl::sceneChanged,
             this, &QmlStreamer::_requestUpdate );

    // need to resize/realize to fix FBO creation, otherwise:
    // QOpenGLFramebufferObject: Framebuffer incomplete attachment.
    resize( size_ );

    // remote URL to QML components are loaded asynchronously
    if( _qmlComponent->isLoading( ))
        connect( _qmlComponent, &QQmlComponent::statusChanged,
                 this, &QmlStreamer::_setupRootItem );
    else if( !_setupRootItem( ))
        throw std::runtime_error( "Failed to setup/load QML" );

    if( !_setupDeflectStream( ))
        throw std::runtime_error( "Failed to setup Deflect stream" );
}

QmlStreamer::~QmlStreamer()
{
    delete _eventHandler;

    _context->makeCurrent( _offscreenSurface );

    // delete first to free scenegraph resources for following destructions
    delete _renderControl;

    delete _qmlComponent;
    delete _quickWindow;
    delete _qmlEngine;
    delete _fbo;

    _context->doneCurrent();

    delete _offscreenSurface;
    delete _context;
}

void QmlStreamer::_createFbo()
{
    _fbo = new QOpenGLFramebufferObject( size() * devicePixelRatio(),
                               QOpenGLFramebufferObject::CombinedDepthStencil );
    _quickWindow->setRenderTarget( _fbo );
}

void QmlStreamer::_destroyFbo()
{
    delete _fbo;
    _fbo = 0;
}

void QmlStreamer::_render()
{
    if( !_context->makeCurrent( _offscreenSurface ))
        return;

    // Polish, synchronize and render the next frame (into our fbo). In this
    // example everything happens on the same thread and therefore all three
    // steps are performed in succession from here. In a threaded setup the
    // render() call would happen on a separate thread.
    _renderControl->polishItems();
    _renderControl->sync();
    _renderControl->render();

    _quickWindow->resetOpenGLState();
    QOpenGLFramebufferObject::bindDefault();

    _context->functions()->glFlush();

    if( !_streaming )
    {
        QCoreApplication::quit();
        return;
    }

    QImage image = _fbo->toImage();
    if( image.isNull( ))
    {
        qDebug() << "Empty image not streamed";
        return;
    }

    ImageWrapper imageWrapper( image.constBits(), image.width(),
                                        image.height(), BGRA, 0, 0 );
    imageWrapper.compressionPolicy = COMPRESSION_ON;
    imageWrapper.compressionQuality = 100;
    _streaming = _stream.send( imageWrapper ) && _stream.finishFrame();
}

void QmlStreamer::_requestUpdate()
{
    _updateTimer.start();
}

void QmlStreamer::_onPressed( double x_, double y_ )
{
    QPoint point( x_ * width(), y_ * height( ));
    QMouseEvent* e = new QMouseEvent( QEvent::MouseButtonPress, point,
                                      Qt::LeftButton, Qt::LeftButton,
                                      Qt::NoModifier );
    QCoreApplication::postEvent( this, e );
}

void QmlStreamer::_onMoved( double x_, double y_ )
{
    QPoint point( x_ * width(), y_ * height( ));
    QMouseEvent* e = new QMouseEvent( QEvent::MouseMove, point, Qt::LeftButton,
                                      Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::postEvent( this, e );
}

void QmlStreamer::_onReleased( double x_, double y_ )
{
    QPoint point( x_ * width(), y_ * height( ));
    QMouseEvent* e = new QMouseEvent( QEvent::MouseButtonRelease, point,
                                      Qt::LeftButton, Qt::NoButton,
                                      Qt::NoModifier );
    QCoreApplication::postEvent( this, e );
}

void QmlStreamer::_onResized( double x_, double y_ )
{
    QResizeEvent* resizeEvent_ = new QResizeEvent( QSize( x_, y_ ), size( ));
    QCoreApplication::postEvent( this, resizeEvent_ );
}

bool QmlStreamer::_setupRootItem()
{
    disconnect( _qmlComponent, &QQmlComponent::statusChanged,
                this, &QmlStreamer::_setupRootItem );

    if( _qmlComponent->isError( ))
    {
        QList< QQmlError > errorList = _qmlComponent->errors();
        foreach( const QQmlError &error, errorList )
            qWarning() << error.url() << error.line() << error;
        return false;
    }

    QObject* rootObject = _qmlComponent->create();
    if( _qmlComponent->isError( ))
    {
        QList< QQmlError > errorList = _qmlComponent->errors();
        foreach( const QQmlError &error, errorList )
            qWarning() << error.url() << error.line() << error;
        return false;
    }

    _rootItem = qobject_cast< QQuickItem* >( rootObject );
    if( !_rootItem )
    {
        qWarning( "run: Not a QQuickItem" );
        delete rootObject;
        return false;
    }

    // The root item is ready. Associate it with the window.
    _rootItem->setParentItem( _quickWindow->contentItem( ));

    // Update item and rendering related geometries.
    _updateSizes();

    // Initialize the render control and our OpenGL resources.
    _context->makeCurrent( _offscreenSurface );
    _renderControl->initialize( _context );

    return true;
}

bool QmlStreamer::_setupDeflectStream()
{
    if( !_stream.isConnected( ))
        return false;

    if( !_stream.registerForEvents( ))
        return false;

    _eventHandler = new EventHandler( _stream );
    connect( _eventHandler, &EventHandler::pressed,
             this, &QmlStreamer::_onPressed );
    connect( _eventHandler, &EventHandler::released,
             this, &QmlStreamer::_onReleased );
    connect( _eventHandler, &EventHandler::moved,
             this, &QmlStreamer::_onMoved );
    connect( _eventHandler, &EventHandler::resized,
             this, &QmlStreamer::_onResized );
    return true;
}

void QmlStreamer::_updateSizes()
{
    _rootItem->setWidth( width( ));
    _rootItem->setHeight( height( ));
    _quickWindow->setGeometry( 0, 0, width(), height( ));
}

void QmlStreamer::resizeEvent( QResizeEvent* e )
{
    setWidth( e->size().width( ));
    setHeight( e->size().height( ));

    if( _fbo && _fbo->size() != size() * devicePixelRatio( ) && _rootItem &&
        _context->makeCurrent( _offscreenSurface ))
    {
        _destroyFbo();
        _createFbo();
        _context->doneCurrent();
        _updateSizes();
        _render();
    }
}

void QmlStreamer::mousePressEvent( QMouseEvent* e )
{
    // Use the constructor taking localPos and screenPos. That puts localPos
    // into the event's localPos and windowPos, and screenPos into the event's
    // screenPos. This way the windowPos in e is ignored and is replaced by
    // localPos. This is necessary because QQuickWindow thinks of itself as a
    // top-level window always.
    QMouseEvent mappedEvent( e->type(), e->localPos(), e->screenPos(),
                             e->button(), e->buttons(), e->modifiers( ));
    QCoreApplication::sendEvent( _quickWindow, &mappedEvent );
}

void QmlStreamer::mouseReleaseEvent( QMouseEvent* e )
{
    QMouseEvent mappedEvent( e->type(), e->localPos(), e->screenPos(),
                             e->button(), e->buttons(), e->modifiers( ));
    QCoreApplication::sendEvent( _quickWindow, &mappedEvent );
}

}
