/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "transfers.h"

#include <QUuid>

Transfers::Transfers(QObject *parent)
    : QAbstractListModel(parent)
{
}

int Transfers::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant Transfers::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Transfer &t = m_items.at(index.row());
    switch (role) {
    case IdRole: return t.id;
    case IncomingRole: return t.incoming;
    case DeviceRole: return t.device;
    case DeviceIconRole: return t.deviceIcon;
    case TitleRole: return t.title;
    case ProgressRole: return t.total > 0 ? qreal(t.done) / qreal(t.total) : (t.state == Done ? 1.0 : 0.0);
    case DoneRole: return t.done;
    case TotalRole: return t.total;
    case StateRole: return int(t.state);
    case ErrorRole: return t.error;
    case PathsRole: return t.paths;
    case ProtocolRole: return t.protocol;
    case ActiveRole: return t.state == Waiting || t.state == Active;
    case WhenRole: return t.when;
    }
    return {};
}

QHash<int, QByteArray> Transfers::roleNames() const
{
    return {{IdRole, "transferId"}, {IncomingRole, "incoming"}, {DeviceRole, "device"}, {DeviceIconRole, "deviceIcon"},
            {TitleRole, "title"}, {ProgressRole, "progress"}, {DoneRole, "done"}, {TotalRole, "total"},
            {StateRole, "state"}, {ErrorRole, "error"}, {PathsRole, "paths"}, {ProtocolRole, "protocol"},
            {ActiveRole, "active"}, {WhenRole, "when"}};
}

int Transfers::activeCount() const
{
    int n = 0;
    for (const Transfer &t : m_items)
        n += (t.state == Waiting || t.state == Active) ? 1 : 0;
    return n;
}

int Transfers::indexOf(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id)
            return i;
    }
    return -1;
}

QString Transfers::add(const Transfer &transfer)
{
    Transfer t = transfer;
    if (t.id.isEmpty())
        t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    t.when = QDateTime::currentDateTime();
    // Newest first
    beginInsertRows({}, 0, 0);
    m_items.prepend(t);
    endInsertRows();
    emit countChanged();
    emit changed(t.id);
    return t.id;
}

void Transfers::setProgress(const QString &id, qint64 done, qint64 total)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    Transfer &t = m_items[i];
    t.done = done;
    t.total = total;
    if (t.state == Waiting)
        t.state = Active;
    emit dataChanged(index(i), index(i));
    emit changed(id);
}

void Transfers::setState(const QString &id, State state, const QString &error)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    m_items[i].state = state;
    m_items[i].error = error;
    if (state == Done && m_items[i].total > 0)
        m_items[i].done = m_items[i].total;
    emit dataChanged(index(i), index(i));
    emit countChanged();
    emit changed(id);
}

void Transfers::setPaths(const QString &id, const QStringList &paths)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    m_items[i].paths = paths;
    emit dataChanged(index(i), index(i));
}

void Transfers::setTitle(const QString &id, const QString &title)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    m_items[i].title = title;
    emit dataChanged(index(i), index(i));
}

void Transfers::setDevice(const QString &id, const QString &device)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    m_items[i].device = device;
    emit dataChanged(index(i), index(i));
}

bool Transfers::get(const QString &id, Transfer *out) const
{
    const int i = indexOf(id);
    if (i < 0)
        return false;
    if (out)
        *out = m_items.at(i);
    return true;
}

void Transfers::clearFinished()
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        const State s = m_items.at(i).state;
        if (s != Waiting && s != Active) {
            beginRemoveRows({}, i, i);
            m_items.removeAt(i);
            endRemoveRows();
        }
    }
    emit countChanged();
}

QString Transfers::describe(const QStringList &names, int count)
{
    if (count <= 0)
        return QString();
    if (count == 1 && !names.isEmpty())
        return names.first();
    return tr("%n files", nullptr, count);
}
