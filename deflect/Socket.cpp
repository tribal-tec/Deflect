/*********************************************************************/
/* Copyright (c) 2015-2017, EPFL/Blue Brain Project                  */
/*                          Raphael Dumusc <raphael.dumusc@epfl.ch>  */
/*                          Daniel Nachbaur <daniel.nachbaur@epfl.ch>*/
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

#include "Socket.h"

#include "MessageHeader.h"
#include "NetworkProtocol.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QLoggingCategory>
#include <QTcpSocket>

#include <sstream>

namespace
{
const int INVALID_NETWORK_PROTOCOL_VERSION = -1;
const int RECEIVE_TIMEOUT_MS = 5000;
}

namespace deflect
{
Socket::Socket(const std::string& host, const unsigned short port)
    : _host(host)
    , _socket(new QTcpSocket(this)) // Ensure that _socket parent is
                                    // *this* so it gets moved to thread
    , _serverProtocolVersion(INVALID_NETWORK_PROTOCOL_VERSION)
{
    // disable warnings which occur if no QCoreApplication is present during
    // _connect(): QObject::connect: Cannot connect (null)::destroyed() to
    // QHostInfoLookupManager::waitForThreadPoolDone()
    if (!qApp)
        QLoggingCategory::defaultCategory()->setEnabled(QtWarningMsg, false);

    _socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    _socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    _connect(host, port);

    // Both objects live in the same thread, can use direct connection.
    QObject::connect(_socket, &QTcpSocket::disconnected, this,
                     &Socket::disconnected, Qt::DirectConnection);
}

const std::string& Socket::getHost() const
{
    return _host;
}

unsigned short Socket::getPort() const
{
    return _socket->peerPort();
}

bool Socket::isConnected() const
{
    return _socket->state() == QTcpSocket::ConnectedState;
}

int32_t Socket::getServerProtocolVersion() const
{
    return _serverProtocolVersion;
}

int Socket::getFileDescriptor() const
{
    return _socket->socketDescriptor();
}

bool Socket::hasMessage(const size_t messageSize) const
{
    QMutexLocker locker(&_socketMutex);

    // needed to 'wakeup' socket when no data was streamed for a while
    _socket->waitForReadyRead(0);
    return _socket->bytesAvailable() >=
           (int)(MessageHeader::serializedSize + messageSize);
}

bool Socket::send(const MessageHeader& messageHeader, const QByteArray& message,
                  const bool waitForBytesWritten)
{
    QMutexLocker locker(&_socketMutex);
    if (!isConnected())
        return false;

    // send header
    QDataStream stream(_socket);
    stream << messageHeader;
    if (stream.status() != QDataStream::Ok)
        return false;

    // send message
    const bool allSent = _write(message);

    if (waitForBytesWritten)
    {
        // Needed in the absence of event loop, otherwise the reception is
        // frozen.
        while (_socket->bytesToWrite() > 0 && isConnected())
            _socket->waitForBytesWritten();
    }
    return allSent;
}

bool Socket::receive(MessageHeader& messageHeader, QByteArray& message)
{
    QMutexLocker locker(&_socketMutex);

    if (!_receiveHeader(messageHeader))
        return false;

    // get the message
    if (messageHeader.size > 0)
    {
        message = _socket->read(messageHeader.size);

        while (message.size() < int(messageHeader.size))
        {
            if (!_socket->waitForReadyRead(RECEIVE_TIMEOUT_MS))
                return false;

            message.append(_socket->read(messageHeader.size - message.size()));
        }
    }

    if (messageHeader.type == MESSAGE_TYPE_QUIT)
    {
        //_socket->disconnectFromHost();
        return false;
    }

    return true;
}

bool Socket::_receiveHeader(MessageHeader& messageHeader)
{
    while (_socket->bytesAvailable() < qint64(MessageHeader::serializedSize))
    {
        if (!_socket->waitForReadyRead(RECEIVE_TIMEOUT_MS))
            return false;
    }

    QDataStream stream(_socket);
    stream >> messageHeader;

    return stream.status() == QDataStream::Ok;
}

void Socket::_connect(const std::string& host, const unsigned short port)
{
    _socket->connectToHost(host.c_str(), port);
    if (!_socket->waitForConnected(RECEIVE_TIMEOUT_MS))
    {
        std::stringstream ss;
        ss << "could not connect to " << host << ":" << port;
        throw std::runtime_error(ss.str());
    }

    if (!_receiveProtocolVersion())
    {
        //_socket->disconnectFromHost();
        throw std::runtime_error("server protocol version was not received");
    }

    if (_serverProtocolVersion < NETWORK_PROTOCOL_VERSION)
    {
        //_socket->disconnectFromHost();
        std::stringstream ss;
        ss << "server uses unsupported protocol: " << _serverProtocolVersion
           << " < " << NETWORK_PROTOCOL_VERSION;
        throw std::runtime_error(ss.str());
    }
}

bool Socket::_receiveProtocolVersion()
{
    while (_socket->bytesAvailable() < qint64(sizeof(int32_t)))
    {
        if (!_socket->waitForReadyRead(RECEIVE_TIMEOUT_MS))
            return false;
    }
    _socket->read((char*)&_serverProtocolVersion, sizeof(int32_t));
    return true;
}

bool Socket::_write(const QByteArray& message)
{
    bool allSent = true;
    if (!message.isEmpty())
    {
        // Send message data
        const char* data = message.constData();
        const int size = message.size();

        int sent = _socket->write(data, size);

        while (sent < size && isConnected())
            sent += _socket->write(data + sent, size - sent);

        allSent = sent == size;
    }
    return allSent;
}
}
