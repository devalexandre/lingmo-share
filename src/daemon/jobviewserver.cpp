/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jobviewserver.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>

static const QString s_service = QStringLiteral("org.kde.JobViewServer");

JobViewServer::JobViewServer(QObject *parent)
    : QObject(parent)
{
}

bool JobViewServer::registerService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    // Never take it from a running Plasma (or anyone else)
    if (bus.interface()->isServiceRegistered(s_service))
        return false;
    if (!bus.registerObject(QStringLiteral("/JobViewServer"), this, QDBusConnection::ExportAllSlots))
        return false;
    return bus.registerService(s_service);
}

QDBusObjectPath JobViewServer::requestView(const QString &desktopEntry, int capabilities, const QVariantMap &hints)
{
    Q_UNUSED(capabilities);
    const int id = m_next++;
    auto *view = new JobView(this, id, desktopEntry);
    view->state = hints;
    m_views.insert(id, view);
    const QString path = QStringLiteral("/JobViewServer/JobView_%1").arg(id);
    QDBusConnection::sessionBus().registerObject(path, view, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
    emit jobStarted(id, desktopEntry);
    return QDBusObjectPath(path);
}

void JobViewServer::cancel(int view)
{
    if (JobView *v = m_views.value(view))
        emit v->cancelRequested();
}

JobView::JobView(JobViewServer *server, int id, const QString &desktopEntry)
    : QObject(server)
    , m_server(server)
    , m_id(id)
    , m_desktopEntry(desktopEntry)
{
}

void JobView::update(const QVariantMap &properties)
{
    for (auto it = properties.cbegin(); it != properties.cend(); ++it)
        state.insert(it.key(), it.value());
    emit m_server->jobUpdated(m_id, state);
}

void JobView::terminate(uint errorCode, const QString &errorMessage, const QVariantMap &hints)
{
    Q_UNUSED(hints);
    emit m_server->jobFinished(m_id, errorCode, errorMessage);
    m_server->m_views.remove(m_id);
    QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/JobViewServer/JobView_%1").arg(m_id));
    deleteLater();
}
