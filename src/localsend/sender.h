/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOCALSEND_SENDER_H
#define LOCALSEND_SENDER_H

#include <QJsonObject>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include "localsend.h"

class QNetworkAccessManager;
class QNetworkReply;

// A file to send: where it is, and the name the receiver sees ("Album/a.jpg")
struct OutgoingFile {
    QString path;
    QString name;
    qint64 size = 0;
    QString mime;
};

/*
 * Sends files (or a text) to one LocalSend device: prepare-upload, then one
 * upload per file, over HTTPS pinned to the certificate fingerprint the device
 * was discovered with. Presents our own certificate (mTLS) so the receiver can
 * recognise us as a trusted device.
 */
class LocalSendSender : public QObject
{
    Q_OBJECT
public:
    enum State { Waiting, NeedPin, Sending, Done, Declined, Failed, Cancelled };
    Q_ENUM(State)

    LocalSendSender(const LsDevice &target, const Identity &identity, const QJsonObject &info, QObject *parent = nullptr);

    // Expands folders into their files (keeping the folder name)
    static QList<OutgoingFile> collect(const QStringList &paths);

    void sendFiles(const QList<OutgoingFile> &files);
    void sendText(const QString &text);
    void setPin(const QString &pin); // retries after NeedPin
    void cancel();

    State state() const { return m_state; }
    QString errorString() const { return m_error; }
    qint64 total() const { return m_total; }
    const LsDevice &target() const { return m_target; }
    const QList<OutgoingFile> &files() const { return m_files; }

signals:
    void stateChanged(State state);
    void progress(qint64 done, qint64 total);

private:
    void prepare();
    void uploadNext();
    void setState(State state, const QString &error = {});
    QNetworkRequest request(const QString &route, const QList<QPair<QString, QString>> &query) const;
    bool pinned(QNetworkReply *reply);

    LsDevice m_target;
    QJsonObject m_info;
    QNetworkAccessManager *m_nam;
    QList<OutgoingFile> m_files;
    QString m_text;
    QString m_pin;
    QString m_sessionId;
    QHash<QString, QString> m_tokens; // file id -> token
    QStringList m_queue;              // file ids still to upload
    QPointer<QNetworkReply> m_reply;
    qint64 m_total = 0;
    qint64 m_done = 0;
    State m_state = Waiting;
    QString m_error;
};

#endif
