/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TRANSFERS_H
#define TRANSFERS_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QStringList>

// The transfers of this session (sent and received, both protocols)
class Transfers : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)

public:
    enum State { Waiting, Active, Done, Failed, Declined, Cancelled };
    Q_ENUM(State)

    struct Transfer {
        QString id;
        bool incoming = false;
        QString device;
        QString deviceIcon;
        QString title;
        qint64 done = 0;
        qint64 total = 0;
        State state = Waiting;
        QString error;
        QStringList paths;
        QString protocol; // localsend | kdeconnect
        QDateTime when;
    };

    enum Roles { IdRole = Qt::UserRole + 1, IncomingRole, DeviceRole, DeviceIconRole, TitleRole, ProgressRole, DoneRole,
                 TotalRole, StateRole, ErrorRole, PathsRole, ProtocolRole, ActiveRole, WhenRole };

    explicit Transfers(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    int activeCount() const;

    QString add(const Transfer &t);
    void setProgress(const QString &id, qint64 done, qint64 total);
    void setState(const QString &id, State state, const QString &error = {});
    void setPaths(const QString &id, const QStringList &paths);
    void setTitle(const QString &id, const QString &title);
    void setDevice(const QString &id, const QString &device);
    bool get(const QString &id, Transfer *out) const;
    Q_INVOKABLE void clearFinished();

    static QString describe(const QStringList &names, int count);

signals:
    void countChanged();
    void changed(const QString &id);

private:
    int indexOf(const QString &id) const;
    QList<Transfer> m_items;
};

#endif
