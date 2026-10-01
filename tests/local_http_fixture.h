#pragma once
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QQueue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>

// Synthetic HTTP responses over real loopback sockets. Never contacts DeepL or loads a user key.
class LocalHttpFixture : public QTcpServer {
public:
    struct Reply { int status = 200; QByteArray body = R"({"translations":[{"text":"fixture","detected_source_language":"EN"}]})"; int delayMs = 0; };
    QQueue<Reply> replies;
    QList<QByteArray> requests;
    // When set and no queued reply is left, builds the reply from the request (method line + body).
    std::function<Reply(const QByteArray& request, const QByteArray& body)> handler;
    // Deterministic stand-in for DeepL: every text comes back as prefix + text (tags kept as they are).
    static Reply echo(const QByteArray& body, const QString& prefix = QStringLiteral("[KO] "), int delayMs = 0) {
        const QJsonArray texts = QJsonDocument::fromJson(body).object().value("text").toArray();
        QJsonArray out;
        for (const auto& text : texts)
            out.append(QJsonObject{{"text", prefix + text.toString()}, {"detected_source_language", "EN"}});
        return {200, QJsonDocument(QJsonObject{{"translations", out}}).toJson(QJsonDocument::Compact), delayMs};
    }
    static QByteArray bodyOf(const QByteArray& request) {
        const qsizetype split = request.indexOf("\r\n\r\n");
        return split < 0 ? QByteArray() : request.mid(split + 4);
    }
    LocalHttpFixture() {
        connect(this,&QTcpServer::newConnection,this,[this] {
            while (hasPendingConnections()) {
                QTcpSocket* socket = nextPendingConnection();
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QTcpSocket::readyRead,this,[this,socket] {
                    if (socket->property("answered").toBool()) return;
                    QByteArray bytes = socket->property("input").toByteArray() + socket->readAll();
                    socket->setProperty("input",bytes);
                    const qsizetype split = bytes.indexOf("\r\n\r\n");
                    if (split<0) return;
                    int length = 0;
                    for (const QByteArray& header : bytes.left(split).split('\n'))
                        if (header.trimmed().toLower().startsWith("content-length:"))
                            length=header.mid(header.indexOf(':')+1).trimmed().toInt();
                    if (bytes.size()<split+4+length) return;
                    socket->setProperty("answered",true);
                    requests << bytes;
                    const Reply response = !replies.isEmpty() ? replies.dequeue()
                        : handler ? handler(bytes, bytes.mid(split + 4, length)) : Reply{};
                    QTimer::singleShot(response.delayMs,socket,[socket,response] {
                        if (socket->state()!=QAbstractSocket::ConnectedState) return;
                        QByteArray bytes = "HTTP/1.1 "+QByteArray::number(response.status)+" Fixture\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(response.body.size())+"\r\n\r\n"+response.body;
                        socket->write(bytes);
                        socket->disconnectFromHost();
                    });
                });
            }
        });
    }
};
class LoopbackNetwork final : public QNetworkAccessManager {
public:
    explicit LoopbackNetwork(quint16 port) : port_(port) { setProxy(QNetworkProxy::NoProxy); }
protected:
    QNetworkReply* createRequest(Operation op,const QNetworkRequest& original,QIODevice* outgoing=nullptr) override {
        QNetworkRequest request(original);
        QUrl url=original.url(); url.setScheme("http"); url.setHost("127.0.0.1"); url.setPort(port_);
        request.setUrl(url);
        return QNetworkAccessManager::createRequest(op,request,outgoing);
    }
private:
    quint16 port_;
};
