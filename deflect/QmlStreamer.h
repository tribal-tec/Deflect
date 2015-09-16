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

#ifndef QMLSTREAMER_H
#define QMLSTREAMER_H

#include <QTimer>
#include <QWindow>

#include <deflect/Stream.h>

QT_FORWARD_DECLARE_CLASS(QOpenGLContext)
QT_FORWARD_DECLARE_CLASS(QOpenGLFramebufferObject)
QT_FORWARD_DECLARE_CLASS(QOffscreenSurface)
QT_FORWARD_DECLARE_CLASS(QQuickRenderControl)
QT_FORWARD_DECLARE_CLASS(QQuickWindow)
QT_FORWARD_DECLARE_CLASS(QQmlEngine)
QT_FORWARD_DECLARE_CLASS(QQmlComponent)
QT_FORWARD_DECLARE_CLASS(QQuickItem)

class EventHandler;

/** Based on http://doc.qt.io/qt-5/qtquick-rendercontrol-example.html
 *
 * This class renders the given QML file in an offscreen fashion and streams
 * on each update on the given Deflect stream. It automatically register also
 * for Deflect events, which can be directly handled in the QML.
 */
class QmlStreamer : public QWindow
{
    Q_OBJECT

public:
    /**
     * Construct a new qml streamer by loading the QML, accessible by
     * getRootItem() and sets up the Deflect stream.
     *
     * @param qmlFile URL to QML file to load
     * @param streamName name of the Deflect stream
     * @param streamHost hostname of the Deflect server
     * @param size of the streamer
     */
    QmlStreamer( const QString& qmlFile, const std::string& streamName,
                 const std::string& streamHost, const QSize& size );

    ~QmlStreamer();

    /**
     * Convenience function to instantiate the QmlStreamer by setting up the
     * QApplication and enter its event loop. Does only return if application
     * is finished, e.g. the stream is closed.
     *
     * @param argc number of commandline arguments
     * @param argv array of commandline arguments
     * @param qmlFile URL to QML file to load
     * @param streamName name of the Deflect stream
     * @param streamHost hostname of the Deflect server
     * @param size of the streamer
     * @return result of qApp->exec()
     */
    static int run( int argc, char** argv, const QString& qmlFile,
                    const std::string& streamName,
                    const std::string& streamHost, const QSize& size );
\
    /** @return the QML root item, might be nullptr if not ready yet. */
    QQuickItem* getRootItem() { return _rootItem; }

protected:
    void resizeEvent( QResizeEvent* e ) final;
    void mousePressEvent( QMouseEvent* e ) final;
    void mouseReleaseEvent( QMouseEvent* e ) final;

private slots:
    void _setupRootItem();

    void _createFbo();
    void _destroyFbo();
    void _render();
    void _requestUpdate();

    void _onPressed( double, double );
    void _onReleased( double, double );
    void _onMoved( double, double );
    void _onResized( double, double );

private:
    bool _setupDeflectStream();
    void _updateSizes();

    QOpenGLContext* _context;
    QOffscreenSurface* _offscreenSurface;
    QQuickRenderControl* _renderControl;
    QQuickWindow* _quickWindow;
    QQmlEngine* _qmlEngine;
    QQmlComponent* _qmlComponent;
    QQuickItem* _rootItem;
    QOpenGLFramebufferObject* _fbo;
    QTimer _updateTimer;

    deflect::Stream _stream;
    EventHandler* _eventHandler;
    bool _streaming;
};

#endif
