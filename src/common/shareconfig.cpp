/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "shareconfig.h"

#include <QDir>
#include <QFile>
#include <QUrl>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

static const QString s_plugins = QStringLiteral("Android/plugin_");

ShareConfig::ShareConfig(QObject *parent)
    : QObject(parent)
    // ~/.config/lingmoos/share.conf, next to the other Lingmo settings
    , m_settings(new QSettings(QStringLiteral("lingmoos"), QStringLiteral("share"), this))
    , m_watcher(new QFileSystemWatcher(this))
{
    // QSettings saves by renaming a new file over the old one: watch the folder too
    QDir().mkpath(QFileInfo(m_settings->fileName()).absolutePath());
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(150);
    connect(debounce, &QTimer::timeout, this, &ShareConfig::reload);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, debounce, qOverload<>(&QTimer::start));
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, debounce, qOverload<>(&QTimer::start));
    watch();
}

ShareConfig::ShareConfig(const QString &file, int port, QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(file, QSettings::IniFormat, this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_port(port)
{
}

ShareConfig::~ShareConfig() = default;

void ShareConfig::watch()
{
    const QString file = m_settings->fileName();
    const QString dir = QFileInfo(file).absolutePath();
    if (!m_watcher->directories().contains(dir))
        m_watcher->addPath(dir);
    if (QFileInfo::exists(file) && !m_watcher->files().contains(file))
        m_watcher->addPath(file);
}

void ShareConfig::reload()
{
    m_settings->sync();
    watch();
    emit changed();
}

QVariant ShareConfig::value(const QString &key, const QVariant &fallback) const
{
    return m_settings->value(key, fallback);
}

void ShareConfig::set(const QString &key, const QVariant &value)
{
    if (m_settings->value(key) == value)
        return;
    m_settings->setValue(key, value);
    m_settings->sync();
    watch();
    emit changed();
}

QStringList ShareConfig::knownPlugins()
{
    return {QStringLiteral("kdeconnect_share"),
            QStringLiteral("kdeconnect_clipboard"),
            QStringLiteral("kdeconnect_notifications"),
            QStringLiteral("kdeconnect_sms"),
            QStringLiteral("kdeconnect_battery"),
            QStringLiteral("kdeconnect_connectivity_report"),
            QStringLiteral("kdeconnect_findmyphone"),
            QStringLiteral("kdeconnect_mprisremote"),
            QStringLiteral("kdeconnect_mousepad"),
            QStringLiteral("kdeconnect_presenter")};
}

QString ShareConfig::deviceName() const
{
    QString fallback = QSysInfo::machineHostName();
    if (fallback.isEmpty() || fallback == QLatin1String("localhost"))
        fallback = QStringLiteral("Lingmo");
    return value(QStringLiteral("Device/name"), fallback).toString();
}

void ShareConfig::setDeviceName(const QString &name)
{
    // LocalSend shows the alias as is: keep it short and on one line
    QString clean = name.simplified().left(48);
    if (!clean.isEmpty())
        set(QStringLiteral("Device/name"), clean);
}

int ShareConfig::visibility() const
{
    return qBound(0, value(QStringLiteral("Device/visibility"), Everyone).toInt(), 2);
}

void ShareConfig::setVisibility(int visibility) { set(QStringLiteral("Device/visibility"), qBound(0, visibility, 2)); }

QString ShareConfig::receiveFolder() const
{
    QString fallback = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (fallback.isEmpty())
        fallback = QDir::homePath();
    const QString folder = value(QStringLiteral("Device/receiveFolder"), fallback).toString();
    return folder.isEmpty() ? fallback : folder;
}

void ShareConfig::setReceiveFolder(const QString &folder)
{
    QString path = folder;
    if (path.startsWith(QLatin1String("file://")))
        path = QUrl(path).toLocalFile();
    if (!path.isEmpty())
        set(QStringLiteral("Device/receiveFolder"), QDir::cleanPath(path));
}

bool ShareConfig::localSendEnabled() const { return value(QStringLiteral("LocalSend/enabled"), true).toBool(); }
void ShareConfig::setLocalSendEnabled(bool enabled) { set(QStringLiteral("LocalSend/enabled"), enabled); }
bool ShareConfig::pinEnabled() const { return value(QStringLiteral("LocalSend/pinEnabled"), false).toBool(); }
void ShareConfig::setPinEnabled(bool enabled) { set(QStringLiteral("LocalSend/pinEnabled"), enabled); }
QString ShareConfig::pin() const { return value(QStringLiteral("LocalSend/pin"), QString()).toString(); }

void ShareConfig::setPin(const QString &pin)
{
    set(QStringLiteral("LocalSend/pin"), pin.trimmed().left(32));
}

QString ShareConfig::requiredPin() const
{
    return pinEnabled() && !pin().isEmpty() ? pin() : QString();
}

bool ShareConfig::androidEnabled() const { return value(QStringLiteral("Android/enabled"), true).toBool(); }
void ShareConfig::setAndroidEnabled(bool enabled) { set(QStringLiteral("Android/enabled"), enabled); }
bool ShareConfig::dropEdgeEnabled() const { return value(QStringLiteral("Edge/dropBubbles"), true).toBool(); }
void ShareConfig::setDropEdgeEnabled(bool enabled) { set(QStringLiteral("Edge/dropBubbles"), enabled); }
QString ShareConfig::edgePeer() const { return value(QStringLiteral("Edge/peer"), QString()).toString(); }
void ShareConfig::setEdgePeer(const QString &fingerprint) { set(QStringLiteral("Edge/peer"), fingerprint.toUpper()); }
QString ShareConfig::edgePeerName() const { return value(QStringLiteral("Edge/peerName"), QString()).toString(); }
void ShareConfig::setEdgePeerName(const QString &name) { set(QStringLiteral("Edge/peerName"), name); }

QString ShareConfig::edgeSide() const
{
    const QString side = value(QStringLiteral("Edge/side"), QStringLiteral("none")).toString();
    static const QStringList sides = {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("top"), QStringLiteral("none")};
    return sides.contains(side) ? side : QStringLiteral("none");
}

void ShareConfig::setEdgeSide(const QString &side) { set(QStringLiteral("Edge/side"), side); }

int ShareConfig::port() const
{
    if (m_port > 0)
        return m_port;
    // Tests run several instances side by side
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("LINGMO_SHARE_PORT", &ok);
    return ok && port > 0 && port < 65536 ? port : 53317;
}

bool ShareConfig::pluginEnabled(const QString &plugin) const
{
    return value(s_plugins + plugin, true).toBool();
}

void ShareConfig::setPluginEnabled(const QString &plugin, bool enabled)
{
    set(s_plugins + plugin, enabled);
}

QVariantList ShareConfig::trustedDevices() const
{
    QVariantList list;
    QSettings &s = *m_settings;
    s.beginGroup(QStringLiteral("Trusted"));
    const QStringList keys = s.childKeys();
    for (const QString &fp : keys)
        list << QVariantMap{{QStringLiteral("fingerprint"), fp}, {QStringLiteral("alias"), s.value(fp).toString()}};
    s.endGroup();
    return list;
}

bool ShareConfig::isTrusted(const QString &fingerprint) const
{
    if (fingerprint.isEmpty())
        return false;
    return m_settings->contains(QStringLiteral("Trusted/") + fingerprint.toUpper());
}

void ShareConfig::trust(const QString &fingerprint, const QString &alias)
{
    if (fingerprint.isEmpty())
        return;
    set(QStringLiteral("Trusted/") + fingerprint.toUpper(), alias);
}

void ShareConfig::untrust(const QString &fingerprint)
{
    m_settings->remove(QStringLiteral("Trusted/") + fingerprint.toUpper());
    m_settings->sync();
    emit changed();
}

QString ShareConfig::dataDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/lingmo-share");
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return dir;
}
