/*********************************************************************/
/* Copyright (c) 20115, EPFL/Blue Brain Project                      */
/*                        Daniel Nachbaur <daniel.nachbaur@epfl.ch>  */
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

#ifndef DESKTOPWINDOWSMODEL_H
#define DESKTOPWINDOWSMODEL_H

#include <QtMac>
#include <CoreGraphics/CoreGraphics.h>

#include <QAbstractListModel>

#include <tuple>
#include <vector>

namespace
{
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

const int PREVIEWIMAGEHEIGHT = 100;
const CGWindowID DESKTOPWINDOWID = 0;

QPixmap getPreviewPixmap( const QPixmap& pixmap )
{
    return QPixmap::fromImage( pixmap.toImage().scaledToHeight(
                                PREVIEWIMAGEHEIGHT, Qt::SmoothTransformation ));
}

QPixmap getWindowPixmap( const CGWindowID windowID )
{
    const CGImageRef windowImage =
            CGWindowListCreateImage( CGRectNull,
                                     kCGWindowListOptionIncludingWindow,
                                     windowID,
                                     kCGWindowImageBoundsIgnoreFraming );

    return QtMac::fromCGImageRef( windowImage );
}

QRect getWindowRect( const CGWindowID windowID )
{
    CGWindowID windowids[1] = { windowID };
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

        _data.push_back( std::make_tuple( "Desktop", DESKTOPWINDOWID,
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
            CGWindowID windowID;
            CFNumberGetValue( cfWindowID, kCFNumberIntType, &windowID );
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
        const auto& data = _data[index.row()];
        switch( role )
        {
        case Qt::DisplayRole:
            return QString("%1").arg( std::get< APPNAME >( data ));

        case Qt::DecorationRole:
            return std::get< WINDOWIMAGE >( data );

        case ROLE_PIXMAP:
            return getWindowPixmap( std::get< WINDOWID >( data ));

        case ROLE_RECT:
            return getWindowRect( std::get< WINDOWID >( data ));

        default:
            return QVariant();
        }
    }

    enum DataRole
    {
        ROLE_PIXMAP = Qt::UserRole,
        ROLE_RECT
    };

private:
    enum TupleValues
    {
        APPNAME,
        WINDOWID,
        WINDOWIMAGE
    };

    std::vector< std::tuple< QString, CGWindowID, QPixmap > > _data;
};

} // namespace

#endif
