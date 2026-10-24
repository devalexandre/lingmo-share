/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "httpserver.h"
#include "identity.h"

#include <QDebug>
#include <QSslConfiguration>
#include <QSslServer>
#include <QSslSocket>
#include <QTcpServer>
#include <QTimer>

static constexpr int s_maxHeader = 16 * 1024;
static constexpr qint64 s_maxBody = 4 * 1024 * 1024; // JSON (prepare-upload with many files)
static constexpr int s_maxConnections = 64;
static constexpr int s_idleTimeout = 60 * 1000;

static QByteArray reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 422: return "Unprocessable Entity";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    default: return "Internal Server Error";
    }
}

class HttpConnection : public QObject
{
    Q_OBJECT
public:
    HttpConnection(HttpServer *server, QTcpSocket *socket)
        : QObject(server)
        , m_server(server)
        , m_socket(socket)
    {
        socket->setParent(this);
        socket->setReadBufferSize(1024 * 1024);
        m_idle.setSingleShot(true);
        m_idle.setInterval(s_idleTimeout);
        connect(&m_idle, &QTimer::timeout, this, [this] { m_socket->abort(); });
        connect(socket, &QTcpSocket::readyRead, this, [this] { m_idle.start(); process(); });
        connect(socket, &QTcpSocket::disconnected, this, &HttpConnection::gone);
        connect(socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            if (m_socket->state() != QAbstractSocket::ConnectedState)
                gone();
        });
        if (auto *ssl = qobject_cast<QSslSocket *>(socket)) {
            const QSslCertificate peer = ssl->peerCertificate();
            if (!peer.isNull())
                m_peerFingerprint = Identity::fingerprintOf(peer);
        }
        m_idle.start();
        ++m_server->m_connections;
        process();
    }

    ~HttpConnection() override { --m_server->m_connections; }

    void respond(HttpRequest *req, int status, const QByteArray &body, const QByteArray &type)
    {
        if (req->m_responded || req != m_request)
            return;
        req->m_responded = true;
        m_idle.start();
        // An answer before the whole body arrived (rejected upload): close afterwards
        const bool close = m_state != Done || m_wantClose;
        QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status) + "\r\n";
        if (!body.isEmpty())
            head += "Content-Type: " + type + "\r\n";
        head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        head += close ? "Connection: close\r\n\r\n" : "Connection: keep-alive\r\n\r\n";
        m_socket->write(head + body);
        if (close) {
            m_closing = true;
            m_socket->disconnectFromHost();
            return;
        }
        // Next request on the same connection
        QTimer::singleShot(0, this, [this] {
            if (m_state == Done && m_request && m_request->m_responded)
                reset();
        });
    }

    void setSink(HttpRequest *req, QIODevice *sink)
    {
        if (req != m_request)
            return;
        req->m_sink = sink;
        QTimer::singleShot(0, this, &HttpConnection::process);
    }

private:
    enum State { Headers, Body, ChunkSize, ChunkData, ChunkCrlf, Trailers, Done };

    void gone()
    {
        if (m_gone)
            return;
        m_gone = true;
        if (m_request && !m_request->m_responded) {
            if (m_state != Done && m_request->m_sink)
                emit m_request->bodyFinished(false);
            emit m_request->aborted();
        }
        deleteLater();
    }

    void reset()
    {
        if (m_request)
            m_request->deleteLater();
        m_request = nullptr;
        m_state = Headers;
        m_received = 0;
        process();
    }

    void fail(int status)
    {
        m_closing = true;
        m_socket->write("HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status)
                        + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        m_socket->disconnectFromHost();
    }

    bool streamed() const { return m_request && m_request->path == m_server->m_streamed; }

    // Hands body bytes over; false when they must wait (no sink yet)
    bool consume(const QByteArray &data)
    {
        if (streamed()) {
            if (m_request->m_responded)
                return true; // rejected: drop
            if (!m_request->m_sink)
                return false;
            if (m_request->m_sink->write(data) != data.size()) {
                emit m_request->bodyFinished(false);
                fail(500);
                return true;
            }
            m_received += data.size();
            emit m_request->bodyProgress(m_received);
            return true;
        }
        if (m_request->body.size() + data.size() > s_maxBody) {
            fail(413);
            return true;
        }
        m_request->body += data;
        return true;
    }

    void finishBody()
    {
        m_state = Done;
        if (streamed()) {
            if (!m_request->m_responded)
                emit m_request->bodyFinished(true);
        } else {
            // The answer may wait for the user (prepare-upload): no idle timeout meanwhile
            m_idle.stop();
            emit m_server->request(m_request);
        }
    }

    void process()
    {
        if (m_closing || m_gone)
            return;
        while (!m_closing) {
            // Read in slices: a body waiting for its sink stays in the socket
            if (m_buffer.size() < 256 * 1024 && m_socket->bytesAvailable() > 0)
                m_buffer += m_socket->read(1024 * 1024);
            if (m_state == Headers) {
                const int end = m_buffer.indexOf("\r\n\r\n");
                if (end < 0) {
                    if (m_buffer.size() > s_maxHeader)
                        fail(431);
                    return;
                }
                if (!parseHeaders(m_buffer.left(end))) {
                    fail(400);
                    return;
                }
                m_buffer.remove(0, end + 4);
                if (streamed() && !m_request->m_responded)
                    emit m_server->request(m_request);
                if (m_closing || !m_request)
                    return;
                continue;
            }
            if (m_state == Body) {
                if (m_remaining == 0) {
                    finishBody();
                    continue;
                }
                if (m_buffer.isEmpty())
                    return;
                const QByteArray part = m_buffer.left(int(qMin<qint64>(m_remaining, m_buffer.size())));
                if (!consume(part))
                    return;
                m_buffer.remove(0, part.size());
                m_remaining -= part.size();
                continue;
            }
            if (m_state == ChunkSize || m_state == Trailers) {
                const int eol = m_buffer.indexOf("\r\n");
                if (eol < 0) {
                    if (m_buffer.size() > 1024)
                        fail(400);
                    return;
                }
                const QByteArray line = m_buffer.left(eol);
                m_buffer.remove(0, eol + 2);
                if (m_state == Trailers) {
                    if (line.isEmpty())
                        finishBody();
                    continue;
                }
                bool ok = false;
                m_remaining = line.split(';').first().trimmed().toLongLong(&ok, 16);
                if (!ok || m_remaining < 0) {
                    fail(400);
                    return;
                }
                m_state = m_remaining == 0 ? Trailers : ChunkData;
                continue;
            }
            if (m_state == ChunkData) {
                if (m_buffer.isEmpty())
                    return;
                const QByteArray part = m_buffer.left(int(qMin<qint64>(m_remaining, m_buffer.size())));
                if (!consume(part))
                    return;
                m_buffer.remove(0, part.size());
                m_remaining -= part.size();
                if (m_remaining == 0)
                    m_state = ChunkCrlf;
                continue;
            }
            if (m_state == ChunkCrlf) {
                if (m_buffer.size() < 2)
                    return;
                if (!m_buffer.startsWith("\r\n")) {
                    fail(400);
                    return;
                }
                m_buffer.remove(0, 2);
                m_state = ChunkSize;
                continue;
            }
            // Done: wait for the answer before reading the next request
            return;
        }
    }

    bool parseHeaders(const QByteArray &block)
    {
        const QList<QByteArray> lines = block.split('\n');
        const QList<QByteArray> first = lines.first().trimmed().split(' ');
        if (first.size() < 3 || !first.at(2).startsWith("HTTP/1."))
            return false;
        if (m_request)
            m_request->deleteLater();
        m_request = new HttpRequest(this);
        m_request->m_connection = this;
        m_request->method = QString::fromLatin1(first.at(0));
        const QUrl url(QString::fromUtf8(first.at(1)));
        m_request->path = url.path();
        m_request->query = QUrlQuery(url.query(QUrl::FullyEncoded));
        m_request->peer = m_socket->peerAddress();
        if (m_request->peer.protocol() == QAbstractSocket::IPv6Protocol) {
            bool ok = false;
            const QHostAddress v4(m_request->peer.toIPv4Address(&ok));
            if (ok)
                m_request->peer = v4;
        }
        m_request->peerFingerprint = m_peerFingerprint;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            m_request->headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
        }
        m_wantClose = m_request->headers.value("connection").toLower() == "close" || first.at(2) == "HTTP/1.0";
        m_received = 0;
        if (m_request->headers.value("transfer-encoding").toLower().contains("chunked")) {
            m_state = ChunkSize;
        } else {
            bool ok = true;
            const QByteArray cl = m_request->headers.value("content-length", "0");
            m_remaining = cl.toLongLong(&ok);
            if (!ok || m_remaining < 0)
                return false;
            m_request->contentLength = m_remaining;
            if (!streamed() && m_remaining > s_maxBody)
                return false;
            m_state = Body;
        }
        return true;
    }

    HttpServer *m_server;
    QTcpSocket *m_socket;
    QTimer m_idle;
    QByteArray m_buffer;
    QPointer<HttpRequest> m_request;
    State m_state = Headers;
    qint64 m_remaining = 0;
    qint64 m_received = 0;
    QString m_peerFingerprint;
    bool m_closing = false;
    bool m_gone = false;
    bool m_wantClose = false;
};

void HttpRequest::setBodySink(QIODevice *sink)
{
    if (m_connection)
        m_connection->setSink(this, sink);
}

void HttpRequest::respond(int status, const QByteArray &body, const QByteArray &contentType)
{
    if (m_connection)
        m_connection->respond(this, status, body, contentType);
}

HttpServer::HttpServer(QObject *parent)
    : QObject(parent)
{
}

HttpServer::~HttpServer() = default;

bool HttpServer::listen(quint16 port, const Identity *identity)
{
    close();
    if (identity) {
        m_server = new QSslServer(this);
        QSslConfiguration config = QSslConfiguration::defaultConfiguration();
        config.setLocalCertificate(identity->certificate());
        config.setPrivateKey(identity->key());
        config.setProtocol(QSsl::TlsV1_2OrLater);
        // Ask senders for their certificate (mTLS, like LocalSend 1.15+) but
        // don't require it: older apps have none. Self-signed is expected.
        config.setPeerVerifyMode(QSslSocket::QueryPeer);
        m_server->setSslConfiguration(config);
        m_server->setHandshakeTimeout(15000);
        connect(m_server, &QSslServer::sslErrors, this, [](QSslSocket *socket, const QList<QSslError> &) {
            socket->ignoreSslErrors();
        });
        connect(m_server, &QTcpServer::pendingConnectionAvailable, this, [this] {
            while (m_server->hasPendingConnections())
                incoming(m_server->nextPendingConnection());
        });
        if (!m_server->listen(QHostAddress::Any, port)) {
            qWarning() << "lingmo-share: cannot listen on port" << port << m_server->errorString();
            close();
            return false;
        }
        return true;
    }
    m_plain = new QTcpServer(this);
    connect(m_plain, &QTcpServer::pendingConnectionAvailable, this, [this] {
        while (m_plain->hasPendingConnections())
            incoming(m_plain->nextPendingConnection());
    });
    if (!m_plain->listen(QHostAddress::Any, port)) {
        close();
        return false;
    }
    return true;
}

void HttpServer::close()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    if (m_plain) {
        m_plain->close();
        m_plain->deleteLater();
        m_plain = nullptr;
    }
    // Open connections are dropped too: nothing stays reachable
    const auto connections = findChildren<HttpConnection *>(Qt::FindDirectChildrenOnly);
    for (HttpConnection *c : connections)
        c->deleteLater();
}

bool HttpServer::isListening() const
{
    return (m_server && m_server->isListening()) || (m_plain && m_plain->isListening());
}

quint16 HttpServer::port() const
{
    if (m_server)
        return m_server->serverPort();
    return m_plain ? m_plain->serverPort() : 0;
}

void HttpServer::incoming(QTcpSocket *socket)
{
    if (!socket)
        return;
    if (m_connections >= s_maxConnections) {
        socket->abort();
        socket->deleteLater();
        return;
    }
    new HttpConnection(this, socket);
}

#include "httpserver.moc"
