#include "single_instance.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QThread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

SingleInstance::SingleInstance(const QString& key, QObject* parent) : QObject(parent), name_(serverName(key)) {
    // Only the same Windows user may connect; another account cannot drive this app.
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, &SingleInstance::accept);
}
SingleInstance::~SingleInstance() { server_.close(); }
QString SingleInstance::serverName(const QString& key) {
    const QByteArray user = qgetenv("USERNAME") + '|' + qgetenv("USERDOMAIN") + '|' + key.toUtf8();
    return QStringLiteral("SubtitleTranslator-") +
           QString::fromLatin1(QCryptographicHash::hash(user, QCryptographicHash::Sha256).toHex().left(20));
}
bool SingleInstance::claim(const QStringList& arguments, int timeoutMs) {
    // Two launches at the same moment may both miss the server; the loser of listen() retries.
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (forward(arguments, timeoutMs)) return false;
        if (server_.listen(name_)) return true;
        QThread::msleep(100);
    }
    return true; // Never refuse to start because the pipe is unusable.
}
bool SingleInstance::forward(const QStringList& arguments, int timeoutMs) {
    QLocalSocket socket;
    socket.connectToServer(name_);
    if (!socket.waitForConnected(timeoutMs / 4)) return false;
    // The primary first sends its process id; allow it to take the foreground from this launch.
    if (!socket.waitForReadyRead(timeoutMs)) return false;
    const QByteArray hello = socket.readLine().trimmed();
    bool ok = false;
    const DWORD pid = hello.toULong(&ok);
    if (ok && pid) AllowSetForegroundWindow(pid);
    socket.write(QJsonDocument(QJsonArray::fromStringList(arguments)).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(timeoutMs)) return false;
    if (!socket.waitForReadyRead(timeoutMs)) return false;
    return socket.readLine().trimmed() == "ok";
}
void SingleInstance::accept() {
    while (QLocalSocket* socket = server_.nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        socket->write(QByteArray::number(qulonglong(GetCurrentProcessId())) + '\n');
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            if (!socket->canReadLine()) {
                if (socket->bytesAvailable() > 64 * 1024) socket->abort(); // not our client
                return;
            }
            const QJsonDocument document = QJsonDocument::fromJson(socket->readLine());
            QStringList arguments;
            for (const auto& value : document.array())
                if (value.isString() && arguments.size() < 32) arguments << value.toString().left(512);
            socket->write("ok\n");
            socket->flush();
            emit activated(arguments);
        });
    }
}
