/*********************************************************************/
/* Copyright (c) 2015, EPFL/Blue Brain Project                      */
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

#include "DesktopWindowsModel.h"

#include <QApplication>
#include <QDebug>
#include <QtMac>
#include <QPixmap>
#include <QScreen>

#import <AppKit/NSWorkspace.h>
#import <AppKit/NSRunningApplication.h>
#import <Foundation/Foundation.h>

namespace
{
const int PREVIEWIMAGEWIDTH = 200;

/**
 * Based on: http://www.qtcentre.org/threads/34752-NSString-to-QString
 */
QString NSStringToQString( const NSString* nsstr )
{
    NSRange range;
    range.location = 0;
    range.length = [nsstr length];
    unichar* chars = new unichar[range.length];
    [nsstr getCharacters:chars range:range];
    QString result = QString::fromUtf16( chars, range.length );
    delete [] chars;
    return result;
}

/**
 * Based on https://github.com/quicksilver/UIAccess-qsplugin/blob/a31107764a9f9951173b326441d46f62b7644dd0/QSUIAccessPlugIn_Action.m#L109
 */
NSArray* getWindows( NSRunningApplication* app, const CFArrayRef& windowList )
{
    __block NSMutableArray* windows = [NSMutableArray array];
    [(NSArray*)windowList enumerateObjectsWithOptions:NSEnumerationConcurrent
                           usingBlock:^( NSDictionary* info, NSUInteger, BOOL* )
    {
        const int pid = [(NSNumber*)[info objectForKey:(NSString *)kCGWindowOwnerPID] intValue];
        if( pid == [app processIdentifier])
            [windows addObject:info];
    }];
    return [[windows copy] autorelease];
}

QPixmap getPreviewPixmap( const QPixmap& pixmap )
{
    return QPixmap::fromImage( pixmap.toImage().scaledToWidth(
                                 PREVIEWIMAGEWIDTH, Qt::SmoothTransformation ));
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
    const CGWindowID windowids[1] = { windowID };
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

} // namespace


@interface AppObserver : NSObject
{
    DesktopWindowsModel* parent;
}
- (void)setParent:(DesktopWindowsModel*)parent_;
@end

@implementation AppObserver

- (id) init
{
    self = [super init];

    NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
    NSNotificationCenter* center = [workspace notificationCenter];
    [center addObserver:self
            selector:@selector(newApplication:)
            name:NSWorkspaceDidLaunchApplicationNotification
            object:Nil];
    [center addObserver:self
            selector:@selector(closedApplication:)
            name:NSWorkspaceDidTerminateApplicationNotification
            object:Nil];

    return self;
}

- (void) dealloc
{
    NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
    NSNotificationCenter* center = [workspace notificationCenter];
    [center removeObserver:self
            name:NSWorkspaceDidLaunchApplicationNotification
            object:Nil];
    [center removeObserver:self
            name:NSWorkspaceDidTerminateApplicationNotification
            object:Nil];

    [super dealloc];
}

- (void)setParent:(DesktopWindowsModel*)parent_
{
    parent = parent_;
}

- (void)newApplication:(NSNotification*) notification
{
    NSRunningApplication* app =
            [[notification userInfo] objectForKey:@"NSWorkspaceApplicationKey"];
    parent->addApplication( app );
}

- (void)closedApplication:(NSNotification*) notification
{
    NSRunningApplication* app =
            [[notification userInfo] objectForKey:@"NSWorkspaceApplicationKey"];
    parent->removeApplication( app );
}

@end


class DesktopWindowsModel::Impl
{
public:
    Impl( DesktopWindowsModel& parent )
        : _parent( parent )
        , _observer( [[AppObserver alloc] init] )
    {
        [_observer setParent:&parent];
        reloadData();
    }

    ~Impl()
    {
        [_observer release];
    }

    enum TupleValues
    {
        APPNAME,
        WINDOWID,
        WINDOWIMAGE
    };

    void addApplication( NSRunningApplication* app )
    {
        _parent.beginResetModel();
        for( ;; )
        {
            CFArrayRef windowList =
                CGWindowListCopyWindowInfo( kCGWindowListOptionOnScreenOnly|
                                            kCGWindowListExcludeDesktopElements,
                                            kCGNullWindowID );
            if( _addApplication( app, windowList ))
                break;
            sleep( 1 );
        }
        _parent.endResetModel();
    }

    void removeApplication( NSRunningApplication* app )
    {
        _parent.beginResetModel();
        const QString& appName = NSStringToQString([app localizedName]);
        const auto& i = std::find_if( _data.begin(), _data.end(),
                                      [&]( const Data::value_type& entry )
            { return std::get< Impl::APPNAME >( entry ) == appName; } );
        _data.erase( i );
        _parent.endResetModel();
    }

    void reloadData()
    {
        _parent.beginResetModel();

        NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
        NSArray* runningApplications = [workspace runningApplications];

        CFArrayRef windowList =
                CGWindowListCopyWindowInfo( kCGWindowListOptionOnScreenOnly|
                                            kCGWindowListExcludeDesktopElements,
                                            kCGNullWindowID );

        _data.clear();
        _data.push_back( std::make_tuple( "Desktop", 0,
            getPreviewPixmap( QApplication::primaryScreen()->grabWindow( 0 ))));

        for( NSRunningApplication* app in runningApplications )
            _addApplication( app, windowList );

        CFRelease( windowList );
        _parent.endResetModel();
    }

    DesktopWindowsModel& _parent;
    typedef std::vector< std::tuple< QString, CGWindowID, QPixmap > > Data;
    Data _data;
    AppObserver* _observer;

private:
    bool _addApplication( NSRunningApplication* app, CFArrayRef windowList )
    {
        const QString& appName = NSStringToQString([app localizedName]);
        const auto& pid = [app processIdentifier];
        if( appName == "SystemUIServer" || appName == "Dock" ||
            pid == QApplication::applicationPid( ))
        {
           return true;
        }

        NSArray* windows = getWindows( app, windowList );

        bool gotOne = false;
        for( NSDictionary* info in windows )
        {
           const QString& windowName = NSStringToQString(
                             [info objectForKey:(NSString*)kCGWindowName]);

           const int windowLayer =
                  [[info objectForKey:(NSString*)kCGWindowLayer] intValue];
           const CGWindowID windowID =
                   [[info objectForKey:(NSString*)kCGWindowNumber]
                     unsignedIntValue];
           const QRect& rect = getWindowRect( windowID );
           if( rect.width() <= 1 || rect.height() <= 1 || windowLayer != 0 )
           {
               if( windowName.isEmpty( ))
                   qDebug() << "Ignoring" << appName;
               else
                   qDebug() << "Ignoring" << appName << "-" << windowName;
               continue;
           }

           _data.push_back( std::make_tuple( appName, windowID,
                          getPreviewPixmap( getWindowPixmap( windowID ))));
           gotOne = true;
        }
        return gotOne;
    }
};

DesktopWindowsModel::DesktopWindowsModel()
    : QAbstractListModel()
    , _impl( new Impl( *this ))
{
}

int DesktopWindowsModel::rowCount( const QModelIndex& ) const
{
    return int( _impl->_data.size( ));
}

QVariant DesktopWindowsModel::data( const QModelIndex& index, int role ) const
{
    const auto& data = _impl->_data[index.row()];
    switch( role )
    {
    case Qt::DisplayRole:
        return QString("%1").arg( std::get< Impl::APPNAME >( data ));

    case Qt::DecorationRole:
        return std::get< Impl::WINDOWIMAGE >( data );

    case ROLE_PIXMAP:
        return getWindowPixmap( std::get< Impl::WINDOWID >( data ));

    case ROLE_RECT:
        return getWindowRect( std::get< Impl::WINDOWID >( data ));

    default:
        return QVariant();
    }
}

void DesktopWindowsModel::addApplication( void* app )
{
    _impl->addApplication( (NSRunningApplication*)app );
}

void DesktopWindowsModel::removeApplication( void* app )
{
    _impl->removeApplication( (NSRunningApplication*)app );
}
