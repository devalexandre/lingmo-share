/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "smsmodel.h"
#include "kdeconnect/kdeconnect.h"

#include <QDateTime>
#include <QTimer>

SmsModel::SmsModel(KdeConnect *kdc, QObject *parent)
    : QObject(parent)
    , m_kdc(kdc)
{
    connect(kdc, &KdeConnect::smsMessage, this, &SmsModel::onMessage);
    connect(kdc, &KdeConnect::conversationLoaded, this, [this](const QString &id, qint64 thread) {
        if (id == m_device && thread == m_thread)
            setLoading(false);
    });
}

void SmsModel::setDevice(const QString &id)
{
    const QString plain = id.startsWith(QLatin1String("kdc:")) ? id.mid(4) : id;
    if (plain == m_device)
        return;
    m_device = plain;
    m_threads.clear();
    m_thread = -1;
    m_messages.clear();
    emit deviceChanged();
    emit threadsChanged();
    emit threadChanged();
    emit messagesChanged();
    refresh();
}

void SmsModel::setLoading(bool loading)
{
    if (m_loading != loading) {
        m_loading = loading;
        emit loadingChanged();
    }
}

QVariantList SmsModel::threads() const
{
    QList<QVariantMap> list = m_threads.values();
    std::sort(list.begin(), list.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("date")).toLongLong() > b.value(QStringLiteral("date")).toLongLong();
    });
    QVariantList out;
    for (const QVariantMap &m : list)
        out << m;
    return out;
}

void SmsModel::refresh()
{
    if (m_device.isEmpty())
        return;
    for (const QVariant &v : m_kdc->conversations(m_device)) {
        const QVariantMap m = v.toMap();
        m_threads.insert(m.value(QStringLiteral("thread")).toLongLong(), m);
    }
    emit threadsChanged();
    // Ask the phone for the newest threads; they arrive as signals
    m_kdc->requestConversations(m_device);
}

void SmsModel::openThread(qint64 thread)
{
    m_thread = thread;
    m_messages.clear();
    emit threadChanged();
    emit messagesChanged();
    if (thread < 0)
        return;
    setLoading(true);
    m_kdc->requestConversation(m_device, thread, 50);
    QTimer::singleShot(8000, this, [this] { setLoading(false); });
}

void SmsModel::onMessage(const QString &id, const QVariantMap &message)
{
    if (id != m_device)
        return;
    const qint64 thread = message.value(QStringLiteral("thread")).toLongLong();
    const QVariantMap latest = m_threads.value(thread);
    if (latest.isEmpty() || latest.value(QStringLiteral("date")).toLongLong() <= message.value(QStringLiteral("date")).toLongLong()) {
        m_threads.insert(thread, message);
        emit threadsChanged();
    }
    if (thread != m_thread)
        return;
    // Keep them in date order, without duplicates
    const qint64 uid = message.value(QStringLiteral("uid")).toLongLong();
    for (const QVariant &v : std::as_const(m_messages)) {
        if (v.toMap().value(QStringLiteral("uid")).toLongLong() == uid)
            return;
    }
    int pos = m_messages.size();
    const qint64 date = message.value(QStringLiteral("date")).toLongLong();
    while (pos > 0 && m_messages.at(pos - 1).toMap().value(QStringLiteral("date")).toLongLong() > date)
        --pos;
    m_messages.insert(pos, message);
    emit messagesChanged();
}

void SmsModel::reply(const QString &text)
{
    if (m_device.isEmpty() || m_thread < 0 || text.trimmed().isEmpty())
        return;
    m_kdc->replyToConversation(m_device, m_thread, text);
}

void SmsModel::sendNew(const QString &number, const QString &text)
{
    if (m_device.isEmpty() || number.trimmed().isEmpty() || text.trimmed().isEmpty())
        return;
    m_kdc->sendSms(m_device, number.trimmed(), text);
}
