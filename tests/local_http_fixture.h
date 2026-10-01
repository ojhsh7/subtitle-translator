#pragma once
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QQueue>

// Synthetic HTTP responses over real loopback sockets. Never contacts DeepL or loads a user key.
class LocalHttpFixture : public QTcpServer {
public:
    struct Reply { int status = 200; QByteArray body = R"({"translations":[{"text":"fixture","detected_source_language":"EN"}]})"; int delayMs = 0; };
    QQueue<Reply> replies;
    QList<QByteArray> requests;
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
                    const Reply response = replies.isEmpty() ? Reply{} : replies.dequeue();
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
