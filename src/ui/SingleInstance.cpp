#include "ui/SingleInstance.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr int kConnectTimeoutMs = 1000;
constexpr int kAckTimeoutMs = 5000;
constexpr char kAck = 'k';
constexpr qint64 kMaxMessage = 1 << 20;
constexpr QDataStream::Version kStreamVersion = QDataStream::Qt_6_0;

bool sendTo(const QString& name, const QStringList& paths)
{
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(kConnectTimeoutMs))
        return false;
#ifdef Q_OS_WIN
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    QDataStream out(&socket);
    out.setVersion(kStreamVersion);
    out << paths;
    socket.flush();
    while (socket.bytesAvailable() < 1 && socket.waitForReadyRead(kAckTimeoutMs)) {
    }
    socket.disconnectFromServer();
    return true;
}
}

SingleInstance::SingleInstance(const QString& key, QObject* parent) : QObject(parent)
{
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    name_ = QStringLiteral("JustNBT-") + QString::fromLatin1(hash.left(16));
}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::handOver(const QStringList& paths)
{
    if (sendTo(name_, paths))
        return true;
    if (listen())
        return false;
    if (sendTo(name_, paths))
        return true;
    QLocalServer::removeServer(name_);
    listen();
    return false;
}

bool SingleInstance::listen()
{
    delete server_;
    server_ = new QLocalServer(this);
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_->listen(name_)) {
        delete server_;
        server_ = nullptr;
        return false;
    }
    connect(server_, &QLocalServer::newConnection, this, &SingleInstance::acceptConnection);
    return true;
}

void SingleInstance::acceptConnection()
{
    while (QLocalSocket* socket = server_->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readMessage(socket); });
        readMessage(socket);
    }
}

void SingleInstance::readMessage(QLocalSocket* socket)
{
    if (socket->bytesAvailable() > kMaxMessage) {
        socket->abort();
        return;
    }
    QDataStream in(socket);
    in.setVersion(kStreamVersion);
    in.startTransaction();
    QStringList paths;
    in >> paths;
    if (!in.commitTransaction())
        return;
    socket->write(&kAck, 1);
    socket->flush();
    emit pathsReceived(paths);
}
