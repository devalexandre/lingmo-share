/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SHARECONFIG_H
#define SHARECONFIG_H

#include <QObject>
#include <QVariantList>

class QFileSystemWatcher;
class QSettings;

/*
 * The settings of Lingmo Share (~/.config/lingmoos/share.conf), shared by the
 * daemon, the app, the CLI and the page in Lingmo Settings. Every process
 * watches the file, so a change made in one shows up in the others.
 */
class ShareConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY changed)
    // Visibility::Everyone, Visibility::TrustedOnly, Visibility::Off
    Q_PROPERTY(int visibility READ visibility WRITE setVisibility NOTIFY changed)
    Q_PROPERTY(QString receiveFolder READ receiveFolder WRITE setReceiveFolder NOTIFY changed)
    Q_PROPERTY(bool localSendEnabled READ localSendEnabled WRITE setLocalSendEnabled NOTIFY changed)
    Q_PROPERTY(bool pinEnabled READ pinEnabled WRITE setPinEnabled NOTIFY changed)
    Q_PROPERTY(QString pin READ pin WRITE setPin NOTIFY changed)
    Q_PROPERTY(bool androidEnabled READ androidEnabled WRITE setAndroidEnabled NOTIFY changed)
    Q_PROPERTY(bool dropEdgeEnabled READ dropEdgeEnabled WRITE setDropEdgeEnabled NOTIFY changed)
    // Lingmo next to Lingmo: the other PC (LocalSend fingerprint) and the side it sits on
    Q_PROPERTY(QString edgePeer READ edgePeer WRITE setEdgePeer NOTIFY changed)
    Q_PROPERTY(QString edgePeerName READ edgePeerName WRITE setEdgePeerName NOTIFY changed)
    Q_PROPERTY(QString edgeSide READ edgeSide WRITE setEdgeSide NOTIFY changed)
    Q_PROPERTY(QVariantList trustedDevices READ trustedDevices NOTIFY changed)
    Q_PROPERTY(int port READ port CONSTANT)

public:
    enum Visibility { Everyone = 0, TrustedOnly = 1, Off = 2 };
    Q_ENUM(Visibility)

    explicit ShareConfig(QObject *parent = nullptr);
    // A settings file of its own (tests run two peers in one process)
    ShareConfig(const QString &file, int port, QObject *parent = nullptr);
    ~ShareConfig() override;

    // KDE Connect plugins Lingmo turns on or off (kdeconnect_<name>)
    static QStringList knownPlugins();

    QString deviceName() const;
    void setDeviceName(const QString &name);
    int visibility() const;
    void setVisibility(int visibility);
    QString receiveFolder() const;
    void setReceiveFolder(const QString &folder);
    bool localSendEnabled() const;
    void setLocalSendEnabled(bool enabled);
    bool pinEnabled() const;
    void setPinEnabled(bool enabled);
    QString pin() const;
    void setPin(const QString &pin);
    bool androidEnabled() const;
    void setAndroidEnabled(bool enabled);
    bool dropEdgeEnabled() const;
    void setDropEdgeEnabled(bool enabled);
    QString edgePeer() const;
    void setEdgePeer(const QString &fingerprint);
    QString edgePeerName() const;
    void setEdgePeerName(const QString &name);
    QString edgeSide() const;
    void setEdgeSide(const QString &side);
    int port() const;

    Q_INVOKABLE bool pluginEnabled(const QString &plugin) const;
    Q_INVOKABLE void setPluginEnabled(const QString &plugin, bool enabled);

    QVariantList trustedDevices() const;
    Q_INVOKABLE bool isTrusted(const QString &fingerprint) const;
    Q_INVOKABLE void trust(const QString &fingerprint, const QString &alias);
    Q_INVOKABLE void untrust(const QString &fingerprint);

    // The PIN asked from senders, or empty when none is required
    QString requiredPin() const;

    // Where the TLS identity and other private files live
    static QString dataDir();

signals:
    void changed();

private:
    QVariant value(const QString &key, const QVariant &fallback) const;
    void set(const QString &key, const QVariant &value);
    void watch();
    void reload();

    QSettings *m_settings;
    QFileSystemWatcher *m_watcher;
    int m_port = 0;
};

#endif
