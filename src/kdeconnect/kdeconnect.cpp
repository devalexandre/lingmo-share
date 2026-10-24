/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "kdeconnect.h"
#include "../common/shareconfig.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

static const QString s_service = QStringLiteral("org.kde.kdeconnect");
static const QString s_daemonPath = QStringLiteral("/modules/kdeconnect");
static const QString s_daemonIface = QStringLiteral("org.kde.kdeconnect.daemon");
static const QString s_deviceIface = QStringLiteral("org.kde.kdeconnect.device");

// ConversationAddress as kdeconnectd marshals it: a struct with one string
struct KdcAddress {
    QString address;
};
Q_DECLARE_METATYPE(KdcAddress)

static QDBusArgument &operator<<(QDBusArgument &arg, const KdcAddress &a)
{
    arg.beginStructure();
    arg << a.address;
    arg.endStructure();
    return arg;
}

static const QDBusArgument &operator>>(const QDBusArgument &arg, KdcAddress &a)
{
    arg.beginStructure();
    arg >> a.address;
    arg.endStructure();
    return arg;
}

QVariantMap KdcDevice::toMap() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("type"), type},
            {QStringLiteral("iconName"), iconName},
            {QStringLiteral("reachable"), reachable},
            {QStringLiteral("pairState"), pairState},
            {QStringLiteral("paired"), paired()},
            {QStringLiteral("verificationKey"), verificationKey},
            {QStringLiteral("plugins"), plugins},
            {QStringLiteral("battery"), battery},
            {QStringLiteral("charging"), charging},
            {QStringLiteral("signalStrength"), signal},
            {QStringLiteral("networkType"), networkType}};
}

KdeConnect::KdeConnect(ShareConfig *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    qDBusRegisterMetaType<KdcAddress>();
    auto *watcher = new QDBusServiceWatcher(s_service, QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
        setAvailable(!owner.isEmpty() && m_config->androidEnabled());
        if (m_available)
            refreshAll();
    });
    connect(m_config, &ShareConfig::changed, this, &KdeConnect::apply);
}

bool KdeConnect::installed()
{
    if (qEnvironmentVariableIsSet("LINGMO_SHARE_FAKE_KDECONNECT"))
        return true;
    return !QStandardPaths::findExecutable(QStringLiteral("kdeconnectd")).isEmpty()
           || QFileInfo::exists(QStringLiteral("/usr/share/dbus-1/services/org.kde.kdeconnect.service"));
}

void KdeConnect::apply()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!m_config->androidEnabled()) {
        // Off: kdeconnectd stops being reachable on the network
        if (bus.interface()->isServiceRegistered(s_service))
            applyVisibility();
        setAvailable(false);
        return;
    }
    if (!m_connected) {
        m_connected = true;
        // Signals of every object kdeconnectd exports (devices come and go)
        const QStringList ifaces = {s_daemonIface, s_deviceIface,
                                    QStringLiteral("org.kde.kdeconnect.device.battery"),
                                    QStringLiteral("org.kde.kdeconnect.device.connectivity_report"),
                                    QStringLiteral("org.kde.kdeconnect.device.share"),
                                    QStringLiteral("org.kde.kdeconnect.device.notifications"),
                                    QStringLiteral("org.kde.kdeconnect.device.mprisremote"),
                                    QStringLiteral("org.kde.kdeconnect.device.conversations")};
        for (const QString &iface : ifaces)
            bus.connect(s_service, QString(), iface, QString(), this, SLOT(onSignal(QDBusMessage)));
    }
    if (bus.interface()->isServiceRegistered(s_service)) {
        setAvailable(true);
        refreshAll();
    } else if (installed()) {
        // D-Bus activation (org.kde.kdeconnect.service): no KDE session needed
        bus.interface()->asyncCall(QStringLiteral("StartServiceByName"), s_service, 0u);
    }
}

void KdeConnect::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    if (!available) {
        m_devices.clear();
        emit devicesChanged();
    }
    emit availableChanged();
}

void KdeConnect::call(const QString &path, const QString &iface, const QString &method, const QVariantList &args,
                      std::function<void(const QDBusMessage &)> done)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(s_service, path, iface, method);
    msg.setArguments(args);
    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg, 5000);
    if (!done)
        return;
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done] {
        watcher->deleteLater();
        done(watcher->reply());
    });
}

QVariantMap KdeConnect::properties(const QString &path, const QString &iface)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(s_service, path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("GetAll"));
    msg << iface;
    QDBusReply<QVariantMap> reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 3000);
    return reply.isValid() ? reply.value() : QVariantMap();
}

void KdeConnect::applyVisibility()
{
    // "Off" in Lingmo means nothing answers on the network: KDE Connect included
    const bool on = m_config->androidEnabled() && m_config->visibility() != ShareConfig::Off;
    call(s_daemonPath, s_daemonIface, QStringLiteral("linkProviders"), {}, [this, on](const QDBusMessage &reply) {
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return;
        const QStringList providers = reply.arguments().first().toStringList();
        for (const QString &line : providers) {
            const QStringList parts = line.split(QLatin1Char('|'));
            if (parts.size() < 3 || parts.at(1) == QLatin1String("loopback"))
                continue;
            const bool enabled = parts.at(2) == QLatin1String("enabled");
            if (enabled != on)
                call(s_daemonPath, s_daemonIface, QStringLiteral("setLinkProviderState"), {parts.at(1), on});
        }
    });
}

void KdeConnect::refreshAll()
{
    if (!m_available)
        return;
    applyVisibility();
    const QString name = m_config->deviceName();
    if (name != m_appliedName) {
        m_appliedName = name;
        call(s_daemonPath, s_daemonIface, QStringLiteral("setAnnouncedName"), {name});
    }
    call(s_daemonPath, s_daemonIface, QStringLiteral("devices"), {false, false}, [this](const QDBusMessage &reply) {
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return;
        const QStringList ids = reply.arguments().first().toStringList();
        bool changed = false;
        for (auto it = m_devices.begin(); it != m_devices.end();) {
            if (!ids.contains(it.key())) {
                it = m_devices.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (changed)
            emit devicesChanged();
        for (const QString &id : ids)
            refreshDevice(id);
    });
}

void KdeConnect::refreshDevice(const QString &id)
{
    if (!m_available || id.isEmpty() || id.contains(QLatin1Char('/')))
        return;
    const QString path = devicePath(id);
    QDBusMessage msg = QDBusMessage::createMethodCall(s_service, path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("GetAll"));
    msg << s_deviceIface;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, id, path] {
        watcher->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *watcher;
        if (reply.isError())
            return;
        const QVariantMap p = reply.value();
        KdcDevice d = m_devices.value(id);
        const int oldPair = d.pairState;
        d.id = id;
        d.name = p.value(QStringLiteral("name")).toString();
        d.type = p.value(QStringLiteral("type")).toString();
        d.iconName = p.value(QStringLiteral("iconName")).toString();
        d.reachable = p.value(QStringLiteral("isReachable")).toBool();
        d.pairState = p.value(QStringLiteral("pairState")).toInt();
        d.verificationKey = p.value(QStringLiteral("verificationKey")).toString();
        // Battery and signal come from their plugins, when loaded
        call(path, s_deviceIface, QStringLiteral("loadedPlugins"), {}, [this, id, path](const QDBusMessage &r) {
            if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty() || !m_devices.contains(id))
                return;
            KdcDevice &dev = m_devices[id];
            dev.plugins = r.arguments().first().toStringList();
            if (dev.plugins.contains(QStringLiteral("kdeconnect_battery"))) {
                const QVariantMap b = properties(path + QStringLiteral("/battery"), QStringLiteral("org.kde.kdeconnect.device.battery"));
                if (b.value(QStringLiteral("hasBattery"), true).toBool()) {
                    dev.battery = b.value(QStringLiteral("charge"), -1).toInt();
                    dev.charging = b.value(QStringLiteral("isCharging")).toBool();
                }
            } else {
                dev.battery = -1;
            }
            if (dev.plugins.contains(QStringLiteral("kdeconnect_connectivity_report"))) {
                const QVariantMap c = properties(path + QStringLiteral("/connectivity_report"),
                                                 QStringLiteral("org.kde.kdeconnect.device.connectivity_report"));
                dev.signal = c.value(QStringLiteral("cellularNetworkStrength"), -1).toInt();
                dev.networkType = c.value(QStringLiteral("cellularNetworkType")).toString();
            } else {
                dev.signal = -1;
            }
            emit devicesChanged();
        });
        const bool isNew = !m_devices.contains(id);
        m_devices.insert(id, d);
        emit devicesChanged();
        if (d.pairState == 2 && oldPair != 2)
            emit pairingRequested(id);
        if (d.paired() && (isNew || oldPair != 3))
            applyDeviceSettings(id);
    });
}

void KdeConnect::applyDeviceSettings(const QString &id)
{
    const QString path = devicePath(id);
    for (const QString &plugin : ShareConfig::knownPlugins())
        call(path, s_deviceIface, QStringLiteral("setPluginEnabled"), {plugin, m_config->pluginEnabled(plugin)});
    applyIncomingFolder(id);
}

void KdeConnect::applyIncomingFolder(const QString &id)
{
    // Received files go to the Lingmo receive folder: kdeconnectd's share
    // plugin reads incoming_path from its per-device config, and reloads it
    // when org.kde.kdeconnect.config.configChanged is emitted for it
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + QStringLiteral("/kdeconnect/") + id + QStringLiteral("/kdeconnect_share");
    QDir().mkpath(dir);
    QSettings share(dir + QStringLiteral("/config"), QSettings::IniFormat);
    const QString folder = m_config->receiveFolder();
    if (share.value(QStringLiteral("incoming_path")).toString() == folder)
        return;
    share.setValue(QStringLiteral("incoming_path"), folder);
    share.sync();
    QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/kdeconnect/") + id + QStringLiteral("/kdeconnect_share"),
                                                     QStringLiteral("org.kde.kdeconnect.config"), QStringLiteral("configChanged"));
    QDBusConnection::sessionBus().send(signal);
}

bool KdeConnect::device(const QString &id, KdcDevice *out) const
{
    auto it = m_devices.constFind(id);
    if (it == m_devices.constEnd())
        return false;
    if (out)
        *out = *it;
    return true;
}

static QString deviceIdFromPath(const QString &path)
{
    static const QString prefix = KdeConnect::devicePath(QString());
    if (!path.startsWith(prefix))
        return {};
    return path.mid(prefix.size()).section(QLatin1Char('/'), 0, 0);
}

// (isa(s)xiixixa(xsss)): event, body, addresses, date, type, read, threadID, uID, subID, attachments
static QVariantMap decodeMessage(const QDBusArgument &arg)
{
    QVariantMap m;
    qint32 event = 0, type = 0, read = 0, uid = 0;
    qint64 date = 0, thread = 0, sub = 0;
    QString body;
    QStringList addresses;
    arg.beginStructure();
    arg >> event >> body;
    arg.beginArray();
    while (!arg.atEnd()) {
        KdcAddress a;
        arg >> a;
        addresses << a.address;
    }
    arg.endArray();
    arg >> date >> type >> read >> thread >> uid >> sub;
    int attachments = 0;
    arg.beginArray();
    while (!arg.atEnd()) {
        qint64 part;
        QString mime, data, unique;
        arg.beginStructure();
        arg >> part >> mime >> data >> unique;
        arg.endStructure();
        ++attachments;
    }
    arg.endArray();
    arg.endStructure();
    m.insert(QStringLiteral("body"), body);
    m.insert(QStringLiteral("addresses"), addresses);
    m.insert(QStringLiteral("date"), date);
    // Android: 1 received, 2 sent
    m.insert(QStringLiteral("sent"), type == 2);
    m.insert(QStringLiteral("read"), read != 0);
    m.insert(QStringLiteral("thread"), thread);
    m.insert(QStringLiteral("uid"), uid);
    m.insert(QStringLiteral("attachments"), attachments);
    return m;
}

static QVariantMap decodeVariantMessage(const QVariant &v)
{
    QVariant inner = v;
    if (inner.canConvert<QDBusVariant>())
        inner = inner.value<QDBusVariant>().variant();
    if (inner.canConvert<QDBusArgument>())
        return decodeMessage(inner.value<QDBusArgument>());
    return {};
}

void KdeConnect::onSignal(const QDBusMessage &message)
{
    if (!m_config->androidEnabled())
        return;
    const QString iface = message.interface();
    const QString member = message.member();
    const QString id = deviceIdFromPath(message.path());
    const QVariantList args = message.arguments();

    if (iface == s_daemonIface) {
        if (member == QLatin1String("deviceAdded") || member == QLatin1String("deviceRemoved")
            || member == QLatin1String("deviceVisibilityChanged") || member == QLatin1String("deviceListChanged"))
            refreshAll();
        return;
    }
    if (id.isEmpty())
        return;
    if (iface == s_deviceIface) {
        refreshDevice(id);
    } else if (iface.endsWith(QLatin1String(".battery")) && args.size() >= 2 && m_devices.contains(id)) {
        m_devices[id].charging = args.at(0).toBool();
        m_devices[id].battery = args.at(1).toInt();
        emit devicesChanged();
    } else if (iface.endsWith(QLatin1String(".connectivity_report")) && args.size() >= 2 && m_devices.contains(id)) {
        m_devices[id].networkType = args.at(0).toString();
        m_devices[id].signal = args.at(1).toInt();
        emit devicesChanged();
    } else if (iface.endsWith(QLatin1String(".share")) && member == QLatin1String("shareReceived") && !args.isEmpty()) {
        emit fileReceived(id, args.first().toString());
    } else if (iface.endsWith(QLatin1String(".notifications"))) {
        if (member == QLatin1String("notificationPosted") && !args.isEmpty())
            emit notificationPosted(id, args.first().toString());
        emit notificationsChanged(id);
    } else if (iface.endsWith(QLatin1String(".mprisremote"))) {
        emit mediaChanged(id);
    } else if (iface.endsWith(QLatin1String(".conversations"))) {
        if ((member == QLatin1String("conversationCreated") || member == QLatin1String("conversationUpdated")) && !args.isEmpty()) {
            const QVariantMap m = decodeVariantMessage(args.first());
            if (!m.isEmpty())
                emit smsMessage(id, m);
        } else if (member == QLatin1String("conversationLoaded") && !args.isEmpty()) {
            emit conversationLoaded(id, args.first().toLongLong());
        }
    }
}

void KdeConnect::requestPairing(const QString &id) { call(devicePath(id), s_deviceIface, QStringLiteral("requestPairing")); }
void KdeConnect::acceptPairing(const QString &id) { call(devicePath(id), s_deviceIface, QStringLiteral("acceptPairing")); }
void KdeConnect::rejectPairing(const QString &id) { call(devicePath(id), s_deviceIface, QStringLiteral("cancelPairing")); }
void KdeConnect::unpair(const QString &id) { call(devicePath(id), s_deviceIface, QStringLiteral("unpair")); }

void KdeConnect::ring(const QString &id)
{
    call(devicePath(id) + QStringLiteral("/findmyphone"), QStringLiteral("org.kde.kdeconnect.device.findmyphone"), QStringLiteral("ring"));
}

void KdeConnect::shareFiles(const QString &id, const QStringList &paths)
{
    QStringList urls;
    for (const QString &p : paths)
        urls << (p.contains(QLatin1String("://")) ? p : QUrl::fromLocalFile(p).toString());
    call(devicePath(id) + QStringLiteral("/share"), QStringLiteral("org.kde.kdeconnect.device.share"),
         QStringLiteral("shareUrls"), {urls});
}

void KdeConnect::shareText(const QString &id, const QString &text)
{
    call(devicePath(id) + QStringLiteral("/share"), QStringLiteral("org.kde.kdeconnect.device.share"),
         QStringLiteral("shareText"), {text});
}

void KdeConnect::sendClipboard(const QString &id)
{
    call(devicePath(id) + QStringLiteral("/clipboard"), QStringLiteral("org.kde.kdeconnect.device.clipboard"),
         QStringLiteral("sendClipboard"));
}

void KdeConnect::mediaAction(const QString &id, const QString &action)
{
    call(devicePath(id) + QStringLiteral("/mprisremote"), QStringLiteral("org.kde.kdeconnect.device.mprisremote"),
         QStringLiteral("sendAction"), {action});
}

void KdeConnect::setMediaVolume(const QString &id, int volume)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(s_service, devicePath(id) + QStringLiteral("/mprisremote"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
    msg << QStringLiteral("org.kde.kdeconnect.device.mprisremote") << QStringLiteral("volume")
        << QVariant::fromValue(QDBusVariant(qBound(0, volume, 100)));
    QDBusConnection::sessionBus().asyncCall(msg);
}

QVariantMap KdeConnect::media(const QString &id)
{
    return properties(devicePath(id) + QStringLiteral("/mprisremote"), QStringLiteral("org.kde.kdeconnect.device.mprisremote"));
}

QVariantList KdeConnect::notifications(const QString &id)
{
    QVariantList out;
    const QString base = devicePath(id) + QStringLiteral("/notifications");
    QDBusMessage msg = QDBusMessage::createMethodCall(s_service, base, QStringLiteral("org.kde.kdeconnect.device.notifications"),
                                                      QStringLiteral("activeNotifications"));
    QDBusReply<QStringList> reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 3000);
    if (!reply.isValid())
        return out;
    for (const QString &publicId : reply.value()) {
        QVariantMap p = properties(base + QLatin1Char('/') + publicId, QStringLiteral("org.kde.kdeconnect.device.notifications.notification"));
        if (p.isEmpty())
            continue;
        p.insert(QStringLiteral("publicId"), publicId);
        p.insert(QStringLiteral("canReply"), !p.value(QStringLiteral("replyId")).toString().isEmpty());
        out.prepend(p);
    }
    return out;
}

void KdeConnect::replyNotification(const QString &id, const QString &publicId, const QString &text)
{
    call(devicePath(id) + QStringLiteral("/notifications/") + publicId,
         QStringLiteral("org.kde.kdeconnect.device.notifications.notification"), QStringLiteral("sendReply"), {text});
}

void KdeConnect::dismissNotification(const QString &id, const QString &publicId)
{
    call(devicePath(id) + QStringLiteral("/notifications/") + publicId,
         QStringLiteral("org.kde.kdeconnect.device.notifications.notification"), QStringLiteral("dismiss"));
}

QVariantList KdeConnect::conversations(const QString &id)
{
    QVariantList out;
    QDBusMessage msg = QDBusMessage::createMethodCall(s_service, devicePath(id), QStringLiteral("org.kde.kdeconnect.device.conversations"),
                                                      QStringLiteral("activeConversations"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return out;
    const QVariant first = reply.arguments().first();
    if (first.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = first.value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd()) {
            QDBusVariant v;
            arg >> v;
            const QVariantMap m = decodeVariantMessage(v.variant());
            if (!m.isEmpty())
                out << m;
        }
        arg.endArray();
    } else {
        for (const QVariant &v : first.toList()) {
            const QVariantMap m = decodeVariantMessage(v);
            if (!m.isEmpty())
                out << m;
        }
    }
    return out;
}

void KdeConnect::requestConversations(const QString &id)
{
    call(devicePath(id), QStringLiteral("org.kde.kdeconnect.device.conversations"), QStringLiteral("requestAllConversationThreads"));
}

void KdeConnect::requestConversation(const QString &id, qint64 thread, int count)
{
    call(devicePath(id), QStringLiteral("org.kde.kdeconnect.device.conversations"), QStringLiteral("requestConversation"),
         {thread, 0, count});
}

void KdeConnect::replyToConversation(const QString &id, qint64 thread, const QString &text)
{
    call(devicePath(id), QStringLiteral("org.kde.kdeconnect.device.conversations"), QStringLiteral("replyToConversation"),
         {thread, text, QVariantList()});
}

void KdeConnect::sendSms(const QString &id, const QString &number, const QString &text)
{
    QVariantList addresses{QVariant::fromValue(KdcAddress{number})};
    call(devicePath(id), QStringLiteral("org.kde.kdeconnect.device.conversations"), QStringLiteral("sendWithoutConversation"),
         {addresses, text, QVariantList()});
}
