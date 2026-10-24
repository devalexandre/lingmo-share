/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef FEEDBACK_H
#define FEEDBACK_H

#include <QHash>
#include <QObject>
#include <QVariantMap>

#include <functional>

/*
 * How a transfer shows up outside the app: as a live activity in the status
 * bar's Island (com.lingmo.Island: StartWithHints / UpdateFields / Finish)
 * when the status bar offers one, else as desktop notifications
 * (org.freedesktop.Notifications, shown by lingmo-notificationd).
 */
class Feedback : public QObject
{
    Q_OBJECT
public:
    explicit Feedback(QObject *parent = nullptr);

    bool islandAvailable() const;

    // A transfer activity: started, updated with progress (0..1), finished
    void start(const QString &key, const QString &title, const QString &subtitle, const QString &icon, qreal progress);
    void update(const QString &key, const QString &subtitle, qreal progress);
    void finish(const QString &key, const QString &message);

    // A plain notification; actions are id/label pairs, the callback gets the id
    uint notify(const QString &title, const QString &body, const QString &icon,
                const QStringList &actions = {}, std::function<void(const QString &)> onAction = {},
                const QVariantMap &hints = {});
    void closeNotification(uint id);

signals:
    // "cancel" pressed on a transfer in the Island or in its notification
    void cancelRequested(const QString &key);

private slots:
    void onIslandAction(uint id, const QString &action);
    void onIslandClosed(uint id);
    void onNotificationAction(uint id, const QString &action);
    void onNotificationClosed(uint id, uint reason);

private:
    struct Activity {
        uint islandId = 0;
        uint notificationId = 0;
        QString title;
        QString icon;
        bool viaIsland = false;
        qint64 lastNotified = 0;
    };
    QHash<QString, Activity> m_activities;
    QHash<uint, std::function<void(const QString &)>> m_callbacks;
};

#endif
