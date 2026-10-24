/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "feedback.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>

static const QString s_island = QStringLiteral("com.lingmo.Island");
static const QString s_islandPath = QStringLiteral("/Island");
static const QString s_notifications = QStringLiteral("org.freedesktop.Notifications");
static const QString s_notificationsPath = QStringLiteral("/org/freedesktop/Notifications");

Feedback::Feedback(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(s_island, s_islandPath, s_island, QStringLiteral("ActionInvoked"), this, SLOT(onIslandAction(uint,QString)));
    bus.connect(s_island, s_islandPath, s_island, QStringLiteral("Closed"), this, SLOT(onIslandClosed(uint)));
    bus.connect(s_notifications, s_notificationsPath, s_notifications, QStringLiteral("ActionInvoked"),
                this, SLOT(onNotificationAction(uint,QString)));
    bus.connect(s_notifications, s_notificationsPath, s_notifications, QStringLiteral("NotificationClosed"),
                this, SLOT(onNotificationClosed(uint,uint)));
}

bool Feedback::islandAvailable() const
{
    return QDBusConnection::sessionBus().interface()->isServiceRegistered(s_island);
}

void Feedback::start(const QString &key, const QString &title, const QString &subtitle, const QString &icon, qreal progress)
{
    Activity a;
    a.title = title;
    a.icon = icon;
    a.viaIsland = islandAvailable();
    m_activities.insert(key, a);

    if (a.viaIsland) {
        QDBusMessage msg = QDBusMessage::createMethodCall(s_island, s_islandPath, s_island, QStringLiteral("StartWithHints"));
        msg << QStringLiteral("lingmo-share") << QStringLiteral("transfer") << title << subtitle << icon << double(progress)
            << QStringList{QStringLiteral("cancel"), tr("Cancel")} << QVariantMap{};
        auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, key] {
            w->deleteLater();
            QDBusPendingReply<uint> reply = *w;
            auto it = m_activities.find(key);
            if (reply.isError()) {
                // The Island went away: fall back to a notification
                if (it != m_activities.end()) {
                    it->viaIsland = false;
                    it->notificationId = notify(it->title, QString(), it->icon);
                }
                return;
            }
            if (it == m_activities.end()) {
                QDBusMessage fin = QDBusMessage::createMethodCall(s_island, s_islandPath, s_island, QStringLiteral("Finish"));
                fin << reply.value() << QString();
                QDBusConnection::sessionBus().call(fin, QDBus::NoBlock);
                return;
            }
            it->islandId = reply.value();
        });
        return;
    }
    // Notifications: one that is replaced as the transfer goes (lingmo-notificationd
    // draws the "value" hint as a progress bar)
    QVariantMap hints{{QStringLiteral("value"), int(progress * 100)}, {QStringLiteral("transient"), true}};
    m_activities[key].notificationId = notify(title, subtitle, icon, {QStringLiteral("cancel"), tr("Cancel")},
                                              [this, key](const QString &action) {
        if (action == QLatin1String("cancel"))
            emit cancelRequested(key);
    }, hints);
    m_activities[key].lastNotified = QDateTime::currentMSecsSinceEpoch();
}

void Feedback::update(const QString &key, const QString &subtitle, qreal progress)
{
    auto it = m_activities.find(key);
    if (it == m_activities.end())
        return;
    if (it->viaIsland) {
        if (!it->islandId)
            return;
        QDBusMessage msg = QDBusMessage::createMethodCall(s_island, s_islandPath, s_island, QStringLiteral("UpdateFields"));
        msg << it->islandId << QVariantMap{{QStringLiteral("subtitle"), subtitle}, {QStringLiteral("progress"), double(progress)}};
        QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
        return;
    }
    // Notifications are heavier: at most one update a second
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - it->lastNotified < 1000)
        return;
    it->lastNotified = now;
    const QString title = it->title;
    const QString icon = it->icon;
    QDBusMessage msg = QDBusMessage::createMethodCall(s_notifications, s_notificationsPath, s_notifications, QStringLiteral("Notify"));
    msg << QStringLiteral("Lingmo Share") << it->notificationId << icon << title << subtitle
        << QStringList{QStringLiteral("cancel"), tr("Cancel")}
        << QVariantMap{{QStringLiteral("value"), int(progress * 100)}, {QStringLiteral("transient"), true},
                       {QStringLiteral("desktop-entry"), QStringLiteral("lingmo-share")}}
        << int(-1);
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
}

void Feedback::finish(const QString &key, const QString &message)
{
    auto it = m_activities.find(key);
    if (it == m_activities.end())
        return;
    const Activity a = *it;
    m_activities.erase(it);
    if (a.viaIsland) {
        if (a.islandId) {
            QDBusMessage msg = QDBusMessage::createMethodCall(s_island, s_islandPath, s_island, QStringLiteral("Finish"));
            msg << a.islandId << message;
            QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
        }
        return;
    }
    if (a.notificationId)
        closeNotification(a.notificationId);
}

uint Feedback::notify(const QString &title, const QString &body, const QString &icon, const QStringList &actions,
                      std::function<void(const QString &)> onAction, const QVariantMap &extraHints)
{
    QVariantMap hints = extraHints;
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("lingmo-share"));
    QDBusMessage msg = QDBusMessage::createMethodCall(s_notifications, s_notificationsPath, s_notifications, QStringLiteral("Notify"));
    msg << QStringLiteral("Lingmo Share") << uint(0) << icon << title << body << actions << hints << int(-1);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return 0;
    const uint id = reply.arguments().first().toUInt();
    if (onAction)
        m_callbacks.insert(id, onAction);
    return id;
}

void Feedback::closeNotification(uint id)
{
    m_callbacks.remove(id);
    QDBusMessage msg = QDBusMessage::createMethodCall(s_notifications, s_notificationsPath, s_notifications,
                                                      QStringLiteral("CloseNotification"));
    msg << id;
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
}

void Feedback::onIslandAction(uint id, const QString &action)
{
    for (auto it = m_activities.cbegin(); it != m_activities.cend(); ++it) {
        if (it->islandId == id && action == QLatin1String("cancel")) {
            emit cancelRequested(it.key());
            return;
        }
    }
}

void Feedback::onIslandClosed(uint id)
{
    for (auto it = m_activities.begin(); it != m_activities.end(); ++it) {
        if (it->islandId == id) {
            it->islandId = 0;
            return;
        }
    }
}

void Feedback::onNotificationAction(uint id, const QString &action)
{
    auto it = m_callbacks.constFind(id);
    if (it != m_callbacks.constEnd() && *it)
        (*it)(action);
}

void Feedback::onNotificationClosed(uint id, uint)
{
    m_callbacks.remove(id);
}
