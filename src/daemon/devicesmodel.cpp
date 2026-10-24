/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "devicesmodel.h"
#include "common/shareconfig.h"
#include "kdeconnect/kdeconnect.h"
#include "localsend/localsend.h"

#include <QCollator>

static const QHash<int, QByteArray> s_roles = {
    {DevicesModel::IdRole, "deviceId"}, {DevicesModel::NameRole, "name"}, {DevicesModel::KindRole, "kind"},
    {DevicesModel::DeviceTypeRole, "deviceType"}, {DevicesModel::IconRole, "iconName"},
    {DevicesModel::BatteryRole, "battery"}, {DevicesModel::ChargingRole, "charging"},
    {DevicesModel::SignalRole, "signalStrength"}, {DevicesModel::ReachableRole, "reachable"},
    {DevicesModel::PairedRole, "paired"}, {DevicesModel::PairStateRole, "pairState"},
    {DevicesModel::KeyRole, "verificationKey"}, {DevicesModel::TrustedRole, "trusted"},
    {DevicesModel::LingmoRole, "lingmo"}, {DevicesModel::AddressRole, "address"},
    {DevicesModel::PluginsRole, "plugins"}, {DevicesModel::ModelRole, "model"},
    {DevicesModel::SendableRole, "sendable"}};

DevicesModel::DevicesModel(KdeConnect *kdc, LocalSend *ls, ShareConfig *config, QObject *parent)
    : QAbstractListModel(parent)
    , m_kdc(kdc)
    , m_ls(ls)
    , m_config(config)
{
    connect(kdc, &KdeConnect::devicesChanged, this, &DevicesModel::rebuild);
    connect(ls, &LocalSend::devicesChanged, this, &DevicesModel::rebuild);
    connect(config, &ShareConfig::changed, this, &DevicesModel::rebuild);
    rebuild();
}

QString DevicesModel::iconFor(const QString &kind, const QString &type)
{
    if (type == QLatin1String("smartphone") || type == QLatin1String("phone") || type == QLatin1String("mobile"))
        return QStringLiteral("smartphone");
    if (type == QLatin1String("tablet"))
        return QStringLiteral("tablet");
    if (type == QLatin1String("laptop"))
        return QStringLiteral("computer-laptop");
    if (type == QLatin1String("tv"))
        return QStringLiteral("video-television");
    if (type == QLatin1String("web"))
        return QStringLiteral("internet-web-browser");
    if (type == QLatin1String("server") || type == QLatin1String("headless"))
        return QStringLiteral("network-server");
    Q_UNUSED(kind);
    return QStringLiteral("computer");
}

void DevicesModel::rebuild()
{
    QList<QVariantMap> items;
    for (const KdcDevice &d : m_kdc->devices()) {
        // Unpaired phones only show while they can be reached (to pair)
        if (!d.paired() && !d.reachable)
            continue;
        QVariantMap m = d.toMap();
        m.insert(QStringLiteral("deviceId"), QStringLiteral("kdc:") + d.id);
        m.insert(QStringLiteral("kind"), QStringLiteral("android"));
        m.insert(QStringLiteral("deviceType"), d.type);
        m.insert(QStringLiteral("iconName"), iconFor(QStringLiteral("android"), d.type));
        m.insert(QStringLiteral("trusted"), d.paired());
        m.insert(QStringLiteral("sendable"), d.paired() && d.reachable);
        items << m;
    }
    for (const LsDevice &d : m_ls->devices()) {
        QVariantMap m = d.toMap();
        m.insert(QStringLiteral("deviceId"), QStringLiteral("ls:") + d.fingerprint);
        m.insert(QStringLiteral("kind"), QStringLiteral("localsend"));
        m.insert(QStringLiteral("iconName"), iconFor(QStringLiteral("localsend"), d.deviceType));
        m.insert(QStringLiteral("reachable"), true);
        m.insert(QStringLiteral("paired"), false);
        m.insert(QStringLiteral("pairState"), 0);
        m.insert(QStringLiteral("battery"), -1);
        m.insert(QStringLiteral("signalStrength"), -1);
        m.insert(QStringLiteral("trusted"), d.verified && m_config->isTrusted(d.fingerprint));
        m.insert(QStringLiteral("sendable"), true);
        items << m;
    }
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(items.begin(), items.end(), [&collator](const QVariantMap &a, const QVariantMap &b) {
        // Paired phones first, then by name
        const bool pa = a.value(QStringLiteral("paired")).toBool(), pb = b.value(QStringLiteral("paired")).toBool();
        if (pa != pb)
            return pa;
        return collator.compare(a.value(QStringLiteral("name")).toString(), b.value(QStringLiteral("name")).toString()) < 0;
    });

    bool sameIds = items.size() == m_items.size();
    for (int i = 0; sameIds && i < items.size(); ++i)
        sameIds = items.at(i).value(QStringLiteral("deviceId")) == m_items.at(i).value(QStringLiteral("deviceId"));
    if (sameIds) {
        m_items = items;
        if (!items.isEmpty())
            emit dataChanged(index(0), index(items.size() - 1));
        emit changed();
        return;
    }
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
    emit changed();
}

QVariantList DevicesModel::phones() const
{
    QVariantList out;
    for (const QVariantMap &m : m_items) {
        if (m.value(QStringLiteral("kind")).toString() == QLatin1String("android") && m.value(QStringLiteral("paired")).toBool())
            out << m;
    }
    return out;
}

int DevicesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant DevicesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    return m_items.at(index.row()).value(QString::fromLatin1(s_roles.value(role)));
}

QHash<int, QByteArray> DevicesModel::roleNames() const
{
    return s_roles;
}

int DevicesModel::pairedCount() const
{
    int n = 0;
    for (const QVariantMap &m : m_items)
        n += m.value(QStringLiteral("paired")).toBool() ? 1 : 0;
    return n;
}

QVariantMap DevicesModel::get(const QString &id) const
{
    for (const QVariantMap &m : m_items) {
        if (m.value(QStringLiteral("deviceId")).toString() == id)
            return m;
    }
    return {};
}

QVariantList DevicesModel::sendTargets() const
{
    QVariantList out;
    for (const QVariantMap &m : m_items) {
        if (m.value(QStringLiteral("sendable")).toBool())
            out << m;
    }
    return out;
}
