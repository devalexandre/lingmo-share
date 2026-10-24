/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SMSMODEL_H
#define SMSMODEL_H

#include <QObject>
#include <QVariantList>

class KdeConnect;

// SMS of the selected phone, read and sent through KDE Connect's conversations
class SmsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY deviceChanged)
    Q_PROPERTY(QVariantList threads READ threads NOTIFY threadsChanged)
    Q_PROPERTY(qint64 thread READ thread NOTIFY threadChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit SmsModel(KdeConnect *kdc, QObject *parent = nullptr);

    QString device() const { return m_device; }
    void setDevice(const QString &id);
    QVariantList threads() const;
    qint64 thread() const { return m_thread; }
    QVariantList messages() const { return m_messages; }
    bool loading() const { return m_loading; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openThread(qint64 thread);
    Q_INVOKABLE void reply(const QString &text);
    Q_INVOKABLE void sendNew(const QString &number, const QString &text);

signals:
    void deviceChanged();
    void threadsChanged();
    void threadChanged();
    void messagesChanged();
    void loadingChanged();

private:
    void onMessage(const QString &id, const QVariantMap &message);
    void setLoading(bool loading);

    KdeConnect *m_kdc;
    QString m_device;
    QHash<qint64, QVariantMap> m_threads; // latest message per thread
    qint64 m_thread = -1;
    QVariantList m_messages;
    bool m_loading = false;
};

#endif
