/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KDECONNECT_H
#define KDECONNECT_H

#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QVariantMap>

#include <functional>

class ShareConfig;

// An Android phone (or tablet) as kdeconnectd sees it
struct KdcDevice {
    QString id;
    QString name;
    QString type; // smartphone | tablet | desktop | laptop | tv
    QString iconName;
    bool reachable = false;
    int pairState = 0; // 0 not paired, 1 we asked, 2 the phone asks, 3 paired
    QString verificationKey;
    QStringList plugins; // loaded
    int battery = -1;
    bool charging = false;
    int signal = -1; // 0..4
    QString networkType;

    bool paired() const { return pairState == 3; }
    QVariantMap toMap() const;
};

/*
 * Talks to kdeconnectd over its D-Bus API (org.kde.kdeconnect): Lingmo shows
 * its own UI, the protocol and the encryption stay in KDE Connect. Starts the
 * daemon by D-Bus activation when Android support is on.
 */
class KdeConnect : public QObject
{
    Q_OBJECT
public:
    explicit KdeConnect(ShareConfig *config, QObject *parent = nullptr);

    static bool installed();
    bool available() const { return m_available; }

    // Starts or stops talking to kdeconnectd according to the settings
    void apply();

    QList<KdcDevice> devices() const { return m_devices.values(); }
    bool device(const QString &id, KdcDevice *out) const;

    void requestPairing(const QString &id);
    void acceptPairing(const QString &id);
    void rejectPairing(const QString &id);
    void unpair(const QString &id);
    void ring(const QString &id);
    void shareFiles(const QString &id, const QStringList &paths);
    void shareText(const QString &id, const QString &text);
    void sendClipboard(const QString &id);
    void mediaAction(const QString &id, const QString &action);
    void setMediaVolume(const QString &id, int volume);

    // Phone notifications currently shown (with reply where the app allows it)
    QVariantList notifications(const QString &id);
    void replyNotification(const QString &id, const QString &publicId, const QString &text);
    void dismissNotification(const QString &id, const QString &publicId);

    QVariantMap media(const QString &id);

    // SMS (conversations interface)
    QVariantList conversations(const QString &id);
    void requestConversations(const QString &id);
    void requestConversation(const QString &id, qint64 thread, int count);
    void replyToConversation(const QString &id, qint64 thread, const QString &text);
    void sendSms(const QString &id, const QString &number, const QString &text);

    static QString devicePath(const QString &id) { return QStringLiteral("/modules/kdeconnect/devices/") + id; }

signals:
    void availableChanged();
    void devicesChanged();
    void pairingRequested(const QString &id);
    void fileReceived(const QString &id, const QString &url);
    void notificationsChanged(const QString &id);
    void notificationPosted(const QString &id, const QString &publicId);
    void mediaChanged(const QString &id);
    // A message (struct) from the conversations interface, decoded
    void smsMessage(const QString &id, const QVariantMap &message);
    void conversationLoaded(const QString &id, qint64 thread);

private slots:
    void onSignal(const QDBusMessage &message);

private:
    void setAvailable(bool available);
    void refreshAll();
    void refreshDevice(const QString &id);
    void applyDeviceSettings(const QString &id);
    void applyIncomingFolder(const QString &id);
    void applyVisibility();
    void call(const QString &path, const QString &iface, const QString &method, const QVariantList &args = {},
              std::function<void(const QDBusMessage &)> done = {});
    QVariantMap properties(const QString &path, const QString &iface);

    ShareConfig *m_config;
    bool m_available = false;
    bool m_connected = false;
    QHash<QString, KdcDevice> m_devices;
    QString m_appliedName;
};

#endif
