/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef JOBVIEWSERVER_H
#define JOBVIEWSERVER_H

#include <QDBusObjectPath>
#include <QHash>
#include <QObject>
#include <QVariantMap>

class JobView;

/*
 * org.kde.JobViewServer: where KIO-based programs (kdeconnectd among them)
 * report the progress of their jobs. Nothing in a Lingmo session offers it, so
 * Lingmo Share does when the name is free, and shows KDE Connect's file
 * transfers (to and from the phone) like its own. Other programs' jobs are
 * accepted and ignored.
 */
class JobViewServer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.JobViewServerV2")
public:
    explicit JobViewServer(QObject *parent = nullptr);
    bool registerService();
    void cancel(int view);

public slots:
    QDBusObjectPath requestView(const QString &desktopEntry, int capabilities, const QVariantMap &hints);

signals:
    void jobStarted(int view, const QString &desktopEntry);
    void jobUpdated(int view, const QVariantMap &state);
    void jobFinished(int view, uint errorCode, const QString &errorMessage);

private:
    friend class JobView;
    int m_next = 1;
    QHash<int, JobView *> m_views;
};

class JobView : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.JobViewV3")
public:
    JobView(JobViewServer *server, int id, const QString &desktopEntry);
    QVariantMap state;

public slots:
    void update(const QVariantMap &properties);
    void terminate(uint errorCode, const QString &errorMessage, const QVariantMap &hints);

signals:
    void cancelRequested();
    void suspendRequested();
    void resumeRequested();

private:
    JobViewServer *m_server;
    int m_id;
    QString m_desktopEntry;
};

#endif
