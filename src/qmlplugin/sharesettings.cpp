/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sharesettings.h"
#include "kdeconnect/kdeconnect.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QProcess>
#include <QRandomGenerator>
#include <QTimer>
#include <QTranslator>

static const QString s_service = QStringLiteral("com.lingmo.Share");

// The page lives in another program (Lingmo Settings): bring our translations
static void installTranslator()
{
    static bool done = false;
    if (done || !QCoreApplication::instance())
        return;
    done = true;
    auto *t = new QTranslator(QCoreApplication::instance());
    const QString dir = qEnvironmentVariableIsSet("LINGMO_SHARE_TRANSLATIONS") ? qEnvironmentVariable("LINGMO_SHARE_TRANSLATIONS")
                                                                             : QStringLiteral(TRANSLATIONS_DIR);
    if (t->load(QLocale::system(), QStringLiteral("lingmo-share"), QStringLiteral("_"), dir))
        QCoreApplication::installTranslator(t);
    else
        delete t;
}

ShareSettings::ShareSettings(QObject *parent)
    : ShareConfig(parent)
{
    installTranslator();
    QTimer::singleShot(0, this, &ShareSettings::refreshDevices);
}

bool ShareSettings::androidInstalled() const
{
    return KdeConnect::installed();
}

bool ShareSettings::daemonRunning() const
{
    return QDBusConnection::sessionBus().interface()->isServiceRegistered(s_service);
}

void ShareSettings::refreshDevices()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(s_service, QStringLiteral("/Share"), s_service, QStringLiteral("Devices"));
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        m_devices.clear();
        if (!reply.isError())
            m_devices = QJsonDocument::fromJson(reply.value().toUtf8()).array().toVariantList();
        emit devicesChanged();
    });
}

void ShareSettings::installAndroidSupport()
{
    QProcess::startDetached(QStringLiteral("lingmo-store"), {QStringLiteral("--app"), QStringLiteral("org.kde.kdeconnect")});
    auto *timer = new QTimer(this);
    timer->setInterval(3000);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        if (KdeConnect::installed()) {
            timer->deleteLater();
            emit androidInstalledChanged();
        }
    });
    timer->start();
}

void ShareSettings::openApp(const QString &page)
{
    QProcess::startDetached(QStringLiteral("lingmo-share"), page.isEmpty() ? QStringList() : QStringList{QStringLiteral("--page"), page});
}

QString ShareSettings::pluginTitle(const QString &plugin) const
{
    if (plugin == QLatin1String("kdeconnect_share")) return tr("Send and receive files");
    if (plugin == QLatin1String("kdeconnect_clipboard")) return tr("Shared clipboard");
    if (plugin == QLatin1String("kdeconnect_notifications")) return tr("Phone notifications");
    if (plugin == QLatin1String("kdeconnect_sms")) return tr("Messages (SMS)");
    if (plugin == QLatin1String("kdeconnect_battery")) return tr("Battery");
    if (plugin == QLatin1String("kdeconnect_connectivity_report")) return tr("Signal");
    if (plugin == QLatin1String("kdeconnect_findmyphone")) return tr("Find my phone");
    if (plugin == QLatin1String("kdeconnect_mprisremote")) return tr("Control the phone's media");
    if (plugin == QLatin1String("kdeconnect_mousepad")) return tr("Remote input");
    if (plugin == QLatin1String("kdeconnect_presenter")) return tr("Presenter");
    return plugin;
}

QString ShareSettings::pluginDescription(const QString &plugin) const
{
    if (plugin == QLatin1String("kdeconnect_share")) return tr("Files from the phone go to the receive folder.");
    if (plugin == QLatin1String("kdeconnect_clipboard")) return tr("Copy on one, paste on the other.");
    if (plugin == QLatin1String("kdeconnect_notifications")) return tr("Show the phone's notifications here, with reply when the app allows it.");
    if (plugin == QLatin1String("kdeconnect_sms")) return tr("Read and send text messages from this computer.");
    if (plugin == QLatin1String("kdeconnect_battery")) return tr("Show the phone's charge in the status bar.");
    if (plugin == QLatin1String("kdeconnect_connectivity_report")) return tr("Show the phone's mobile signal.");
    if (plugin == QLatin1String("kdeconnect_findmyphone")) return tr("Make the phone ring, even when silent.");
    if (plugin == QLatin1String("kdeconnect_mprisremote")) return tr("Play, pause and skip what plays on the phone.");
    if (plugin == QLatin1String("kdeconnect_mousepad")) return tr("Use the phone as a touchpad and keyboard for this computer.");
    if (plugin == QLatin1String("kdeconnect_presenter")) return tr("Use the phone as a slide clicker and pointer.");
    return {};
}

QString ShareSettings::randomPin() const
{
    return QString::number(QRandomGenerator::system()->bounded(100000, 1000000));
}
