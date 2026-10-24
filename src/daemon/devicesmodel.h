/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DEVICESMODEL_H
#define DEVICESMODEL_H

#include <QAbstractListModel>
#include <QVariantMap>

class KdeConnect;
class LocalSend;
class ShareConfig;

/*
 * Every nearby device in one list: Android phones through KDE Connect
 * ("kdc:<id>") and LocalSend devices of any kind ("ls:<fingerprint>").
 */
class DevicesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int pairedCount READ pairedCount NOTIFY countChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, KindRole, DeviceTypeRole, IconRole, BatteryRole, ChargingRole,
                 SignalRole, ReachableRole, PairedRole, PairStateRole, KeyRole, TrustedRole, LingmoRole, AddressRole,
                 PluginsRole, ModelRole, SendableRole };

    DevicesModel(KdeConnect *kdc, LocalSend *ls, ShareConfig *config, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    int pairedCount() const;
    Q_INVOKABLE QVariantMap get(const QString &id) const;
    // Devices files can be sent to now (reachable paired phones and LocalSend peers)
    QVariantList sendTargets() const;
    // Paired Android phones
    Q_INVOKABLE QVariantList phones() const;

    static QString iconFor(const QString &kind, const QString &type);

signals:
    void countChanged();
    void changed();

private:
    void rebuild();
    QList<QVariantMap> m_items;
    KdeConnect *m_kdc;
    LocalSend *m_ls;
    ShareConfig *m_config;
};

#endif
