/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sender.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslConfiguration>
#include <QUrlQuery>
#include <QUuid>

LocalSendSender::LocalSendSender(const LsDevice &target, const Identity &identity, const QJsonObject &info, QObject *parent)
    : QObject(parent)
    , m_target(target)
    , m_info(info)
    , m_nam(new QNetworkAccessManager(this))
{
    m_info.remove(QStringLiteral("announce"));
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setLocalCertificate(identity.certificate());
    ssl.setPrivateKey(identity.key());
    // Self-signed by design: the pin below checks the certificate instead of a CA
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_nam->setProperty("ssl", QVariant::fromValue(ssl));
}

QList<OutgoingFile> LocalSendSender::collect(const QStringList &paths)
{
    QMimeDatabase mimes;
    QList<OutgoingFile> out;
    auto add = [&](const QString &path, const QString &name) {
        const QFileInfo info(path);
        if (!info.isFile() || !info.isReadable())
            return;
        out << OutgoingFile{info.absoluteFilePath(), name, info.size(), mimes.mimeTypeForFile(info).name()};
    };
    for (QString path : paths) {
        if (path.startsWith(QLatin1String("file://")))
            path = QUrl(path).toLocalFile();
        const QFileInfo info(path);
        if (info.isDir()) {
            const QDir base = info.absoluteDir();
            QDirIterator it(info.absoluteFilePath(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString file = it.next();
                if (QFileInfo(file).isSymLink())
                    continue;
                add(file, base.relativeFilePath(file));
            }
        } else {
            add(path, info.fileName());
        }
    }
    return out;
}

QNetworkRequest LocalSendSender::request(const QString &route, const QList<QPair<QString, QString>> &query) const
{
    QUrl url = m_target.url(route);
    QUrlQuery q;
    for (const auto &item : query)
        q.addQueryItem(item.first, QString::fromLatin1(QUrl::toPercentEncoding(item.second)));
    if (!q.isEmpty())
        url.setQuery(q);
    QNetworkRequest req(url);
    req.setSslConfiguration(m_nam->property("ssl").value<QSslConfiguration>());
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    return req;
}

// The receiver must present the certificate it was discovered with
bool LocalSendSender::pinned(QNetworkReply *reply)
{
    if (m_target.protocol == QLatin1String("http"))
        return true;
    const QSslCertificate cert = reply->sslConfiguration().peerCertificate();
    if (cert.isNull())
        return false;
    return Identity::fingerprintOf(cert) == m_target.fingerprint.toUpper();
}

void LocalSendSender::sendFiles(const QList<OutgoingFile> &files)
{
    m_files = files;
    m_total = 0;
    for (const OutgoingFile &f : files)
        m_total += f.size;
    if (m_files.isEmpty()) {
        setState(Failed, tr("Nothing to send."));
        return;
    }
    prepare();
}

void LocalSendSender::sendText(const QString &text)
{
    m_text = text;
    m_total = text.toUtf8().size();
    prepare();
}

void LocalSendSender::setPin(const QString &pin)
{
    m_pin = pin;
    if (m_state == NeedPin)
        prepare();
}

void LocalSendSender::prepare()
{
    QJsonObject files;
    m_tokens.clear();
    QStringList ids;
    if (!m_text.isNull()) {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        files.insert(id, QJsonObject{{QStringLiteral("id"), id},
                                     {QStringLiteral("fileName"), id + QStringLiteral(".txt")},
                                     {QStringLiteral("size"), double(m_text.toUtf8().size())},
                                     {QStringLiteral("fileType"), QStringLiteral("text/plain")},
                                     {QStringLiteral("preview"), m_text}});
    } else {
        for (int i = 0; i < m_files.size(); ++i) {
            const OutgoingFile &f = m_files.at(i);
            const QString id = QString::number(i);
            const QFileInfo info(f.path);
            files.insert(id, QJsonObject{{QStringLiteral("id"), id},
                                         {QStringLiteral("fileName"), f.name},
                                         {QStringLiteral("size"), double(f.size)},
                                         {QStringLiteral("fileType"), f.mime},
                                         {QStringLiteral("metadata"), QJsonObject{
                                              {QStringLiteral("modified"), info.lastModified().toUTC().toString(Qt::ISODate)},
                                              {QStringLiteral("accessed"), info.lastRead().toUTC().toString(Qt::ISODate)}}}});
            ids << id;
        }
    }
    QList<QPair<QString, QString>> query;
    if (!m_pin.isEmpty())
        query << qMakePair(QStringLiteral("pin"), m_pin);
    QNetworkRequest req = request(QStringLiteral("prepare-upload"), query);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("info"), m_info}, {QStringLiteral("files"), files}})
                                .toJson(QJsonDocument::Compact);
    setState(Waiting);
    m_reply = m_nam->post(req, body);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::encrypted, this, [this, reply] {
        if (!pinned(reply)) {
            setState(Failed, tr("The device's identity changed. Look for it again before sending."));
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, ids] {
        reply->deleteLater();
        if (m_state != Waiting)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0 || reply->error() == QNetworkReply::OperationCanceledError) {
            setState(Failed, tr("Could not reach %1.").arg(m_target.alias));
            return;
        }
        if (!pinned(reply)) {
            setState(Failed, tr("The device's identity changed. Look for it again before sending."));
            return;
        }
        switch (status) {
        case 200: {
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            m_sessionId = o.value(QStringLiteral("sessionId")).toString();
            const QJsonObject tokens = o.value(QStringLiteral("files")).toObject();
            m_queue.clear();
            for (const QString &id : ids) {
                if (tokens.contains(id)) {
                    m_tokens.insert(id, tokens.value(id).toString());
                    m_queue << id;
                }
            }
            // Files the receiver skipped don't count
            m_total = 0;
            for (const QString &id : std::as_const(m_queue))
                m_total += m_files.at(id.toInt()).size;
            setState(Sending);
            uploadNext();
            return;
        }
        case 204:
            m_done = m_total;
            emit progress(m_done, m_total);
            setState(Done);
            return;
        case 401:
            setState(NeedPin, m_pin.isEmpty() ? tr("%1 asks for a PIN.").arg(m_target.alias) : tr("Wrong PIN."));
            return;
        case 403:
            setState(Declined, tr("%1 declined.").arg(m_target.alias));
            return;
        case 409:
            setState(Failed, tr("%1 is busy with another transfer.").arg(m_target.alias));
            return;
        case 429:
            setState(Failed, tr("Too many wrong PINs. Try again later."));
            return;
        default:
            setState(Failed, tr("%1 could not receive the files (error %2).").arg(m_target.alias).arg(status));
        }
    });
}

void LocalSendSender::uploadNext()
{
    if (m_state != Sending)
        return;
    if (m_queue.isEmpty()) {
        setState(Done);
        return;
    }
    const QString id = m_queue.takeFirst();
    const OutgoingFile &f = m_files.at(id.toInt());
    auto *file = new QFile(f.path, this);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        setState(Failed, tr("Could not read %1.").arg(f.name));
        cancel();
        return;
    }
    QNetworkRequest req = request(QStringLiteral("upload"), {qMakePair(QStringLiteral("sessionId"), m_sessionId),
                                                             qMakePair(QStringLiteral("fileId"), id),
                                                             qMakePair(QStringLiteral("token"), m_tokens.value(id))});
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    req.setHeader(QNetworkRequest::ContentLengthHeader, file->size());
    m_reply = m_nam->post(req, file);
    QNetworkReply *reply = m_reply;
    file->setParent(reply);
    const qint64 base = m_done;
    connect(reply, &QNetworkReply::encrypted, this, [this, reply] {
        if (!pinned(reply)) {
            setState(Failed, tr("The device's identity changed. Look for it again before sending."));
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::uploadProgress, this, [this, base](qint64 sent, qint64) {
        m_done = base + sent;
        emit progress(m_done, m_total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, base, f] {
        reply->deleteLater();
        if (m_state != Sending)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            setState(Failed, status == 0 ? tr("The connection to %1 was lost.").arg(m_target.alias)
                                         : tr("%1 could not save %2.").arg(m_target.alias, f.name));
            return;
        }
        m_done = base + f.size;
        emit progress(m_done, m_total);
        uploadNext();
    });
}

void LocalSendSender::cancel()
{
    const bool active = m_state == Waiting || m_state == Sending || m_state == NeedPin;
    if (m_reply)
        m_reply->abort();
    // Without a session yet (the receiver is still asking its user) the cancel
    // goes without one, like the official apps do
    if (active) {
        QList<QPair<QString, QString>> query;
        if (!m_sessionId.isEmpty())
            query << qMakePair(QStringLiteral("sessionId"), m_sessionId);
        QNetworkReply *r = m_nam->post(request(QStringLiteral("cancel"), query), QByteArray());
        connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
    }
    if (active)
        setState(Cancelled);
}

void LocalSendSender::setState(State state, const QString &error)
{
    if (!error.isEmpty() || state == Waiting || state == Sending || state == Done)
        m_error = error;
    if (m_state == state && error.isEmpty())
        return;
    m_state = state;
    emit stateChanged(state);
}
