/*********************************************************************/
/* Copyright (c) 2011-2012, The University of Texas at Austin.       */
/* Copyright (c) 2014-2018, EPFL/Blue Brain Project                  */
/*                          Raphael Dumusc <raphael.dumusc@epfl.ch>  */
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
/*    THIS  SOFTWARE  IS  PROVIDED  BY  THE  ECOLE  POLYTECHNIQUE    */
/*    FEDERALE DE LAUSANNE  ''AS IS''  AND ANY EXPRESS OR IMPLIED    */
/*    WARRANTIES, INCLUDING, BUT  NOT  LIMITED  TO,  THE  IMPLIED    */
/*    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR  A PARTICULAR    */
/*    PURPOSE  ARE  DISCLAIMED.  IN  NO  EVENT  SHALL  THE  ECOLE    */
/*    POLYTECHNIQUE  FEDERALE  DE  LAUSANNE  OR  CONTRIBUTORS  BE    */
/*    LIABLE  FOR  ANY  DIRECT,  INDIRECT,  INCIDENTAL,  SPECIAL,    */
/*    EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING, BUT NOT    */
/*    LIMITED TO,  PROCUREMENT  OF  SUBSTITUTE GOODS OR SERVICES;    */
/*    LOSS OF USE, DATA, OR  PROFITS;  OR  BUSINESS INTERRUPTION)    */
/*    HOWEVER CAUSED AND  ON ANY THEORY OF LIABILITY,  WHETHER IN    */
/*    CONTRACT, STRICT LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE    */
/*    OR OTHERWISE) ARISING  IN ANY WAY  OUT OF  THE USE OF  THIS    */
/*    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.   */
/*                                                                   */
/* The views and conclusions contained in the software and           */
/* documentation are those of the authors and should not be          */
/* interpreted as representing official policies, either expressed   */
/* or implied, of Ecole polytechnique federale de Lausanne.          */
/*********************************************************************/

#include <deflect/server/EventReceiver.h>
#include <deflect/server/Server.h>

#include <QCoreApplication>
#include <QThread>
#include <QTime>

#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    auto server = new deflect::server::Server(1701);

    QTime* time = new QTime();
    uint32_t currentFrame = 0u;
    QString uri;

    server->connect(server, &deflect::server::Server::pixelStreamOpened,
                    [&](const QString uri_) {
                         uri = uri_;
                         server->requestFrame(uri);
                     });

    app.connect(server, &deflect::server::Server::receivedFrame,
                 [&](deflect::server::FramePtr) {
                 ++currentFrame;
                 const uint32_t smoothingInterval = 30u;
                 if(currentFrame == smoothingInterval)
                 {
                     const int milliseconds = time->elapsed();
                     const float fps = 1000.0f * smoothingInterval / (float)milliseconds;
                     std::cout << "fps: " << fps << std::endl;
                     currentFrame = 0u;
                     time->restart();
                 }
                 server->requestFrame(uri);
                 });

     app.connect(server, &deflect::server::Server::registerToEvents,
                 [&](const QString, const bool,
                 deflect::server::EventReceiver*,
                 deflect::server::BoolPromisePtr success) {
                         success->set_value(true);
                 });

    time->start();
    return app.exec();
}
