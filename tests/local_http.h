#pragma once
// Minimal HTTP/1.1 server on 127.0.0.1 for tests (P-04). It answers each request with the next
// scripted reply, so the real network paths (status codes, Retry-After, time-outs, truncated bodies,
// cancellation) run without contacting DeepL.
#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <memory>

class LocalHttp final : public QObject {
public:
    struct Reply {
        int status = 200;
        QByteArray body;
        QByteArray extraHeaders; // e.g. "Retry-After: 7\r\n"
        bool hang = false;       // never answer (time-out and cancel paths)
        bool truncate = false;   // announce more bytes than are sent, then close
    };
    QList<Reply> replies;      // used in order; an empty list answers 200 with an empty body
    QList<QByteArray> requests; // every complete request as received, head and body

    LocalHttp() {
        server_.listen(QHostAddress::LocalHost, 0);
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = server_.nextPendingConnection()) serve(socket);
        });
    }
    bool listening() const { return server_.isListening(); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort())); }

private:
    void serve(QTcpSocket* socket) {
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            *buffer += socket->readAll();
            const qsizetype headEnd = buffer->indexOf("\r\n\r\n");
            if (headEnd < 0) return;
            qsizetype length = 0;
            for (const QByteArray& line : buffer->left(headEnd).split('\n'))
                if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
            if (buffer->size() < headEnd + 4 + length) return;
            requests << *buffer;
            buffer->clear();
            const Reply reply = replies.isEmpty() ? Reply{} : replies.takeFirst();
            if (reply.hang) return;
            const qsizetype announced = reply.body.size() + (reply.truncate ? 64 : 0);
            socket->write("HTTP/1.1 " + QByteArray::number(reply.status) + " Test\r\nContent-Type: application/json\r\n" +
                          reply.extraHeaders + "Content-Length: " + QByteArray::number(announced) +
                          "\r\nConnection: close\r\n\r\n" + reply.body);
            socket->flush();
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
    QTcpServer server_;
};
