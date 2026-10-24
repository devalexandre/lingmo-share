/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "localsend.h"
#include "httpserver.h"
#include "../common/safename.h"
#include "../common/shareconfig.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QSslConfiguration>
#include <QStorageInfo>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>

static const QHostAddress s_group(QStringLiteral("224.0.0.167"));
static const QString s_api = QStringLiteral("/api/localsend/v2/");
static const QString s_version = QStringLiteral("2.2");

// Writes the received bytes to the part file and hashes them on the way
class HashingFile : public QFile
{
public:
    explicit HashingFile(const QString &path, QObject *parent)
        : QFile(path, parent)
        , hash(QCryptographicHash::Sha256)
    {
    }
    QCryptographicHash hash;

protected:
    qint64 writeData(const char *data, qint64 len) override
    {
        const qint64 n = QFile::writeData(data, len);
        if (n > 0)
            hash.addData(QByteArrayView(data, n));
        return n;
    }
};

QVariantMap LsDevice::toMap() const
{
    return {{QStringLiteral("id"), fingerprint},
            {QStringLiteral("fingerprint"), fingerprint},
            {QStringLiteral("name"), alias},
            {QStringLiteral("model"), deviceModel},
            {QStringLiteral("deviceType"), deviceType},
            {QStringLiteral("address"), ip.toString()},
            {QStringLiteral("port"), port},
            {QStringLiteral("verified"), verified},
            {QStringLiteral("lingmo"), deviceModel == QLatin1String("Lingmo")}};
}

QUrl LsDevice::url(const QString &route) const
{
    QUrl url;
    url.setScheme(protocol == QLatin1String("http") ? QStringLiteral("http") : QStringLiteral("https"));
    url.setHost(ip.toString());
    url.setPort(port);
    url.setPath(s_api + route);
    return url;
}

static LsDevice deviceFromJson(const QJsonObject &o)
{
    LsDevice d;
    d.alias = o.value(QStringLiteral("alias")).toString().simplified().left(64);
    d.version = o.value(QStringLiteral("version")).toString();
    d.deviceModel = o.value(QStringLiteral("deviceModel")).toString().left(64);
    d.deviceType = o.value(QStringLiteral("deviceType")).toString();
    static const QStringList types = {QStringLiteral("mobile"), QStringLiteral("desktop"), QStringLiteral("web"),
                                      QStringLiteral("headless"), QStringLiteral("server")};
    if (!types.contains(d.deviceType))
        d.deviceType = QStringLiteral("desktop");
    d.fingerprint = o.value(QStringLiteral("fingerprint")).toString().toUpper();
    const int port = o.value(QStringLiteral("port")).toInt(53317);
    d.port = port > 0 && port < 65536 ? quint16(port) : 53317;
    d.protocol = o.value(QStringLiteral("protocol")).toString(QStringLiteral("https"));
    if (d.alias.isEmpty())
        d.alias = QStringLiteral("LocalSend");
    return d;
}

LocalSend::LocalSend(ShareConfig *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    m_announceTimer = new QTimer(this);
    m_announceTimer->setSingleShot(true);
    connect(m_announceTimer, &QTimer::timeout, this, &LocalSend::announce);
}

LocalSend::~LocalSend()
{
    stopSockets();
}

bool LocalSend::init(const QString &dataDir)
{
    if (!m_identity.load(dataDir)) {
        m_error = tr("Could not create the encryption certificate.");
        return false;
    }
    return true;
}

QJsonObject LocalSend::info(bool announce) const
{
    QJsonObject o{{QStringLiteral("alias"), m_config->deviceName()},
                  {QStringLiteral("version"), s_version},
                  {QStringLiteral("deviceModel"), QStringLiteral("Lingmo")},
                  {QStringLiteral("deviceType"), QStringLiteral("desktop")},
                  {QStringLiteral("fingerprint"), m_identity.fingerprint()},
                  {QStringLiteral("port"), m_server ? int(m_server->port()) : m_config->port()},
                  {QStringLiteral("protocol"), QStringLiteral("https")},
                  {QStringLiteral("download"), false}};
    if (announce)
        o.insert(QStringLiteral("announce"), true);
    return o;
}

void LocalSend::apply()
{
    if (m_identity.fingerprint().isEmpty() || !m_config->localSendEnabled()) {
        setMode(m_discovering && m_config->localSendEnabled() ? Mode::DiscoverOnly : Mode::Stopped);
        return;
    }
    if (m_config->visibility() == ShareConfig::Off)
        setMode(m_discovering ? Mode::DiscoverOnly : Mode::Stopped);
    else
        setMode(Mode::Full);
}

void LocalSend::setDiscovering(bool on)
{
    if (m_discovering == on)
        return;
    m_discovering = on;
    apply();
    if (on)
        refresh();
}

void LocalSend::setMode(Mode mode)
{
    if (m_identity.fingerprint().isEmpty())
        mode = Mode::Stopped;
    if (m_mode == mode)
        return;
    const Mode old = m_mode;
    m_mode = mode;
    if (mode == Mode::Stopped) {
        stopSockets();
        // Nothing of the old network picture stays around
        m_devices.clear();
        emit devicesChanged();
    } else if (old == Mode::Stopped) {
        startSockets();
    }
    if (mode != Mode::Full) {
        const QStringList ids = m_incoming.keys();
        for (const QString &id : ids)
            dropSession(id, tr("Receiving was turned off."));
    }
    emit modeChanged();
    if (mode != Mode::Stopped)
        refresh();
}

void LocalSend::startSockets()
{
    m_error.clear();
    m_server = new HttpServer(this);
    m_server->setStreamedPath(s_api + QStringLiteral("upload"));
    connect(m_server, &HttpServer::request, this, &LocalSend::onRequest);
    if (!m_server->listen(quint16(m_config->port()), &m_identity)) {
        m_error = tr("Port %1 is in use by another program.").arg(m_config->port());
        // Sending still works without our server: peers answer announcements over UDP
    }

    m_udp = new QUdpSocket(this);
    if (!m_udp->bind(QHostAddress::AnyIPv4, 53317, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning() << "lingmo-share: multicast bind failed" << m_udp->errorString();
    }
    // Tests run two instances on one host
    m_udp->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
    m_udp->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
    connect(m_udp, &QUdpSocket::readyRead, this, &LocalSend::readMulticast);
    joinGroups();

    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setLocalCertificate(m_identity.certificate());
    ssl.setPrivateKey(m_identity.key());
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_nam = new QNetworkAccessManager(this);
    m_nam->setTransferTimeout(4000);
    m_nam->setProperty("ssl", QVariant::fromValue(ssl));
}

void LocalSend::stopSockets()
{
    m_announceTimer->stop();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    if (m_udp) {
        m_udp->close();
        m_udp->deleteLater();
        m_udp = nullptr;
    }
    if (m_nam) {
        m_nam->deleteLater();
        m_nam = nullptr;
    }
}

static QList<QNetworkInterface> multicastInterfaces()
{
    const bool loopback = qEnvironmentVariableIntValue("LINGMO_SHARE_LOOPBACK") == 1;
    QList<QNetworkInterface> out;
    const auto all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : all) {
        const auto f = iface.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning))
            continue;
        if (loopback != bool(f & QNetworkInterface::IsLoopBack))
            continue;
        if (!loopback && !(f & QNetworkInterface::CanMulticast))
            continue;
        bool hasV4 = false;
        for (const QNetworkAddressEntry &e : iface.addressEntries())
            hasV4 |= e.ip().protocol() == QAbstractSocket::IPv4Protocol;
        if (hasV4)
            out << iface;
    }
    return out;
}

void LocalSend::joinGroups()
{
    if (!m_udp)
        return;
    for (const QNetworkInterface &iface : multicastInterfaces())
        m_udp->joinMulticastGroup(s_group, iface);
}

bool LocalSend::visibleTo(const QString &fingerprint) const
{
    if (m_discovering)
        return true;
    if (m_mode != Mode::Full)
        return false;
    if (m_config->visibility() == ShareConfig::TrustedOnly)
        return m_config->isTrusted(fingerprint);
    return m_config->visibility() == ShareConfig::Everyone;
}

void LocalSend::refresh(bool httpScan)
{
    if (m_mode == Mode::Stopped)
        return;
    // Forget devices nobody heard of in a while
    const QDateTime old = QDateTime::currentDateTimeUtc().addSecs(-600);
    bool changed = false;
    for (auto it = m_devices.begin(); it != m_devices.end();) {
        if (it->lastSeen < old) {
            it = m_devices.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        emit devicesChanged();
    joinGroups();
    m_announceTimer->setProperty("round", 0);
    m_announceTimer->start(100);
    if (httpScan)
        scanSubnets();
}

void LocalSend::announce()
{
    if (!m_udp)
        return;
    // Announcing tells everyone on the network we are here: only when visible to
    // everyone, or while the user is picking a device to send to
    if (m_discovering || (m_mode == Mode::Full && m_config->visibility() == ShareConfig::Everyone)) {
        const QByteArray payload = QJsonDocument(info(true)).toJson(QJsonDocument::Compact);
        const auto ifaces = multicastInterfaces();
        for (const QNetworkInterface &iface : ifaces) {
            m_udp->setMulticastInterface(iface);
            m_udp->writeDatagram(payload, s_group, 53317);
        }
    }
    // Repeated a few times, like the official apps (100 ms, 500 ms, 2 s)
    const int round = m_announceTimer->property("round").toInt() + 1;
    m_announceTimer->setProperty("round", round);
    if (round == 1)
        m_announceTimer->start(400);
    else if (round == 2)
        m_announceTimer->start(1500);
}

void LocalSend::readMulticast()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_udp->receiveDatagram(8192);
        const QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
        if (o.isEmpty() || !o.contains(QStringLiteral("fingerprint")))
            continue;
        LsDevice d = deviceFromJson(o);
        if (d.fingerprint.isEmpty() || d.fingerprint == m_identity.fingerprint())
            continue;
        QHostAddress ip = dg.senderAddress();
        bool ok = false;
        const QHostAddress v4(ip.toIPv4Address(&ok));
        d.ip = ok ? v4 : ip;
        const bool announcement = o.value(QStringLiteral("announce")).toBool() || o.value(QStringLiteral("announcement")).toBool();
        upsert(d);
        if (announcement && visibleTo(d.fingerprint))
            sendRegister(d);
    }
}

// Answer an announcement (or scan an address) with POST /register; the reply
// tells us who is there, proven by the certificate of the connection
void LocalSend::sendRegister(const LsDevice &to)
{
    if (!m_nam)
        return;
    QNetworkRequest req(to.url(QStringLiteral("register")));
    req.setSslConfiguration(m_nam->property("ssl").value<QSslConfiguration>());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(info(false)).toJson(QJsonDocument::Compact));
    const QHostAddress ip = to.ip;
    const quint16 port = to.port;
    const QString protocol = to.protocol;
    const QString claimed = to.fingerprint;
    connect(reply, &QNetworkReply::finished, this, [this, reply, ip, port, protocol, claimed] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            // UDP fallback, as the protocol asks
            if (m_udp && !claimed.isEmpty() && visibleTo(claimed)) {
                const QByteArray payload = QJsonDocument(info(false)).toJson(QJsonDocument::Compact);
                m_udp->writeDatagram(payload, s_group, 53317);
            }
            return;
        }
        LsDevice d = deviceFromJson(QJsonDocument::fromJson(reply->readAll()).object());
        const QSslCertificate cert = reply->sslConfiguration().peerCertificate();
        if (!cert.isNull()) {
            d.fingerprint = Identity::fingerprintOf(cert);
            d.verified = true;
        }
        if (d.fingerprint.isEmpty() || d.fingerprint == m_identity.fingerprint())
            return;
        d.ip = ip;
        d.port = port;
        d.protocol = protocol;
        upsert(d);
    });
}

void LocalSend::scanSubnets()
{
    // HTTP discovery for networks that drop multicast: /24 around each address
    QList<QHostAddress> targets;
    for (const QNetworkInterface &iface : multicastInterfaces()) {
        for (const QNetworkAddressEntry &e : iface.addressEntries()) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol || e.prefixLength() < 16)
                continue;
            const quint32 self = e.ip().toIPv4Address();
            const quint32 base = self & 0xFFFFFF00u;
            for (quint32 host = 1; host < 255; ++host) {
                if ((base | host) != self)
                    targets << QHostAddress(base | host);
            }
        }
    }
    // A handful at a time, so the network isn't flooded
    auto queue = std::make_shared<QList<QHostAddress>>(targets);
    auto next = std::make_shared<std::function<void()>>();
    *next = [this, queue, next] {
        for (int i = 0; i < 24 && !queue->isEmpty(); ++i) {
            LsDevice d;
            d.ip = queue->takeFirst();
            d.port = quint16(m_config->port());
            sendRegister(d);
        }
        if (!queue->isEmpty())
            QTimer::singleShot(300, this, *next);
    };
    (*next)();
}

void LocalSend::upsert(LsDevice d)
{
    d.lastSeen = QDateTime::currentDateTimeUtc();
    auto it = m_devices.find(d.fingerprint);
    if (it != m_devices.end()) {
        // A claimed (UDP) fingerprint never replaces a proven one's address
        if (it->verified && !d.verified) {
            it->lastSeen = d.lastSeen;
            return;
        }
        const bool same = it->alias == d.alias && it->ip == d.ip && it->port == d.port && it->verified == d.verified;
        *it = d;
        if (same)
            return;
    } else {
        m_devices.insert(d.fingerprint, d);
    }
    emit devicesChanged();
}

QList<LsDevice> LocalSend::devices() const
{
    return m_devices.values();
}

bool LocalSend::device(const QString &fingerprint, LsDevice *out) const
{
    auto it = m_devices.constFind(fingerprint.toUpper());
    if (it == m_devices.constEnd())
        return false;
    if (out)
        *out = *it;
    return true;
}

// ---------------------------------------------------------------- server

static QByteArray json(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

static QByteArray message(const QString &text)
{
    return json({{QStringLiteral("message"), text}});
}

void LocalSend::onRequest(HttpRequest *request)
{
    const QString route = request->path.startsWith(s_api) ? request->path.mid(s_api.size()) : QString();
    if (route == QLatin1String("register") && request->method == QLatin1String("POST"))
        handleRegister(request);
    else if (route == QLatin1String("info"))
        request->respond(visibleTo(request->peerFingerprint) ? 200 : 403,
                         visibleTo(request->peerFingerprint) ? json(info(false)) : message(QStringLiteral("Forbidden")));
    else if (route == QLatin1String("prepare-upload") && request->method == QLatin1String("POST"))
        handlePrepareUpload(request);
    else if (route == QLatin1String("upload") && request->method == QLatin1String("POST"))
        handleUpload(request);
    else if (route == QLatin1String("cancel") && request->method == QLatin1String("POST"))
        handleCancel(request);
    else
        request->respond(404, message(QStringLiteral("Not found")));
}

void LocalSend::handleRegister(HttpRequest *request)
{
    const QJsonObject o = QJsonDocument::fromJson(request->body).object();
    if (o.isEmpty()) {
        request->respond(400, message(QStringLiteral("Invalid body")));
        return;
    }
    LsDevice d = deviceFromJson(o);
    // With mTLS the certificate proves who is talking; the JSON is only a claim
    if (!request->peerFingerprint.isEmpty()) {
        if (!d.fingerprint.isEmpty() && d.fingerprint != request->peerFingerprint) {
            request->respond(400, message(QStringLiteral("Fingerprint mismatch")));
            return;
        }
        d.fingerprint = request->peerFingerprint;
        d.verified = true;
    }
    if (d.fingerprint == m_identity.fingerprint()) {
        request->respond(200, json(info(false)));
        return;
    }
    if (!visibleTo(d.fingerprint)) {
        request->respond(403, message(QStringLiteral("Forbidden")));
        return;
    }
    d.ip = request->peer;
    if (!d.fingerprint.isEmpty())
        upsert(d);
    QJsonObject reply = info(false);
    reply.remove(QStringLiteral("port"));
    reply.remove(QStringLiteral("protocol"));
    request->respond(200, json(reply));
}

void LocalSend::handlePrepareUpload(HttpRequest *request)
{
    const QString ip = request->peer.toString();
    if (m_mode != Mode::Full) {
        request->respond(403, message(QStringLiteral("Receiving is off")));
        return;
    }
    // PIN: three wrong tries per address, then 429 (like the reference server)
    const QString pin = m_config->requiredPin();
    if (!pin.isEmpty()) {
        if (m_pinFailures.value(ip) >= 3) {
            request->respond(429, message(QStringLiteral("Too many requests")));
            return;
        }
        const QString given = request->query.queryItemValue(QStringLiteral("pin"), QUrl::FullyDecoded);
        if (given.isEmpty()) {
            request->respond(401, message(QStringLiteral("PIN required")));
            return;
        }
        if (given != pin) {
            // Three wrong tries lock that address out for five minutes
            if (++m_pinFailures[ip] == 3)
                QTimer::singleShot(5 * 60 * 1000, this, [this, ip] { m_pinFailures.remove(ip); });
            request->respond(401, message(QStringLiteral("Invalid PIN")));
            return;
        }
        m_pinFailures.remove(ip);
    }

    const QJsonObject body = QJsonDocument::fromJson(request->body).object();
    const QJsonObject infoObj = body.value(QStringLiteral("info")).toObject();
    const QJsonObject files = body.value(QStringLiteral("files")).toObject();
    if (infoObj.isEmpty() || files.isEmpty() || files.size() > 10000) {
        request->respond(400, message(QStringLiteral("Invalid body")));
        return;
    }

    LsDevice sender = deviceFromJson(infoObj);
    sender.ip = request->peer;
    if (!request->peerFingerprint.isEmpty()) {
        sender.fingerprint = request->peerFingerprint;
        sender.verified = true;
    }
    // Only a fingerprint proven by the certificate can be trusted
    const bool trusted = sender.verified && m_config->isTrusted(sender.fingerprint);
    if (m_config->visibility() == ShareConfig::TrustedOnly && !trusted) {
        request->respond(403, message(QStringLiteral("Rejected")));
        return;
    }

    // One session at a time; a stale one (sender vanished) gives way
    if (!m_activeSession.isEmpty()) {
        const Incoming &active = m_incoming.value(m_activeSession);
        const bool stale = active.created.secsTo(QDateTime::currentDateTimeUtc()) > 600 && active.done == 0;
        if (m_incoming.contains(m_activeSession) && !stale) {
            request->respond(409, message(QStringLiteral("Blocked by another session")));
            return;
        }
        dropSession(m_activeSession, tr("The transfer was replaced by a new one."));
    }

    Incoming s;
    s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    s.sender = sender;
    s.created = QDateTime::currentDateTimeUtc();
    s.folder = m_config->receiveFolder();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonObject f = it.value().toObject();
        IncomingFile file;
        file.id = it.key();
        file.fileName = f.value(QStringLiteral("fileName")).toString();
        file.size = qint64(f.value(QStringLiteral("size")).toDouble());
        file.fileType = f.value(QStringLiteral("fileType")).toString();
        file.sha256 = f.value(QStringLiteral("sha256")).toString().toLower();
        file.preview = f.value(QStringLiteral("preview")).toString();
        file.modified = QDateTime::fromString(f.value(QStringLiteral("metadata")).toObject()
                                                  .value(QStringLiteral("modified")).toString(), Qt::ISODateWithMs);
        if (file.id.isEmpty() || file.size < 0) {
            request->respond(400, message(QStringLiteral("Invalid body")));
            return;
        }
        s.total += file.size;
        s.files.insert(file.id, file);
        s.order << file.id;
    }
    // A text shared as a message: one text file whose content is the preview
    if (s.files.size() == 1) {
        const IncomingFile &f = s.files.begin().value();
        s.message = f.fileType.startsWith(QLatin1String("text/")) && !f.preview.isEmpty()
                    && f.preview.toUtf8().size() == f.size;
    }
    if (!s.message) {
        const QStorageInfo storage(s.folder);
        if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < s.total + 50 * 1024 * 1024) {
            request->respond(500, message(QStringLiteral("Not enough space")));
            return;
        }
    }

    s.pending = request;
    m_activeSession = s.id;
    const QString id = s.id;
    m_incoming.insert(id, s);
    connect(request, &HttpRequest::aborted, this, [this, id] {
        auto it = m_incoming.find(id);
        if (it != m_incoming.end() && !it->accepted) {
            dropSession(id, tr("The sender cancelled."));
            emit incomingWithdrawn(id);
        }
    });
    // Nobody answered: decline after five minutes
    QTimer::singleShot(5 * 60 * 1000, this, [this, id] {
        auto it = m_incoming.find(id);
        if (it != m_incoming.end() && !it->accepted) {
            decline(id);
            emit incomingWithdrawn(id);
        }
    });

    if (trusted)
        accept(id, false);
    else
        emit incomingRequest(id);
}

QVariantMap LocalSend::incomingInfo(const QString &sessionId) const
{
    auto it = m_incoming.constFind(sessionId);
    if (it == m_incoming.constEnd())
        return {};
    QVariantList files;
    for (const QString &fid : it->order) {
        const IncomingFile &f = it->files.value(fid);
        files << QVariantMap{{QStringLiteral("name"), SafeName::sanitize(f.fileName)},
                             {QStringLiteral("size"), f.size},
                             {QStringLiteral("type"), f.fileType}};
    }
    QVariantMap m = it->sender.toMap();
    m.insert(QStringLiteral("session"), it->id);
    m.insert(QStringLiteral("files"), files);
    m.insert(QStringLiteral("fileCount"), files.size());
    m.insert(QStringLiteral("total"), it->total);
    m.insert(QStringLiteral("message"), it->message ? it->files.begin().value().preview : QString());
    m.insert(QStringLiteral("trusted"), it->sender.verified && m_config->isTrusted(it->sender.fingerprint));
    m.insert(QStringLiteral("folder"), it->folder);
    m.insert(QStringLiteral("protocol"), QStringLiteral("localsend"));
    return m;
}

void LocalSend::accept(const QString &sessionId, bool trustSender)
{
    auto it = m_incoming.find(sessionId);
    if (it == m_incoming.end() || it->accepted)
        return;
    it->accepted = true;
    // Trust needs a fingerprint proven by the certificate
    if (trustSender && it->sender.verified)
        m_config->trust(it->sender.fingerprint, it->sender.alias);
    answerPrepare(*it);
}

void LocalSend::answerPrepare(Incoming &s)
{
    QPointer<HttpRequest> request = s.pending;
    s.pending = nullptr;
    if (!request) {
        dropSession(s.id, tr("The sender cancelled."));
        return;
    }
    if (s.message) {
        const QString id = s.id, alias = s.sender.alias, text = s.files.begin().value().preview;
        request->respond(204);
        emit messageReceived(id, alias, text);
        m_incoming.remove(id);
        m_activeSession.clear();
        emit incomingFinished(id, {}, true, QString());
        return;
    }
    QJsonObject tokens;
    for (auto fit = s.files.begin(); fit != s.files.end(); ++fit) {
        fit->token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        tokens.insert(fit->id, fit->token);
    }
    request->respond(200, json({{QStringLiteral("sessionId"), s.id}, {QStringLiteral("files"), tokens}}));
    emit incomingStarted(s.id);
}

void LocalSend::decline(const QString &sessionId)
{
    auto it = m_incoming.find(sessionId);
    if (it == m_incoming.end())
        return;
    if (it->pending)
        it->pending->respond(403, message(QStringLiteral("Rejected")));
    m_incoming.erase(it);
    if (m_activeSession == sessionId)
        m_activeSession.clear();
}

void LocalSend::cancelIncoming(const QString &sessionId)
{
    if (!m_incoming.contains(sessionId))
        return;
    dropSession(sessionId, tr("Cancelled."));
}

void LocalSend::dropSession(const QString &sessionId, const QString &error)
{
    auto it = m_incoming.find(sessionId);
    if (it == m_incoming.end())
        return;
    if (it->pending)
        it->pending->respond(403, message(QStringLiteral("Rejected")));
    const bool started = it->accepted;
    // Half-received files never stay behind
    for (const IncomingFile &f : std::as_const(it->files)) {
        if (!f.partPath.isEmpty())
            QFile::remove(f.partPath);
    }
    QStringList saved;
    for (const IncomingFile &f : std::as_const(it->files)) {
        if (f.state == IncomingFile::Done)
            saved << f.target;
    }
    m_incoming.erase(it);
    if (m_activeSession == sessionId)
        m_activeSession.clear();
    if (started)
        emit incomingFinished(sessionId, saved, false, error);
}

void LocalSend::handleUpload(HttpRequest *request)
{
    const QString sessionId = request->query.queryItemValue(QStringLiteral("sessionId"), QUrl::FullyDecoded);
    const QString fileId = request->query.queryItemValue(QStringLiteral("fileId"), QUrl::FullyDecoded);
    const QString token = request->query.queryItemValue(QStringLiteral("token"), QUrl::FullyDecoded);
    if (sessionId.isEmpty() || fileId.isEmpty() || token.isEmpty()) {
        request->respond(400, message(QStringLiteral("Missing parameters")));
        return;
    }
    auto it = m_incoming.find(sessionId);
    if (it == m_incoming.end() || !it->accepted || it->sender.ip != request->peer || !it->files.contains(fileId)) {
        request->respond(403, message(QStringLiteral("Invalid token or IP address")));
        return;
    }
    IncomingFile &file = it->files[fileId];
    if (file.token.isEmpty() || file.token != token || file.state != IncomingFile::Pending) {
        request->respond(403, message(QStringLiteral("Invalid token or IP address")));
        return;
    }
    if (request->contentLength > file.size) {
        request->respond(400, message(QStringLiteral("Size mismatch")));
        return;
    }

    file.target = SafeName::uniqueTarget(it->folder, file.fileName);
    if (file.target.isEmpty()) {
        request->respond(500, message(QStringLiteral("Invalid file name")));
        fileDone(sessionId, fileId, false);
        return;
    }
    // Hidden part file next to the target; renamed when complete and verified
    const QFileInfo target(file.target);
    file.partPath = target.absolutePath() + QStringLiteral("/.") + target.fileName() + QStringLiteral(".lingmo-share-part");
    auto *sink = new HashingFile(file.partPath, request);
    // NewOnly: O_EXCL, never follows a planted symlink
    if (!sink->open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        QFile::remove(file.partPath);
        if (!sink->open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            file.partPath.clear();
            request->respond(500, message(QStringLiteral("Cannot write")));
            fileDone(sessionId, fileId, false);
            return;
        }
    }
    sink->setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.state = IncomingFile::Receiving;

    QPointer<HttpRequest> req = request;
    const qint64 size = file.size;
    connect(request, &HttpRequest::bodyProgress, this, [this, req, sessionId, fileId, size](qint64 received) {
        auto it = m_incoming.find(sessionId);
        if (it == m_incoming.end())
            return;
        IncomingFile &f = it->files[fileId];
        if (received > size && req) {
            if (!f.partPath.isEmpty())
                QFile::remove(f.partPath);
            f.partPath.clear();
            req->respond(400, message(QStringLiteral("Size mismatch")));
            fileDone(sessionId, fileId, false);
            return;
        }
        it->done += received - f.received;
        f.received = received;
        emit incomingProgress(sessionId, it->done, it->total);
    });
    connect(request, &HttpRequest::bodyFinished, this, [this, req, sink, sessionId, fileId](bool complete) {
        sink->close();
        auto it = m_incoming.find(sessionId);
        if (it == m_incoming.end()) {
            sink->remove();
            return;
        }
        IncomingFile &f = it->files[fileId];
        if (f.state != IncomingFile::Receiving)
            return;
        int status = 200;
        if (!complete || f.received != f.size)
            status = 400;
        else if (!f.sha256.isEmpty() && QString::fromLatin1(sink->hash.result().toHex()) != f.sha256)
            status = 422;
        if (status == 200) {
            // The name may have been taken meanwhile: pick again, never overwrite
            if (QFileInfo::exists(f.target))
                f.target = SafeName::uniqueTarget(it->folder, f.fileName);
            if (f.target.isEmpty() || !QFile::rename(f.partPath, f.target))
                status = 500;
            else {
                QFile::setPermissions(f.target, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
                // Keep the sender's modification time, like the official apps
                if (f.modified.isValid()) {
                    QFile saved(f.target);
                    if (saved.open(QIODevice::ReadWrite))
                        saved.setFileTime(f.modified, QFileDevice::FileModificationTime);
                }
            }
        }
        if (status != 200)
            QFile::remove(f.partPath);
        f.partPath.clear();
        if (req)
            req->respond(status, status == 200 ? QByteArray() : message(QStringLiteral("Upload failed")));
        fileDone(sessionId, fileId, status == 200);
    });
    connect(request, &HttpRequest::aborted, this, [this, sessionId, fileId] {
        auto it = m_incoming.find(sessionId);
        if (it == m_incoming.end())
            return;
        IncomingFile &f = it->files[fileId];
        if (f.state == IncomingFile::Receiving) {
            if (!f.partPath.isEmpty())
                QFile::remove(f.partPath);
            f.partPath.clear();
            fileDone(sessionId, fileId, false);
        }
    });
    request->setBodySink(sink);
    emit incomingProgress(sessionId, it->done, it->total);
}

void LocalSend::fileDone(const QString &sessionId, const QString &fileId, bool ok)
{
    auto it = m_incoming.find(sessionId);
    if (it == m_incoming.end())
        return;
    IncomingFile &f = it->files[fileId];
    f.state = ok ? IncomingFile::Done : IncomingFile::Failed;
    if (!ok)
        it->done -= f.received;
    finishIfComplete(sessionId);
}

void LocalSend::finishIfComplete(const QString &sessionId)
{
    auto it = m_incoming.find(sessionId);
    if (it == m_incoming.end())
        return;
    QStringList saved;
    bool allOk = true;
    for (const IncomingFile &f : std::as_const(it->files)) {
        if (f.state == IncomingFile::Pending || f.state == IncomingFile::Receiving)
            return;
        if (f.state == IncomingFile::Done)
            saved << f.target;
        else
            allOk = false;
    }
    m_incoming.erase(it);
    if (m_activeSession == sessionId)
        m_activeSession.clear();
    emit incomingFinished(sessionId, saved, allOk, allOk ? QString() : tr("Some files could not be received."));
}

void LocalSend::handleCancel(HttpRequest *request)
{
    QString sessionId = request->query.queryItemValue(QStringLiteral("sessionId"), QUrl::FullyDecoded);
    // Without a session id (the official app cancelling while we still ask the
    // user): the request from that address that is still waiting
    if (sessionId.isEmpty()) {
        for (auto w = m_incoming.cbegin(); w != m_incoming.cend(); ++w) {
            if (!w->accepted && w->sender.ip == request->peer)
                sessionId = w.key();
        }
    }
    auto it = m_incoming.find(sessionId);
    if (it == m_incoming.end() || it->sender.ip != request->peer) {
        request->respond(200);
        return;
    }
    const bool waiting = !it->accepted;
    dropSession(sessionId, tr("The sender cancelled."));
    if (waiting)
        emit incomingWithdrawn(sessionId);
    request->respond(200);
}
