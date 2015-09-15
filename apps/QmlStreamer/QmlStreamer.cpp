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
    if( !_setupDeflectStream( ))
        throw std::runtime_error( "Failed to setup Deflect stream" );

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
    else
        _setupRootItem();
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

int QmlStreamer::run( int argc, char** argv, const QString& qmlFile,
                      const std::string& streamName,
                      const std::string& streamHost, const QSize& size )
{
    QGuiApplication app( argc,argv );
    app.setQuitOnLastWindowClosed( true );

    QScopedPointer< QmlStreamer > streamer(
                new QmlStreamer( qmlFile, streamName, streamHost, size ));
    return app.exec();
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
    deflect::ImageWrapper imageWrapper( image.constBits(), image.width(),
                                        image.height(), deflect::BGRA, 0, 0 );
    imageWrapper.compressionPolicy = deflect::COMPRESSION_ON;
    imageWrapper.compressionQuality = 100;
    _streaming = _stream.send( imageWrapper ) && _stream.finishFrame();
}

void QmlStreamer::_requestUpdate()
{
    if( !_updateTimer.isActive( ))
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

//void Window::onButtonPressed(int buttonid)
//{
//    std::vector< uint8_t > transferFunction;
//    switch( buttonid )
//    {
//    case 0:
//        transferFunction = { 0, 0, 0, 0, 0, 255, 255, 0, 0, 255, 255, 1, 0, 255, 255, 2, 0, 255, 255, 3, 0, 255, 255, 3, 0, 255, 255, 4, 0, 255, 255, 5, 0, 255, 255, 6, 0, 255, 255, 6, 0, 255, 255, 7, 0, 255, 255, 8, 0, 255, 255, 9, 0, 255, 255, 10, 0, 255, 255, 10, 0, 255, 255, 11, 0, 255, 255, 12, 0, 255, 255, 13, 0, 255, 255, 13, 0, 255, 255, 14, 0, 255, 255, 15, 0, 255, 255, 16, 0, 255, 255, 17, 0, 255, 255, 17, 0, 255, 255, 18, 0, 255, 255, 19, 0, 255, 255, 20, 0, 255, 255, 20, 0, 255, 255, 21, 0, 255, 255, 22, 0, 255, 255, 23, 0, 255, 255, 24, 0, 255, 255, 24, 0, 255, 255, 25, 0, 255, 255, 26, 0, 255, 255, 27, 0, 255, 255, 27, 0, 255, 255, 28, 0, 255, 255, 29, 0, 255, 255, 30, 0, 255, 255, 31, 0, 255, 255, 31, 0, 255, 255, 32, 0, 255, 255, 33, 0, 255, 255, 34, 0, 255, 255, 34, 0, 255, 255, 35, 0, 255, 255, 36, 0, 255, 255, 37, 0, 255, 255, 38, 0, 255, 255, 38, 0, 255, 255, 39, 0, 255, 255, 40, 0, 255, 255, 41, 0, 255, 255, 41, 0, 255, 255, 42, 0, 255, 255, 43, 0, 255, 255, 44, 0, 255, 255, 45, 0, 255, 255, 45, 0, 255, 255, 46, 0, 255, 255, 47, 0, 255, 255, 48, 0, 255, 255, 48, 0, 255, 255, 49, 0, 255, 255, 50, 0, 255, 255, 51, 0, 255, 255, 52, 0, 255, 255, 52, 0, 255, 255, 53, 0, 255, 255, 54, 0, 255, 255, 55, 0, 255, 255, 55, 0, 255, 255, 56, 0, 255, 255, 57, 0, 255, 255, 58, 0, 255, 255, 59, 0, 255, 255, 59, 0, 255, 255, 60, 0, 255, 255, 61, 0, 255, 255, 62, 0, 255, 255, 62, 0, 255, 255, 63, 0, 255, 255, 64, 0, 255, 255, 65, 0, 255, 255, 66, 0, 255, 255, 66, 0, 255, 255, 67, 0, 255, 255, 68, 0, 255, 255, 69, 0, 255, 255, 69, 0, 255, 255, 70, 0, 255, 255, 71, 0, 255, 255, 72, 0, 255, 255, 73, 0, 255, 255, 73, 0, 255, 255, 74, 0, 255, 255, 75, 0, 255, 255, 76, 0, 255, 255, 76, 0, 255, 255, 77, 0, 255, 255, 78, 0, 255, 255, 79, 0, 255, 255, 80, 0, 255, 255, 80, 0, 255, 255, 81, 0, 255, 255, 82, 0, 255, 255, 83, 0, 255, 255, 83, 0, 255, 255, 84, 0, 255, 255, 85, 0, 255, 255, 86, 0, 255, 255, 87, 0, 255, 255, 87, 0, 255, 255, 88, 0, 255, 255, 89, 0, 255, 255, 90, 0, 255, 255, 90, 0, 255, 255, 91, 0, 255, 255, 92, 0, 255, 255, 93, 0, 255, 255, 94, 0, 255, 255, 94, 0, 255, 255, 95, 0, 255, 255, 96, 0, 255, 255, 97, 0, 255, 255, 97, 0, 255, 255, 98, 0, 255, 255, 99, 3, 251, 255, 98, 5, 249, 255, 97, 7, 247, 255, 96, 9, 245, 255, 95, 11, 243, 255, 95, 13, 241, 255, 94, 15, 239, 255, 93, 17, 237, 255, 92, 19, 235, 255, 92, 21, 233, 255, 91, 23, 231, 255, 90, 25, 229, 255, 90, 27, 227, 255, 89, 29, 225, 255, 88, 31, 223, 255, 87, 33, 221, 255, 87, 35, 219, 255, 86, 37, 217, 255, 85, 39, 215, 255, 85, 41, 213, 255, 84, 43, 211, 255, 83, 45, 209, 255, 82, 47, 207, 255, 82, 49, 205, 255, 81, 51, 203, 255, 80, 53, 201, 255, 80, 55, 199, 255, 79, 57, 197, 255, 78, 59, 195, 255, 77, 61, 193, 255, 77, 63, 191, 255, 76, 65, 189, 255, 75, 67, 187, 255, 75, 69, 185, 255, 74, 71, 183, 255, 73, 73, 181, 255, 72, 75, 179, 255, 72, 77, 177, 255, 71, 79, 175, 255, 70, 81, 173, 255, 70, 83, 171, 255, 69, 85, 169, 255, 68, 87, 167, 255, 67, 89, 165, 255, 67, 91, 163, 255, 66, 93, 161, 255, 65, 95, 159, 255, 65, 97, 157, 255, 64, 99, 155, 255, 63, 101, 153, 255, 62, 103, 151, 255, 62, 105, 149, 255, 61, 107, 147, 255, 60, 109, 145, 255, 60, 111, 143, 255, 59, 113, 141, 255, 58, 115, 139, 255, 57, 117, 137, 255, 57, 119, 135, 255, 56, 121, 133, 255, 55, 123, 131, 255, 54, 125, 129, 255, 54, 127, 127, 255, 53, 129, 125, 255, 52, 131, 123, 255, 52, 133, 121, 255, 51, 135, 119, 255, 50, 137, 117, 255, 49, 139, 115, 255, 49, 141, 113, 255, 48, 143, 111, 255, 47, 145, 109, 255, 47, 147, 107, 255, 46, 149, 105, 255, 45, 151, 103, 255, 44, 153, 101, 255, 44, 155, 99, 255, 43, 157, 97, 255, 42, 159, 95, 255, 42, 161, 93, 255, 41, 163, 91, 255, 40, 165, 89, 255, 39, 167, 87, 255, 39, 169, 85, 255, 38, 171, 83, 255, 37, 173, 81, 255, 37, 175, 79, 255, 36, 177, 77, 255, 35, 179, 75, 255, 34, 181, 73, 255, 34, 183, 71, 255, 33, 185, 69, 255, 32, 187, 67, 255, 32, 189, 65, 255, 31, 191, 63, 255, 30, 193, 61, 255, 29, 195, 59, 255, 29, 197, 57, 255, 28, 199, 55, 255, 27, 201, 53, 255, 27, 203, 51, 255, 26, 205, 49, 255, 25, 207, 47, 255, 24, 209, 45, 255, 24, 211, 43, 255, 23, 213, 41, 255, 22, 215, 39, 255, 21, 217, 37, 255, 21, 219, 35, 255, 20, 221, 33, 255, 19, 223, 31, 255, 19, 225, 29, 255, 18, 227, 27, 255, 17, 229, 25, 255, 16, 231, 23, 255, 16, 233, 21, 255, 15, 235, 19, 255, 14, 237, 17, 255, 14, 239, 15, 255, 13, 241, 13, 255, 12, 243, 11, 255, 11, 245, 9, 255, 11, 247, 7, 255, 10, 249, 5, 255, 9, 251, 3, 255, 9, 253, 1, 255, 8, 255, 0, 255, 7 };
//        break;
//    case 1:
//        transferFunction = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 1, 0, 0, 255, 1, 0, 0, 255, 2, 0, 1, 255, 3, 0, 1, 255, 3, 0, 1, 255, 4, 0, 2, 255, 5, 0, 2, 255, 5, 0, 2, 255, 6, 0, 3, 255, 7, 0, 3, 255, 7, 0, 3, 255, 8, 0, 3, 255, 9, 0, 4, 255, 9, 0, 4, 255, 10, 0, 4, 255, 11, 0, 5, 255, 11, 0, 5, 255, 12, 0, 5, 255, 13, 0, 6, 255, 13, 0, 6, 255, 14, 0, 6, 255, 15, 0, 6, 255, 15, 0, 7, 255, 16, 0, 7, 255, 17, 0, 7, 255, 17, 0, 8, 255, 18, 0, 8, 255, 19, 0, 8, 255, 19, 0, 9, 255, 20, 0, 9, 255, 21, 0, 9, 255, 21, 0, 9, 255, 22, 0, 10, 255, 23, 0, 10, 255, 23, 0, 10, 255, 24, 0, 11, 255, 25, 0, 11, 255, 25, 0, 11, 255, 26, 0, 12, 255, 27, 0, 12, 255, 27, 0, 12, 255, 28, 0, 12, 255, 29, 0, 13, 255, 29, 0, 13, 255, 30, 0, 13, 255, 31, 0, 14, 255, 31, 0, 15, 255, 32, 0, 16, 255, 33, 0, 17, 255, 33, 0, 18, 255, 34, 0, 19, 255, 35, 0, 21, 255, 35, 0, 22, 255, 36, 0, 23, 255, 37, 0, 24, 255, 37, 0, 25, 255, 38, 0, 26, 255, 39, 0, 28, 255, 39, 0, 29, 255, 40, 0, 30, 255, 41, 0, 31, 255, 41, 0, 32, 255, 42, 0, 33, 255, 43, 0, 34, 255, 44, 0, 36, 255, 45, 0, 37, 255, 47, 0, 38, 255, 48, 0, 39, 255, 49, 0, 40, 255, 50, 0, 41, 255, 51, 0, 43, 255, 52, 0, 44, 255, 53, 0, 45, 255, 54, 0, 46, 255, 55, 0, 47, 255, 57, 0, 48, 255, 58, 0, 49, 255, 59, 0, 51, 255, 60, 0, 52, 255, 61, 0, 53, 255, 62, 0, 54, 255, 63, 0, 55, 255, 64, 0, 56, 255, 65, 0, 58, 255, 66, 0, 59, 255, 68, 0, 60, 255, 69, 0, 61, 255, 70, 0, 62, 255, 71, 0, 63, 255, 72, 0, 64, 255, 73, 0, 66, 255, 74, 0, 67, 255, 75, 0, 68, 255, 76, 0, 69, 255, 78, 0, 70, 255, 79, 0, 71, 255, 80, 0, 72, 255, 81, 0, 74, 255, 82, 0, 75, 255, 83, 0, 76, 255, 84, 0, 77, 255, 85, 0, 78, 255, 86, 0, 79, 255, 87, 0, 81, 255, 89, 0, 82, 255, 90, 0, 83, 255, 91, 0, 84, 255, 92, 0, 85, 255, 93, 0, 86, 255, 94, 0, 87, 255, 95, 0, 89, 255, 96, 0, 90, 255, 97, 0, 91, 255, 99, 0, 92, 255, 100, 0, 93, 255, 101, 0, 94, 255, 102, 0, 96, 255, 103, 0, 97, 255, 104, 0, 98, 255, 105, 0, 99, 255, 106, 0, 100, 255, 107, 0, 101, 255, 109, 0, 102, 255, 110, 0, 104, 255, 111, 0, 105, 255, 112, 0, 106, 255, 113, 0, 107, 255, 114, 0, 108, 255, 115, 0, 109, 255, 116, 0, 111, 255, 117, 0, 112, 255, 118, 0, 113, 255, 120, 0, 114, 255, 121, 0, 115, 255, 122, 0, 116, 255, 123, 0, 117, 255, 124, 0, 119, 255, 125, 0, 120, 255, 126, 0, 121, 255, 127, 0, 122, 255, 128, 0, 123, 255, 130, 0, 124, 255, 131, 0, 125, 255, 132, 0, 127, 255, 133, 0, 128, 255, 134, 0, 129, 255, 135, 0, 130, 255, 136, 0, 131, 255, 137, 0, 132, 255, 138, 0, 134, 255, 139, 0, 135, 255, 141, 0, 136, 255, 142, 0, 137, 255, 143, 0, 138, 255, 144, 0, 139, 255, 145, 0, 140, 255, 146, 0, 142, 255, 147, 0, 143, 255, 148, 0, 144, 255, 149, 0, 145, 255, 151, 0, 146, 255, 152, 0, 147, 255, 153, 0, 149, 255, 154, 0, 150, 255, 155, 0, 151, 255, 156, 0, 152, 255, 157, 0, 153, 255, 158, 0, 154, 255, 159, 0, 155, 255, 160, 0, 157, 255, 162, 0, 158, 255, 163, 0, 159, 255, 164, 0, 160, 255, 165, 0, 161, 255, 166, 0, 162, 255, 167, 0, 163, 255, 168, 0, 165, 255, 169, 0, 166, 255, 170, 0, 167, 255, 172, 0, 168, 255, 173, 0, 169, 255, 174, 0, 170, 255, 175, 0, 172, 255, 176, 0, 173, 255, 177, 0, 174, 255, 178, 0, 175, 255, 179, 0, 176, 255, 180, 0, 177, 255, 182, 0, 178, 255, 183, 0, 180, 255, 184, 0, 181, 255, 185, 0, 182, 255, 186, 0, 183, 255, 187, 0, 184, 255, 188, 0, 185, 255, 189, 0, 187, 255, 190, 0, 188, 255, 191, 0, 189, 255, 193, 0, 190, 255, 194, 0, 191, 255, 195, 0, 192, 255, 196, 0, 193, 255, 197, 0, 195, 255, 198, 0, 196, 255, 199, 0, 197, 255, 200, 0, 198, 255, 201, 0, 199, 255, 203, 0, 200, 255, 204, 0, 202, 255, 205, 0, 203, 255, 206, 0, 204, 255, 207, 0, 205, 255, 208, 0, 206, 255, 209, 0, 207, 255, 210, 0, 208, 255, 211, 0, 210, 255, 212, 0, 211, 255, 214, 0, 212, 255, 215, 0, 213, 255, 216, 0, 214, 255, 217, 0, 215, 255, 218, 0, 216, 255, 219, 0, 218, 255, 220, 0, 219, 255, 221, 0, 220, 255, 222, 0, 221, 255, 224, 0, 222, 255, 225, 0, 223, 255, 226, 0, 225, 255, 227, 0, 226, 255, 228, 0, 227, 255, 229, 0, 228, 255, 230, 0, 229, 255, 231, 0, 230, 255, 232, 0, 231, 255, 233, 0, 233, 255, 235, 0, 234, 255, 236, 0, 235, 255, 237, 0, 236, 255, 238, 0, 237, 255, 239, 0, 238, 255, 240, 0, 240, 255, 241, 0, 241, 255, 242, 0, 242, 255, 243, 0, 243, 255, 245, 0, 244, 255, 246, 0, 245, 255, 247, 0, 246, 255, 248, 0, 248, 255, 249, 0, 249, 255, 250, 0, 250, 255, 251, 0, 251, 255, 252, 0, 252, 255, 253, 0, 253 };
//        break;
//    case 2:
//        transferFunction = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 31, 32, 32, 32, 32, 33, 33, 33, 33, 34, 34, 34, 34, 35, 35, 35, 35, 36, 36, 36, 36, 37, 37, 37, 37, 38, 38, 38, 38, 39, 39, 39, 39, 40, 40, 40, 40, 41, 41, 41, 41, 42, 42, 42, 42, 43, 43, 43, 43, 44, 44, 44, 44, 45, 45, 45, 45, 46, 46, 46, 46, 47, 47, 47, 47, 48, 48, 48, 48, 49, 49, 49, 49, 50, 50, 50, 50, 51, 51, 51, 51, 52, 52, 52, 52, 53, 53, 53, 53, 54, 54, 54, 54, 55, 55, 55, 55, 56, 56, 56, 56, 57, 57, 57, 57, 58, 58, 58, 58, 59, 59, 59, 59, 60, 60, 60, 60, 61, 61, 61, 61, 62, 62, 62, 62, 63, 63, 63, 63, 64, 64, 64, 64, 65, 65, 65, 65, 66, 66, 66, 66, 67, 67, 67, 67, 68, 68, 68, 68, 69, 69, 69, 69, 70, 70, 70, 70, 71, 71, 71, 71, 72, 72, 72, 72, 73, 73, 73, 73, 74, 74, 74, 74, 75, 75, 75, 75, 76, 76, 76, 76, 77, 77, 77, 77, 78, 78, 78, 78, 79, 79, 79, 79, 80, 80, 80, 80, 81, 81, 81, 81, 82, 82, 82, 82, 83, 83, 83, 83, 84, 84, 84, 84, 85, 85, 85, 85, 86, 86, 86, 86, 87, 87, 87, 87, 88, 88, 88, 88, 89, 89, 89, 89, 90, 90, 90, 90, 91, 91, 91, 91, 92, 92, 92, 92, 93, 93, 93, 93, 94, 94, 94, 94, 95, 95, 95, 95, 96, 96, 96, 96, 97, 97, 97, 97, 98, 98, 98, 98, 99, 99, 99, 99, 100, 100, 100, 100, 101, 101, 101, 101, 102, 102, 102, 102, 103, 103, 103, 103, 104, 104, 104, 104, 105, 105, 105, 105, 106, 106, 106, 106, 107, 107, 107, 107, 108, 108, 108, 108, 109, 109, 109, 109, 110, 110, 110, 110, 111, 111, 111, 111, 112, 112, 112, 112, 113, 113, 113, 113, 114, 114, 114, 114, 115, 115, 115, 115, 116, 116, 116, 116, 117, 117, 117, 117, 118, 118, 118, 118, 119, 119, 119, 119, 120, 120, 120, 120, 121, 121, 121, 121, 122, 122, 122, 122, 123, 123, 123, 123, 124, 124, 124, 124, 125, 125, 125, 125, 126, 126, 126, 126, 127, 127, 127, 127, 128, 128, 128, 128, 129, 129, 129, 129, 130, 130, 130, 130, 131, 131, 131, 131, 132, 132, 132, 132, 133, 133, 133, 133, 134, 134, 134, 134, 135, 135, 135, 135, 136, 136, 136, 136, 137, 137, 137, 137, 138, 138, 138, 138, 139, 139, 139, 139, 140, 140, 140, 140, 141, 141, 141, 141, 142, 142, 142, 142, 143, 143, 143, 143, 144, 144, 144, 144, 145, 145, 145, 145, 146, 146, 146, 146, 147, 147, 147, 147, 148, 148, 148, 148, 149, 149, 149, 149, 150, 150, 150, 150, 151, 151, 151, 151, 152, 152, 152, 152, 153, 153, 153, 153, 154, 154, 154, 154, 155, 155, 155, 155, 156, 156, 156, 156, 157, 157, 157, 157, 158, 158, 158, 158, 159, 159, 159, 159, 160, 160, 160, 160, 161, 161, 161, 161, 162, 162, 162, 162, 163, 163, 163, 163, 164, 164, 164, 164, 165, 165, 165, 165, 166, 166, 166, 166, 167, 167, 167, 167, 168, 168, 168, 168, 169, 169, 169, 169, 170, 170, 170, 170, 171, 171, 171, 171, 172, 172, 172, 172, 173, 173, 173, 173, 174, 174, 174, 174, 175, 175, 175, 175, 176, 176, 176, 176, 177, 177, 177, 177, 178, 178, 178, 178, 179, 179, 179, 179, 180, 180, 180, 180, 181, 181, 181, 181, 182, 182, 182, 182, 183, 183, 183, 183, 184, 184, 184, 184, 185, 185, 185, 185, 186, 186, 186, 186, 187, 187, 187, 187, 188, 188, 188, 188, 189, 189, 189, 189, 190, 190, 190, 190, 191, 191, 191, 191, 192, 192, 192, 192, 193, 193, 193, 193, 194, 194, 194, 194, 195, 195, 195, 195, 196, 196, 196, 196, 197, 197, 197, 197, 198, 198, 198, 198, 199, 199, 199, 199, 200, 200, 200, 200, 201, 201, 201, 201, 202, 202, 202, 202, 203, 203, 203, 203, 204, 204, 204, 204, 205, 205, 205, 205, 206, 206, 206, 206, 207, 207, 207, 207, 208, 208, 208, 208, 209, 209, 209, 209, 210, 210, 210, 210, 211, 211, 211, 211, 212, 212, 212, 212, 213, 213, 213, 213, 214, 214, 214, 214, 215, 215, 215, 215, 216, 216, 216, 216, 217, 217, 217, 217, 218, 218, 218, 218, 219, 219, 219, 219, 220, 220, 220, 220, 221, 221, 221, 221, 222, 222, 222, 222, 223, 223, 223, 223, 224, 224, 224, 224, 225, 225, 225, 225, 226, 226, 226, 226, 227, 227, 227, 227, 228, 228, 228, 228, 229, 229, 229, 229, 230, 230, 230, 230, 231, 231, 231, 231, 232, 232, 232, 232, 233, 233, 233, 233, 234, 234, 234, 234, 235, 235, 235, 235, 236, 236, 236, 236, 237, 237, 237, 237, 238, 238, 238, 238, 239, 239, 239, 239, 240, 240, 240, 240, 241, 241, 241, 241, 242, 242, 242, 242, 243, 243, 243, 243, 244, 244, 244, 244, 245, 245, 245, 245, 246, 246, 246, 246, 247, 247, 247, 247, 248, 248, 248, 248, 249, 249, 249, 249, 250, 250, 250, 250, 251, 251, 251, 251, 252, 252, 252, 252, 253, 253, 253, 253, 254, 254, 254, 254 };
//        break;
//    case 3:
//    default:
//        transferFunction = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0, 8, 0, 0, 0, 9, 0, 0, 0, 10, 0, 0, 0, 11, 0, 0, 0, 12, 0, 0, 0, 13, 0, 0, 0, 14, 0, 0, 0, 15, 0, 0, 0, 16, 0, 0, 0, 17, 0, 0, 0, 18, 0, 0, 0, 19, 0, 0, 0, 20, 0, 0, 0, 21, 0, 0, 0, 22, 0, 0, 0, 23, 0, 0, 0, 24, 0, 0, 0, 25, 0, 0, 0, 26, 0, 0, 0, 27, 0, 0, 0, 28, 0, 0, 0, 29, 0, 0, 0, 30, 0, 0, 0, 31, 0, 0, 0, 32, 0, 0, 0, 33, 0, 0, 0, 34, 0, 0, 0, 35, 0, 0, 0, 36, 0, 0, 0, 37, 0, 0, 0, 38, 0, 0, 0, 39, 0, 0, 0, 40, 0, 0, 0, 41, 0, 0, 0, 42, 0, 0, 1, 43, 0, 0, 3, 44, 0, 0, 5, 45, 0, 0, 6, 46, 0, 0, 8, 47, 0, 0, 10, 48, 0, 0, 12, 49, 0, 0, 13, 50, 0, 0, 15, 51, 0, 0, 17, 52, 0, 0, 19, 53, 0, 0, 20, 54, 0, 0, 22, 55, 0, 0, 24, 56, 0, 0, 26, 57, 0, 0, 27, 58, 0, 0, 29, 59, 0, 0, 31, 60, 0, 0, 33, 61, 0, 0, 35, 62, 0, 0, 36, 63, 0, 0, 38, 64, 0, 0, 40, 65, 0, 0, 42, 66, 0, 0, 43, 67, 0, 0, 45, 68, 0, 0, 47, 69, 0, 0, 49, 70, 0, 0, 50, 71, 0, 0, 52, 72, 0, 0, 54, 73, 0, 0, 56, 74, 0, 0, 57, 75, 0, 0, 59, 76, 0, 0, 61, 77, 0, 0, 63, 78, 0, 0, 64, 79, 0, 0, 66, 80, 0, 0, 68, 81, 0, 0, 70, 82, 0, 0, 71, 83, 0, 0, 73, 84, 0, 0, 75, 85, 0, 0, 77, 86, 0, 0, 78, 87, 0, 0, 80, 88, 0, 0, 82, 89, 0, 0, 84, 90, 0, 0, 85, 91, 0, 0, 87, 92, 0, 0, 89, 93, 0, 0, 91, 94, 0, 0, 92, 95, 0, 0, 94, 96, 0, 0, 96, 97, 0, 0, 98, 98, 0, 0, 99, 99, 0, 0, 101, 100, 0, 0, 103, 101, 0, 0, 105, 102, 0, 0, 106, 103, 0, 0, 108, 104, 0, 0, 110, 105, 0, 0, 112, 106, 0, 0, 113, 107, 0, 0, 115, 108, 0, 0, 117, 109, 0, 0, 119, 110, 0, 0, 120, 111, 0, 0, 122, 112, 0, 0, 124, 113, 0, 0, 126, 114, 0, 0, 127, 115, 0, 0, 129, 116, 0, 0, 131, 117, 0, 0, 133, 118, 0, 0, 134, 119, 0, 0, 136, 120, 0, 0, 138, 121, 0, 0, 140, 122, 0, 0, 141, 123, 0, 0, 143, 124, 0, 0, 145, 125, 0, 0, 147, 126, 0, 0, 148, 127, 0, 0, 150, 128, 0, 0, 152, 129, 0, 0, 154, 130, 0, 0, 155, 131, 0, 0, 157, 132, 0, 0, 159, 133, 0, 0, 161, 134, 0, 0, 162, 135, 0, 0, 164, 136, 0, 0, 166, 137, 0, 0, 168, 138, 0, 0, 169, 139, 0, 0, 171, 140, 0, 0, 173, 141, 0, 0, 175, 142, 0, 0, 176, 143, 0, 0, 178, 144, 0, 0, 180, 145, 0, 0, 182, 146, 0, 0, 184, 147, 0, 0, 185, 148, 0, 0, 187, 149, 0, 0, 189, 150, 0, 0, 191, 151, 0, 0, 192, 152, 0, 0, 194, 153, 0, 0, 196, 154, 0, 0, 198, 155, 0, 0, 199, 156, 0, 0, 201, 157, 0, 0, 203, 158, 0, 0, 205, 159, 0, 0, 206, 160, 0, 0, 208, 161, 0, 0, 210, 162, 0, 0, 212, 163, 0, 0, 213, 164, 0, 0, 215, 165, 0, 0, 217, 166, 0, 0, 219, 167, 0, 0, 220, 168, 0, 0, 222, 169, 0, 0, 224, 170, 0, 0, 226, 171, 0, 0, 227, 172, 0, 0, 229, 173, 0, 0, 231, 174, 0, 0, 233, 175, 0, 0, 234, 176, 0, 0, 236, 177, 0, 0, 238, 178, 0, 0, 240, 179, 0, 0, 241, 180, 0, 0, 243, 181, 0, 0, 245, 182, 0, 0, 247, 183, 0, 0, 248, 184, 0, 0, 250, 185, 0, 0, 252, 186, 0, 0, 4, 187, 0, 0, 249, 188, 0, 0, 239, 189, 0, 0, 229, 190, 0, 0, 219, 191, 0, 0, 209, 192, 0, 0, 199, 193, 0, 0, 188, 194, 0, 0, 178, 195, 0, 0, 168, 196, 0, 0, 158, 197, 0, 0, 148, 198, 0, 0, 138, 199, 0, 0, 128, 200, 0, 0, 117, 201, 0, 0, 107, 202, 0, 0, 97, 203, 0, 0, 87, 204, 0, 0, 77, 205, 0, 0, 67, 206, 0, 0, 56, 207, 0, 0, 46, 208, 0, 0, 36, 209, 0, 0, 26, 210, 0, 0, 16, 211, 0, 0, 6, 212, 0, 0, 2, 213, 0, 0, 8, 214, 0, 0, 14, 215, 0, 0, 19, 216, 0, 0, 25, 217, 0, 0, 31, 218, 0, 0, 37, 219, 0, 0, 43, 220, 0, 0, 49, 221, 0, 0, 55, 222, 0, 0, 61, 223, 0, 0, 66, 224, 0, 0, 72, 225, 0, 0, 78, 226, 0, 0, 84, 227, 0, 0, 90, 228, 0, 0, 96, 229, 0, 0, 102, 230, 0, 0, 108, 231, 0, 0, 113, 232, 0, 0, 119, 233, 0, 0, 125, 234, 0, 0, 131, 235, 0, 0, 137, 236, 0, 0, 143, 237, 0, 0, 149, 238, 0, 0, 155, 239, 0, 0, 160, 240, 0, 0, 166, 241, 0, 0, 172, 242, 0, 0, 178, 243, 0, 0, 184, 244, 0, 0, 190, 245, 0, 0, 196, 246, 0, 0, 202, 247, 0, 0, 207, 248, 0, 0, 213, 249, 0, 0, 219, 250, 0, 0, 225, 251, 0, 0, 231, 252, 0, 0, 237, 253, 0, 0, 243, 254, 0, 0, 249 };
//        break;
//    }

//    _publisher.publish( zeq::hbp::serializeLookupTable1D( transferFunction ) );
//}

void QmlStreamer::_setupRootItem()
{
    disconnect( _qmlComponent, &QQmlComponent::statusChanged,
                this, &QmlStreamer::_setupRootItem );

    if( _qmlComponent->isError( ))
    {
        QList< QQmlError > errorList = _qmlComponent->errors();
        foreach( const QQmlError &error, errorList )
            qWarning() << error.url() << error.line() << error;
        return;
    }

    QObject* rootObject = _qmlComponent->create();
    if( _qmlComponent->isError( ))
    {
        QList< QQmlError > errorList = _qmlComponent->errors();
        foreach( const QQmlError &error, errorList )
            qWarning() << error.url() << error.line() << error;
        return;
    }

    _rootItem = qobject_cast< QQuickItem* >( rootObject );
    if( !_rootItem )
    {
        qWarning( "run: Not a QQuickItem" );
        delete rootObject;
        return;
    }

    // The root item is ready. Associate it with the window.
    _rootItem->setParentItem( _quickWindow->contentItem( ));

   // connect( _rootItem, SIGNAL(pressed(int)), this, SLOT(onButtonPressed(int)));

    // Update item and rendering related geometries.
    _updateSizes();

    // Initialize the render control and our OpenGL resources.
    _context->makeCurrent( _offscreenSurface );
    _renderControl->initialize( _context );
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
