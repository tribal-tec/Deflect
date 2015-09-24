/*********************************************************************/
/* Copyright (c) 2011 - 2012, The University of Texas at Austin.     */
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

#include "MainWindow.h"

#include <deflect/Server.h>
#include <deflect/Stream.h>
#include <deflect/version.h>

#include <iostream>

#ifdef _WIN32
typedef __int32 int32_t;
#  include <windows.h>
#else
#  include <stdint.h>
#  include <unistd.h>
#endif

#ifdef __APPLE__
#  include <QtMac>
#  define STREAM_EVENTS_SUPPORTED TRUE
#  if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_9
#    include <CoreGraphics/CoreGraphics.h>
#  else
#    include <ApplicationServices/ApplicationServices.h>
#  endif
#endif

#define SHARE_DESKTOP_UPDATE_DELAY      1
#define SERVUS_BROWSE_DELAY           100
#define FRAME_RATE_AVERAGE_NUM_FRAMES  10

#define DEFAULT_HOST_ADDRESS  "128.178.97.206"
#define CURSOR_IMAGE_FILE     ":/cursor.png"

namespace
{
QString getUserName()
{
    QString name = qgetenv( "USER" );
    if( name.isEmpty( ))
        name = qgetenv( "USERNAME" );
    return name;
}

#ifdef __APPLE__
QString cFStringToQString( CFStringRef cfString )
{
    if( !cfString )
        return QString();

    const CFIndex length = 2 * (CFStringGetLength(cfString) + 1);
    char* buffer = new char[length];

    QString result;
    if( CFStringGetCString( cfString, buffer, length, kCFStringEncodingUTF8 ))
        result = QString::fromUtf8( buffer );
    else
        qWarning( "CFString conversion failed." );
    delete buffer;
    return result;
}

const int previewImageHeight = 100;

QPixmap getPreviewPixmap( const QPixmap& pixmap )
{
    return QPixmap::fromImage( pixmap.toImage().scaledToHeight(
                                previewImageHeight, Qt::SmoothTransformation ));
}

QPixmap getWindowPixmap( const long windowID )
{
    const CGImageRef windowImage =
            CGWindowListCreateImage( CGRectNull,
                                     kCGWindowListOptionIncludingWindow,
                                     windowID,
                                     kCGWindowImageBoundsIgnoreFraming );

    return QtMac::fromCGImageRef( windowImage );
}

QRect getWindowRect( const long windowID )
{
    long windowids[1] = {windowID};
    const CFArrayRef windowIDs = CFArrayCreate( kCFAllocatorDefault,
                                         (const void **)windowids, 1, nullptr );
    CFArrayRef windowList = CGWindowListCreateDescriptionFromArray( windowIDs );
    CFRelease( windowIDs );

    if( CFArrayGetCount( windowList ) == 0 )
        return QRect();

    const CFDictionaryRef info =
            (CFDictionaryRef)CFArrayGetValueAtIndex( windowList, 0 );
    CFDictionaryRef bounds = (CFDictionaryRef)CFDictionaryGetValue( info,
                                                              kCGWindowBounds );
    CGRect rect;
    CGRectMakeWithDictionaryRepresentation( bounds, &rect );
    CFRelease( windowList );

    return QRect( CGRectGetMinX( rect ), CGRectGetMinY( rect ),
                  CGRectGetWidth( rect ), CGRectGetHeight( rect ));
}

class DesktopWindowsModel : public QAbstractListModel
{
public:
    DesktopWindowsModel()
        : QAbstractListModel()
    {
        CFArrayRef windowList =
                CGWindowListCopyWindowInfo( kCGWindowListOptionOnScreenOnly|
                                            kCGWindowListExcludeDesktopElements,
                                            kCGNullWindowID );

        _data.push_back( std::make_tuple( "Desktop", 0,
            getPreviewPixmap( QApplication::primaryScreen()->grabWindow( 0 ))));

        for( size_t i = 0; i < size_t(CFArrayGetCount( windowList )); ++i )
        {
            const CFDictionaryRef info =
                    (CFDictionaryRef)CFArrayGetValueAtIndex( windowList, i );
            const CFStringRef cfTitle = (CFStringRef)CFDictionaryGetValue( info,
                                                                kCGWindowName );
            const CFStringRef cfApp = (CFStringRef)CFDictionaryGetValue( info,
                                                           kCGWindowOwnerName );
            const QString title = cFStringToQString( cfTitle );
            const QString app = cFStringToQString( cfApp );
            if( title.isEmpty() || app == "Window Server" || app == "Dock" )
                continue;

            CFNumberRef cfWindowID = (CFNumberRef)CFDictionaryGetValue( info,
                                                              kCGWindowNumber );
            long windowID;
            CFNumberGetValue( cfWindowID, kCFNumberLongType, &windowID );
            _data.push_back( std::make_tuple( app, windowID,
                               getPreviewPixmap( getWindowPixmap( windowID ))));
        }
        CFRelease( windowList );
    }

    int rowCount( const QModelIndex& ) const final
    {
        return int( _data.size( ));
    }

    QVariant data( const QModelIndex& index, int role ) const final
    {
        switch( role )
        {
        case Qt::DisplayRole:
            return QString("%1").arg( std::get< APPNAME >( _data[index.row()]));

        case Qt::DecorationRole:
            return std::get< WINDOWIMAGE >( _data[index.row()] );

        case Qt::UserRole:
            return getWindowPixmap( std::get< WINDOWID >( _data[index.row()] ));

        case Qt::UserRole+1:
            return getWindowRect( std::get< WINDOWID >( _data[index.row()] ));

        default:
            return QVariant();
        }
    }

private:
    enum TupleValues
    {
        APPNAME,
        WINDOWID,
        WINDOWIMAGE
    };

    std::vector< std::tuple< QString, long, QPixmap > > _data;
};

#endif

} // namespace

MainWindow::MainWindow()
    : _stream( 0 )
#ifdef DEFLECT_USE_SERVUS
    , _servus( deflect::Server::serviceName )
#endif
{
    _generateCursorImage();
    _setupUI();
}

void MainWindow::_generateCursorImage()
{
    _cursor = QImage( CURSOR_IMAGE_FILE ).scaled( 20, 20, Qt::KeepAspectRatio );
}

void MainWindow::_setupUI()
{
    QWidget* widget = new QWidget();
    QFormLayout* formLayout = new QFormLayout();

    setCentralWidget( widget );

    _hostnameLineEdit.setText( DEFAULT_HOST_ADDRESS );

    char hostname[256] = { 0 };
    gethostname( hostname, 256 );
    _streamNameLineEdit.setText( QString("%1@%2").arg( getUserName( ))
                                                 .arg( hostname ));
#ifdef __APPLE__
    _listView.setModel( new DesktopWindowsModel );
    connect( &_listView, &QListView::clicked, [=](const QModelIndex& current) {
        const QString& appname =
                _listView.model()->data( current, Qt::DisplayRole ).toString();
        _streamNameLineEdit.setText( QString("%1@%2").arg(appname)
                                                     .arg(hostname)); } );
#endif

    // frame rate limiting
    _frameRateSpinBox.setRange( 1, 60 );
    _frameRateSpinBox.setValue( 24 );

    // add widgets to UI
#ifdef __APPLE__
    formLayout->addRow( "Windows", &_listView );
#endif
    formLayout->addRow( "Hostname", &_hostnameLineEdit );
    formLayout->addRow( "Stream name", &_streamNameLineEdit );
#ifdef STREAM_EVENTS_SUPPORTED
    formLayout->addRow( "Allow desktop interaction", &_streamEventsBox );
    _streamEventsBox.setChecked( true );
    connect( &_streamEventsBox, SIGNAL( clicked( bool )),
             this, SLOT( _onStreamEventsBoxClicked( bool )));
#endif
    formLayout->addRow( "Max frame rate", &_frameRateSpinBox );
    formLayout->addRow( "Actual frame rate", &_frameRateLabel );

    widget->setLayout( formLayout );

    // share desktop action
    _shareDesktopAction = new QAction( "Share Desktop", this );
    _shareDesktopAction->setStatusTip( "Share desktop" );
    _shareDesktopAction->setCheckable( true );
    _shareDesktopAction->setChecked( false );
    connect( _shareDesktopAction, SIGNAL( triggered( bool )), this,
             SLOT( _shareDesktop( bool )));
    connect( this, SIGNAL( streaming( bool )), _shareDesktopAction,
             SLOT( setChecked( bool )));

    QToolBar* toolbar = addToolBar( "toolbar" );
    toolbar->addAction( _shareDesktopAction );

    // add About dialog
    QAction* showAboutDialog = new QAction( "About", this );
    showAboutDialog->setStatusTip( "About DesktopStreamer" );
    connect( showAboutDialog, &QAction::triggered,
             this, &MainWindow::_openAboutWidget );
    QMenu* helpMenu = menuBar()->addMenu( "&Help" );
    helpMenu->addAction( showAboutDialog );

    // Update timer
    connect( &_updateTimer, SIGNAL( timeout( )), this, SLOT( _update( )));

#ifdef DEFLECT_USE_SERVUS
    _servus.beginBrowsing( servus::Servus::IF_ALL );
    connect( &_browseTimer, SIGNAL( timeout( )), this, SLOT( _updateServus( )));
    _browseTimer.start( SERVUS_BROWSE_DELAY );
#endif
}

void MainWindow::_startStreaming()
{
    if( _stream )
        return;

    _stream = new deflect::Stream( _streamNameLineEdit.text().toStdString(),
                                   _hostnameLineEdit.text().toStdString( ));
    if( !_stream->isConnected( ))
    {
        _handleStreamingError( "Could not connect to host!" );
        return;
    }
#ifdef STREAM_EVENTS_SUPPORTED
    if( _streamEventsBox.isChecked( ))
        _stream->registerForEvents();
#endif

#ifdef __APPLE__
    _napSuspender.suspend();
#endif
#ifdef DEFLECT_USE_SERVUS
    _browseTimer.stop();
#endif
    _updateTimer.start( SHARE_DESKTOP_UPDATE_DELAY );
}

void MainWindow::_stopStreaming()
{
    _updateTimer.stop();
    _frameRateLabel.setText( "" );

    delete _stream;
    _stream = 0;

#ifdef __APPLE__
    _napSuspender.resume();
#endif
    emit streaming( false );
}

void MainWindow::_handleStreamingError( const QString& errorMessage )
{
    std::cerr << errorMessage.toStdString() << std::endl;
    QMessageBox::warning( this, "Error", errorMessage, QMessageBox::Ok,
                          QMessageBox::Ok );

    _stopStreaming();
}

void MainWindow::closeEvent( QCloseEvent* closeEvt )
{
    _stopStreaming();
    QMainWindow::closeEvent( closeEvt );
}

void MainWindow::_shareDesktop( const bool set )
{
    if( set )
        _startStreaming();
    else
        _stopStreaming();
}

void MainWindow::_update()
{
    if( _stream->isRegisteredForEvents( ))
        _processStreamEvents();
    _shareDesktopUpdate();
}

void MainWindow::_processStreamEvents()
{
    while( _stream->hasEvent( ))
    {
        const deflect::Event& wallEvent = _stream->getEvent();
        // Once registered for events they must be consumed, otherwise they
        // queue up. Until unregister is implemented, just ignore them.
        if( !_streamEventsBox.checkState( ))
            break;
#ifndef NDEBUG
        std::cout << "----------" << std::endl;
#endif
        switch( wallEvent.type )
        {
        case deflect::Event::EVT_CLOSE:
            _stopStreaming();
            break;
        case deflect::Event::EVT_PRESS:
            _sendMouseMoveEvent( wallEvent.mouseX, wallEvent.mouseY );
            _sendMousePressEvent( wallEvent.mouseX, wallEvent.mouseY );
            break;
        case deflect::Event::EVT_RELEASE:
            _sendMouseMoveEvent( wallEvent.mouseX, wallEvent.mouseY );
            _sendMouseReleaseEvent( wallEvent.mouseX, wallEvent.mouseY );
            break;
        case deflect::Event::EVT_DOUBLECLICK:
            _sendMouseDoubleClickEvent( wallEvent.mouseX, wallEvent.mouseY );
            break;

        case deflect::Event::EVT_MOVE:
            _sendMouseMoveEvent( wallEvent.mouseX, wallEvent.mouseY );
            break;
        case deflect::Event::EVT_CLICK:
        case deflect::Event::EVT_WHEEL:
        case deflect::Event::EVT_SWIPE_LEFT:
        case deflect::Event::EVT_SWIPE_RIGHT:
        case deflect::Event::EVT_SWIPE_UP:
        case deflect::Event::EVT_SWIPE_DOWN:
        case deflect::Event::EVT_KEY_PRESS:
        case deflect::Event::EVT_KEY_RELEASE:
        case deflect::Event::EVT_VIEW_SIZE_CHANGED:
        default:
            break;
        }
    }
}

#ifdef DEFLECT_USE_SERVUS
void MainWindow::_updateServus()
{
    if( _hostnameLineEdit.text() != DEFAULT_HOST_ADDRESS )
    {
        _browseTimer.stop();
        return;
    }

    _servus.browse( 0 );
    const servus::Strings& hosts = _servus.getInstances();
    if( hosts.empty( ))
        return;

    _browseTimer.stop();
    _hostnameLineEdit.setText( _servus.getHost( hosts.front( )).c_str( ));
}
#endif

void MainWindow::_shareDesktopUpdate()
{
    QTime frameTime;
    frameTime.start();

    QPixmap pixmap;
#ifdef __APPLE__
    if( _listView.currentIndex().row() != 0 )
    {
        pixmap = _listView.model()->data( _listView.currentIndex(),
                                          Qt::UserRole ).value< QPixmap >();
        _windowRect = _listView.model()->data( _listView.currentIndex(),
                                              Qt::UserRole+1 ).value< QRect >();
    }
    else
#endif
    {
        pixmap = QApplication::primaryScreen()->grabWindow( 0 );
        _windowRect = QRect( 0, 0, pixmap.width(), pixmap.height( ));
    }

    if( pixmap.isNull( ))
    {
        _handleStreamingError( "Got NULL desktop pixmap" );
        return;
    }
    QImage image = pixmap.toImage();

    // render mouse cursor
    QPoint mousePos = ( devicePixelRatio() * QCursor::pos() - _windowRect.topLeft()) -
                        QPoint( _cursor.width() / 2, _cursor.height() / 2 );
    QPainter painter( &image );
    painter.drawImage( mousePos, _cursor );
    painter.end(); // Make sure to release the QImage before using it

    // QImage Format_RGB32 (0xffRRGGBB) corresponds to GL_BGRA == deflect::BGRA
    deflect::ImageWrapper deflectImage( (const void*)image.bits(),
                                        image.width(), image.height(),
                                        deflect::BGRA );
    deflectImage.compressionPolicy = deflect::COMPRESSION_ON;

    bool success = _stream->send( deflectImage ) && _stream->finishFrame();
    if( !success )
    {
        _handleStreamingError( "Streaming failure, connection closed." );
        return;
    }

    _regulateFrameRate( frameTime.elapsed( ));
}

void MainWindow::_regulateFrameRate( const int elapsedFrameTime )
{
    // frame rate limiting
    const int maxFrameRate = _frameRateSpinBox.value();
    const int desiredFrameTime = (int)( 1000.f * 1.f / (float)maxFrameRate );
    const int sleepTime = desiredFrameTime - elapsedFrameTime;

    if( sleepTime > 0 )
    {
#ifdef _WIN32
        Sleep( sleepTime );
#else
        usleep( 1000 * sleepTime );
#endif
    }

    // frame rate is calculated for every FRAME_RATE_AVERAGE_NUM_FRAMES
    // sequential frames
    _frameSentTimes.push_back( QTime::currentTime( ));

    if( _frameSentTimes.size() > FRAME_RATE_AVERAGE_NUM_FRAMES )
    {
        _frameSentTimes.clear();
    }
    else if( _frameSentTimes.size() == FRAME_RATE_AVERAGE_NUM_FRAMES )
    {
        const float fps = (float)_frameSentTimes.size() * 1000.f /
               (float)_frameSentTimes.front().msecsTo( _frameSentTimes.back( ));

        _frameRateLabel.setText( QString::number( fps ) + QString( " fps" ));
    }
}

void MainWindow::_onStreamEventsBoxClicked( const bool checked )
{
    if( !checked )
        return;
#ifdef STREAM_EVENTS_SUPPORTED
    if( _stream && _stream->isConnected() && !_stream->isRegisteredForEvents( ))
        _stream->registerForEvents();
#endif
}

void MainWindow::_openAboutWidget()
{
    const int revision = deflect::Version::getRevision();

    std::ostringstream aboutMsg;
    aboutMsg << "Current version: " << deflect::Version::getString();
    aboutMsg << std::endl;
    aboutMsg << "SCM revision: " << std::hex << revision << std::dec;

    QMessageBox::about( this, "About DesktopStreamer", aboutMsg.str().c_str( ));
}

#ifdef __APPLE__
void sendMouseEvent( const CGEventType type, const CGMouseButton button,
                     const CGPoint point )
{
    CGEventRef event = CGEventCreateMouseEvent( 0, type, point, button );
    CGEventSetType( event, type );
    CGEventPost( kCGHIDEventTap, event );
    CFRelease( event );
}

void MainWindow::_sendMousePressEvent( const float x, const float y )
{
    CGPoint point;
    point.x = _windowRect.topLeft().x() + x * _windowRect.width();
    point.y = _windowRect.topLeft().y() + y * _windowRect.height();
#ifndef NDEBUG
    std::cout << "Press " << point.x << ", " << point.y << " ("
              << x << ", " << y << ")"<< std::endl;
#endif
    sendMouseEvent( kCGEventLeftMouseDown, kCGMouseButtonLeft, point );
}

void MainWindow::_sendMouseMoveEvent( const float x, const float y )
{
    CGPoint point;
    point.x = _windowRect.topLeft().x() + x * _windowRect.width();
    point.y = _windowRect.topLeft().y() + y * _windowRect.height();
#ifndef NDEBUG
    std::cout << "Move " << point.x << ", " << point.y << " ("
              << x << ", " << y << ")"<< std::endl;
#endif
    sendMouseEvent( kCGEventMouseMoved, kCGMouseButtonLeft, point );
}

void MainWindow::_sendMouseReleaseEvent( const float x, const float y )
{
    CGPoint point;
    point.x = _windowRect.topLeft().x() + x * _windowRect.width();
    point.y = _windowRect.topLeft().y() + y * _windowRect.height();
#ifndef NDEBUG
    std::cout << "Release " << point.x << ", " << point.y << " ("
              << x << ", " << y << ")"<< std::endl;
#endif
    sendMouseEvent( kCGEventLeftMouseUp, kCGMouseButtonLeft, point );
}

void MainWindow::_sendMouseDoubleClickEvent( const float x, const float y )
{
    CGPoint point;
    point.x = _windowRect.topLeft().x() + x * _windowRect.width();
    point.y = _windowRect.topLeft().y() + y * _windowRect.height();
    CGEventRef event = CGEventCreateMouseEvent( 0, kCGEventLeftMouseDown,
                                                point, kCGMouseButtonLeft );
#ifndef NDEBUG
    std::cout << "Double click " << point.x << ", " << point.y << " ("
              << x << ", " << y << ")"<< std::endl;
#endif

    CGEventSetIntegerValueField( event, kCGMouseEventClickState, 2 );
    CGEventPost( kCGHIDEventTap, event );

    CGEventSetType( event, kCGEventLeftMouseUp );
    CGEventPost( kCGHIDEventTap, event );

    CGEventSetType( event, kCGEventLeftMouseDown );
    CGEventPost( kCGHIDEventTap, event );

    CGEventSetType( event, kCGEventLeftMouseUp );
    CGEventPost( kCGHIDEventTap, event );
    CFRelease( event );
}
#else
void MainWindow::_sendMousePressEvent( const float, const float ) {}
void MainWindow::_sendMouseMoveEvent( const float, const float ) {}
void MainWindow::_sendMouseReleaseEvent( const float, const float ) {}
void MainWindow::_sendMouseDoubleClickEvent( const float, const float ) {}
#endif
