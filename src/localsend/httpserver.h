/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOCALSEND_HTTPSERVER_H
#define LOCALSEND_HTTPSERVER_H

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QUrlQuery>

class QSslServer;
class QTcpSocket;
class Identity;

/*
 * One HTTP/1.1 request on a TLS connection. Small bodies (JSON) are read
 * whole before HttpServer::request is emitted; for streamed routes
 * (HttpServer::setStreamedPath) the handler gets the request as soon as the
 * headers are in and either gives a sink for the body or answers right away.
 */
class HttpRequest : public QObject
{
    Q_OBJECT
public:
    QString method;
    QString path;
    QUrlQuery query;
    QHash<QByteArray, QByteArray> headers; // lower-case names
    QByteArray body;                       // only for non-streamed requests
    QHostAddress peer;
    // SHA-256 of the client certificate when the client sent one (mTLS), else empty
    QString peerFingerprint;
    qint64 contentLength = -1;

    // Streamed body: written to sink as it arrives; bodyFinished(ok) at the end
    void setBodySink(QIODevice *sink);
    void respond(int status, const QByteArray &body = {}, const QByteArray &contentType = "application/json");
    bool responded() const { return m_responded; }

signals:
    void bodyProgress(qint64 received);
    void bodyFinished(bool complete);
    // The connection went away before an answer was sent (sender cancelled)
    void aborted();

private:
    friend class HttpConnection;
    explicit HttpRequest(QObject *parent) : QObject(parent) {}
    QPointer<QIODevice> m_sink;
    bool m_responded = false;
    class HttpConnection *m_connection = nullptr;
};

class HttpServer : public QObject
{
    Q_OBJECT
public:
    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer() override;

    // Listens on every address. Plain HTTP when identity is null (tests only).
    bool listen(quint16 port, const Identity *identity);
    void close();
    bool isListening() const;
    quint16 port() const;

    void setStreamedPath(const QString &path) { m_streamed = path; }

signals:
    void request(HttpRequest *request);

private:
    friend class HttpConnection;
    void incoming(QTcpSocket *socket);

    QSslServer *m_server = nullptr;
    class QTcpServer *m_plain = nullptr;
    QString m_streamed;
    int m_connections = 0;
};

#endif
