/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOCALSEND_H
#define LOCALSEND_H

#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include "identity.h"

class QFile;
class QNetworkAccessManager;
class QTimer;
class QUdpSocket;
class HttpRequest;
class HttpServer;
class ShareConfig;

// A LocalSend peer seen on the network
struct LsDevice {
    QString fingerprint;
    QString alias;
    QString version;
    QString deviceModel;
    QString deviceType; // mobile | desktop | web | headless | server
    QHostAddress ip;
    quint16 port = 53317;
    QString protocol = QStringLiteral("https");
    bool verified = false; // fingerprint proven by the TLS certificate
    QDateTime lastSeen;

    QVariantMap toMap() const;
    QUrl url(const QString &route) const;
};

/*
 * LocalSend protocol v2 (https://github.com/localsend/protocol): multicast
 * discovery on 224.0.0.167:53317, the HTTP "register" fallback, and receiving
 * files (prepare-upload / upload / cancel) over HTTPS with our self-signed
 * certificate. Sending is LocalSendSender.
 *
 * Exposure follows ShareConfig::visibility:
 *  - Everyone: announced and reachable; unknown senders are asked about.
 *  - TrustedOnly: no announcements, and only trusted devices (by certificate
 *    fingerprint) are answered; everything else gets 403.
 *  - Off: no socket at all. While the user is picking a device to send to,
 *    setDiscovering(true) opens discovery only (no receiving) for that time.
 */
class LocalSend : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Stopped, DiscoverOnly, Full };

    LocalSend(ShareConfig *config, QObject *parent = nullptr);
    ~LocalSend() override;

    bool init(const QString &dataDir);
    const Identity &identity() const { return m_identity; }
    Mode mode() const { return m_mode; }
    QString error() const { return m_error; }

    // Applies the settings (call when they change)
    void apply();
    // A send picker is open: discover even when not visible
    void setDiscovering(bool on);
    // Announce now (and later again), and optionally scan the local subnets over HTTP
    void refresh(bool httpScan = false);

    QList<LsDevice> devices() const;
    bool device(const QString &fingerprint, LsDevice *out) const;
    // Our own register/announce payload
    QJsonObject info(bool announce) const;

    // Incoming sessions waiting for an answer
    QVariantMap incomingInfo(const QString &sessionId) const;
    void accept(const QString &sessionId, bool trustSender);
    void decline(const QString &sessionId);
    void cancelIncoming(const QString &sessionId);

signals:
    void devicesChanged();
    void modeChanged();
    void incomingRequest(const QString &sessionId);
    void incomingStarted(const QString &sessionId);
    void incomingProgress(const QString &sessionId, qint64 done, qint64 total);
    void incomingFinished(const QString &sessionId, const QStringList &paths, bool ok, const QString &error);
    // A text sent as a message (LocalSend "text" share): never written to disk
    void messageReceived(const QString &sessionId, const QString &alias, const QString &text);
    // The request went away before the user answered (sender cancelled or timed out)
    void incomingWithdrawn(const QString &sessionId);

private:
    struct IncomingFile {
        QString id;
        QString fileName;
        qint64 size = 0;
        QString fileType;
        QString sha256;
        QString preview;
        QDateTime modified;
        QString token;
        enum State { Pending, Receiving, Done, Failed, Skipped } state = Pending;
        QString target;
        QString partPath;
        qint64 received = 0;
    };
    struct Incoming {
        QString id;
        LsDevice sender;
        QHash<QString, IncomingFile> files;
        QStringList order;
        QPointer<HttpRequest> pending; // prepare-upload waiting for the user
        bool accepted = false;
        bool message = false;
        QString folder;
        QDateTime created;
        qint64 total = 0;
        qint64 done = 0;
    };

    void setMode(Mode mode);
    void startSockets();
    void stopSockets();
    void joinGroups();
    void readMulticast();
    void announce();
    void sendRegister(const LsDevice &to);
    void scanSubnets();
    void upsert(LsDevice device);
    bool visibleTo(const QString &fingerprint) const;

    void onRequest(HttpRequest *request);
    void handleRegister(HttpRequest *request);
    void handlePrepareUpload(HttpRequest *request);
    void handleUpload(HttpRequest *request);
    void handleCancel(HttpRequest *request);
    void answerPrepare(Incoming &session);
    void fileDone(const QString &sessionId, const QString &fileId, bool ok);
    void finishIfComplete(const QString &sessionId);
    void dropSession(const QString &sessionId, const QString &error);

    ShareConfig *m_config;
    Identity m_identity;
    Mode m_mode = Mode::Stopped;
    bool m_discovering = false;
    QString m_error;

    HttpServer *m_server = nullptr;
    QUdpSocket *m_udp = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QTimer *m_announceTimer = nullptr;
    QHash<QString, LsDevice> m_devices;
    QHash<QString, Incoming> m_incoming;
    QString m_activeSession; // LocalSend receives one session at a time
    QHash<QString, int> m_pinFailures; // per IP
};

#endif
