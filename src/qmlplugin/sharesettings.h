/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SHARESETTINGS_H
#define SHARESETTINGS_H

#include <QQmlEngine>

#include "common/shareconfig.h"

/*
 * The settings of Lingmo Share for QML (the page in Lingmo Settings and the
 * app). Same file as the daemon; the daemon notices changes by itself.
 */
class ShareSettings : public ShareConfig
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool androidInstalled READ androidInstalled NOTIFY androidInstalledChanged)
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

public:
    explicit ShareSettings(QObject *parent = nullptr);

    bool androidInstalled() const;
    bool daemonRunning() const;
    QVariantList devices() const { return m_devices; }

    // Asks the daemon for the devices around (LocalSend and paired phones)
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void installAndroidSupport();
    Q_INVOKABLE void openApp(const QString &page = QString());
    Q_INVOKABLE QString pluginTitle(const QString &plugin) const;
    Q_INVOKABLE QString pluginDescription(const QString &plugin) const;
    Q_INVOKABLE QStringList plugins() const { return knownPlugins(); }
    Q_INVOKABLE QString randomPin() const;

signals:
    void androidInstalledChanged();
    void devicesChanged();

private:
    QVariantList m_devices;
};

#endif
