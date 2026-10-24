/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DAEMON_H
#define DAEMON_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include "common/shareconfig.h"
#include "devicesmodel.h"
#include "smsmodel.h"
#include "transfers.h"

class QQmlApplicationEngine;
class QQuickWindow;
class QSystemTrayIcon;
class QMenu;
class DevicesModel;
class DragWatcher;
class Feedback;
class JobViewServer;
class KdeConnect;
class LocalSend;
class LocalSendSender;
class ShareConfig;
class SmsModel;
class Transfers;

/*
 * The Lingmo Share daemon: LocalSend server and discovery, the KDE Connect
 * client, transfers, the drop bubbles shown while dragging, the tray
 * indicator, and (on demand) the app window. Exposed to QML as "share" and on
 * D-Bus as com.lingmo.Share.
 */
class Daemon : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ShareConfig *config READ config CONSTANT)
    Q_PROPERTY(DevicesModel *devices READ devices CONSTANT)
    Q_PROPERTY(Transfers *transfers READ transfers CONSTANT)
    Q_PROPERTY(SmsModel *sms READ sms CONSTANT)
    Q_PROPERTY(bool androidInstalled READ androidInstalled NOTIFY androidChanged)
    Q_PROPERTY(bool androidAvailable READ androidAvailable NOTIFY androidChanged)
    Q_PROPERTY(bool installingAndroid READ installingAndroid NOTIFY androidChanged)
    Q_PROPERTY(QString localSendState READ localSendState NOTIFY localSendChanged)
    Q_PROPERTY(QString localSendError READ localSendError NOTIFY localSendChanged)
    Q_PROPERTY(QString fingerprint READ fingerprint CONSTANT)
    Q_PROPERTY(QString firewall READ firewall NOTIFY firewallChanged)
    Q_PROPERTY(QString firewallCommand READ firewallCommand NOTIFY firewallChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)
    Q_PROPERTY(QVariantList sendTargets READ sendTargets NOTIFY sendTargetsChanged)

public:
    explicit Daemon(QObject *parent = nullptr);
    ~Daemon() override;

    bool start();

    ShareConfig *config() const { return m_config; }
    DevicesModel *devices() const { return m_devices; }
    Transfers *transfers() const { return m_transfers; }
    SmsModel *sms() const { return m_sms; }
    bool androidInstalled() const;
    bool androidAvailable() const;
    bool installingAndroid() const { return m_installing; }
    QString localSendState() const;
    QString localSendError() const;
    QString fingerprint() const;
    QString firewall() const { return m_firewall; }
    QString firewallCommand() const;
    bool dragging() const;
    QVariantList sendTargets() const;

    // Windows
    Q_INVOKABLE void showWindow(const QString &page = QString());
    void pickAndSend(const QStringList &paths);

    // Devices
    Q_INVOKABLE void pair(const QString &id);
    Q_INVOKABLE void unpair(const QString &id);
    Q_INVOKABLE void acceptPairing(const QString &id);
    Q_INVOKABLE void rejectPairing(const QString &id);
    Q_INVOKABLE void ring(const QString &id);
    Q_INVOKABLE void sendClipboard(const QString &id);
    Q_INVOKABLE void refresh(bool httpScan = false);
    Q_INVOKABLE void setDiscovering(bool on);
    Q_INVOKABLE void trust(const QString &id, bool trusted);
    Q_INVOKABLE void installAndroidSupport();
    Q_INVOKABLE void copyText(const QString &text);

    // Transfers
    Q_INVOKABLE QString send(const QString &deviceId, const QVariant &paths);
    Q_INVOKABLE QString sendText(const QString &deviceId, const QString &text);
    Q_INVOKABLE void cancelTransfer(const QString &transferId);
    Q_INVOKABLE void sendPin(const QString &transferId, const QString &pin);
    Q_INVOKABLE void showInFolder(const QString &transferId);
    Q_INVOKABLE QVariantMap transfer(const QString &transferId) const;
    Q_INVOKABLE void answerIncoming(const QString &sessionId, bool accept, bool trust);
    Q_INVOKABLE QVariantMap incoming(const QString &sessionId) const;

    // Android extras
    Q_INVOKABLE QVariantList phoneNotifications(const QString &id);
    Q_INVOKABLE void replyNotification(const QString &id, const QString &publicId, const QString &text);
    Q_INVOKABLE void dismissNotification(const QString &id, const QString &publicId);
    Q_INVOKABLE QVariantMap media(const QString &id);
    Q_INVOKABLE void mediaAction(const QString &id, const QString &action);
    Q_INVOKABLE void setMediaVolume(const QString &id, int volume);

    // Drop bubbles
    Q_INVOKABLE void dropFinished();

    QString devicesJson() const;

signals:
    void androidChanged();
    void localSendChanged();
    void firewallChanged();
    void draggingChanged();
    void sendTargetsChanged();
    void transferChanged(const QString &transferId);
    void phoneNotificationsChanged(const QString &id);
    void mediaChanged(const QString &id);
    void pinNeeded(const QString &transferId, const QString &device);

private:
    QQmlApplicationEngine *engine();
    QQuickWindow *createWindow(const QString &file, const QVariantMap &props = {});
    void setupTray();
    void updateTray();
    void detectFirewall();
    void onIncomingRequest(const QString &sessionId);
    void onIncomingFinished(const QString &sessionId, const QStringList &paths, bool ok, const QString &error);
    void onPairingRequested(const QString &id);
    void onJobStarted(int view, const QString &desktopEntry);
    void onJobUpdated(int view, const QVariantMap &state);
    void onJobFinished(int view, uint errorCode, const QString &errorMessage);
    void onDragStarted();
    void onDragEnded();
    void trackSender(const QString &transferId, LocalSendSender *sender);
    QString edgeSideFor(const QString &fingerprint) const;

    ShareConfig *m_config;
    LocalSend *m_localSend;
    KdeConnect *m_kdc;
    DevicesModel *m_devices;
    Transfers *m_transfers;
    SmsModel *m_sms;
    Feedback *m_feedback;
    JobViewServer *m_jobs;
    DragWatcher *m_drag;

    QQmlApplicationEngine *m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    QPointer<QQuickWindow> m_dropEdge;
    QPointer<QQuickWindow> m_picker;
    QHash<QString, QPointer<QQuickWindow>> m_dialogs;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;

    QHash<QString, QPointer<LocalSendSender>> m_senders;  // transfer id -> sender
    QHash<QString, QString> m_incomingTransfer;           // LocalSend session -> transfer id
    QHash<QString, QString> m_incomingSender;             // LocalSend session -> proven fingerprint
    QHash<int, QString> m_jobTransfer;                    // job view -> transfer id
    int m_discoverers = 0;
    bool m_installing = false;
    QString m_firewall;
};

#endif
