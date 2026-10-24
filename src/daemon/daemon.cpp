/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "daemon.h"
#include "devicesmodel.h"
#include "dragwatcher.h"
#include "feedback.h"
#include "jobviewserver.h"
#include "smsmodel.h"
#include "transfers.h"
#include "common/shareconfig.h"
#include "kdeconnect/kdeconnect.h"
#include "localsend/localsend.h"
#include "localsend/sender.h"

#include <QApplication>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>

Daemon::Daemon(QObject *parent)
    : QObject(parent)
    , m_config(new ShareConfig(this))
    , m_localSend(new LocalSend(m_config, this))
    , m_kdc(new KdeConnect(m_config, this))
    , m_devices(new DevicesModel(m_kdc, m_localSend, m_config, this))
    , m_transfers(new Transfers(this))
    , m_sms(new SmsModel(m_kdc, this))
    , m_feedback(new Feedback(this))
    , m_jobs(new JobViewServer(this))
    , m_drag(new DragWatcher(this))
{
}

Daemon::~Daemon()
{
    for (const auto &sender : std::as_const(m_senders)) {
        if (sender)
            sender->cancel();
    }
}

bool Daemon::start()
{
    m_localSend->init(ShareConfig::dataDir());
    connect(m_config, &ShareConfig::changed, this, [this] {
        m_localSend->apply();
        emit localSendChanged();
        updateTray();
    });
    connect(m_localSend, &LocalSend::modeChanged, this, &Daemon::localSendChanged);
    connect(m_localSend, &LocalSend::incomingRequest, this, &Daemon::onIncomingRequest);
    connect(m_localSend, &LocalSend::incomingWithdrawn, this, [this](const QString &id) {
        if (QQuickWindow *w = m_dialogs.take(id))
            w->close();
        const QString t = m_incomingTransfer.take(id);
        if (!t.isEmpty())
            m_transfers->setState(t, Transfers::Cancelled, tr("The sender cancelled."));
    });
    connect(m_localSend, &LocalSend::incomingStarted, this, [this](const QString &session) {
        const QVariantMap info = m_localSend->incomingInfo(session);
        Transfers::Transfer t;
        t.incoming = true;
        t.device = info.value(QStringLiteral("name")).toString();
        t.deviceIcon = DevicesModel::iconFor(QStringLiteral("localsend"), info.value(QStringLiteral("deviceType")).toString());
        QStringList names;
        for (const QVariant &f : info.value(QStringLiteral("files")).toList())
            names << f.toMap().value(QStringLiteral("name")).toString();
        t.title = Transfers::describe(names, names.size());
        t.total = info.value(QStringLiteral("total")).toLongLong();
        t.protocol = QStringLiteral("localsend");
        const QString id = m_transfers->add(t);
        m_incomingTransfer.insert(session, id);
        if (info.value(QStringLiteral("verified")).toBool())
            m_incomingSender.insert(session, info.value(QStringLiteral("fingerprint")).toString());
        m_feedback->start(id, tr("Receiving from %1").arg(t.device), t.title, QStringLiteral("document-save"), 0);
    });
    connect(m_localSend, &LocalSend::incomingProgress, this, [this](const QString &session, qint64 done, qint64 total) {
        const QString id = m_incomingTransfer.value(session);
        if (id.isEmpty())
            return;
        m_transfers->setProgress(id, done, total);
        Transfers::Transfer t;
        if (m_transfers->get(id, &t))
            m_feedback->update(id, t.title, total > 0 ? qreal(done) / total : 0);
    });
    connect(m_localSend, &LocalSend::incomingFinished, this, &Daemon::onIncomingFinished);
    connect(m_localSend, &LocalSend::messageReceived, this, [this](const QString &, const QString &alias, const QString &text) {
        // Texts are shown, never opened or run; copying is the user's choice
        m_feedback->notify(tr("Message from %1").arg(alias), text.left(500), QStringLiteral("mail-message"),
                           {QStringLiteral("copy"), tr("Copy")}, [this, text](const QString &action) {
            if (action == QLatin1String("copy"))
                copyText(text);
        });
    });

    connect(m_kdc, &KdeConnect::availableChanged, this, &Daemon::androidChanged);
    connect(m_kdc, &KdeConnect::devicesChanged, this, [this] {
        updateTray();
        emit sendTargetsChanged();
    });
    connect(m_localSend, &LocalSend::devicesChanged, this, &Daemon::sendTargetsChanged);
    connect(m_kdc, &KdeConnect::pairingRequested, this, &Daemon::onPairingRequested);
    connect(m_kdc, &KdeConnect::notificationsChanged, this, &Daemon::phoneNotificationsChanged);
    connect(m_kdc, &KdeConnect::mediaChanged, this, &Daemon::mediaChanged);

    connect(this, &Daemon::pinNeeded, this, [this](const QString &transferId, const QString &device) {
        QQuickWindow *w = createWindow(QStringLiteral("PinDialog.qml"), {{QStringLiteral("transferId"), transferId},
                                                                        {QStringLiteral("deviceName"), device}});
        if (!w)
            return;
        connect(w, &QWindow::visibleChanged, w, [w](bool visible) {
            if (!visible)
                w->deleteLater();
        });
        w->show();
        w->requestActivate();
    });
    connect(m_transfers, &Transfers::changed, this, &Daemon::transferChanged);
    connect(m_transfers, &Transfers::countChanged, this, &Daemon::updateTray);
    connect(m_feedback, &Feedback::cancelRequested, this, &Daemon::cancelTransfer);

    if (m_jobs->registerService()) {
        connect(m_jobs, &JobViewServer::jobStarted, this, &Daemon::onJobStarted);
        connect(m_jobs, &JobViewServer::jobUpdated, this, &Daemon::onJobUpdated);
        connect(m_jobs, &JobViewServer::jobFinished, this, &Daemon::onJobFinished);
    }

    if (m_drag->start()) {
        connect(m_drag, &DragWatcher::dragStarted, this, &Daemon::onDragStarted);
        connect(m_drag, &DragWatcher::dragEnded, this, &Daemon::onDragEnded);
    }

    m_localSend->apply();
    m_kdc->apply();
    setupTray();
    detectFirewall();
    return true;
}

// ------------------------------------------------------------------ state

bool Daemon::androidInstalled() const { return KdeConnect::installed(); }
bool Daemon::androidAvailable() const { return m_kdc->available(); }
QString Daemon::fingerprint() const { return m_localSend->identity().fingerprint(); }
bool Daemon::dragging() const { return m_drag->dragging(); }
QVariantList Daemon::sendTargets() const { return m_devices->sendTargets(); }

QString Daemon::localSendState() const
{
    switch (m_localSend->mode()) {
    case LocalSend::Mode::Full: return QStringLiteral("on");
    case LocalSend::Mode::DiscoverOnly: return QStringLiteral("discovering");
    default: return QStringLiteral("off");
    }
}

QString Daemon::localSendError() const { return m_localSend->error(); }

QString Daemon::firewallCommand() const
{
    const QString port = QString::number(m_config->port());
    if (m_firewall == QLatin1String("ufw"))
        return QStringLiteral("sudo ufw allow %1/tcp && sudo ufw allow %1/udp && sudo ufw allow 1714:1764/udp && sudo ufw allow 1714:1764/tcp").arg(port);
    if (m_firewall == QLatin1String("firewalld"))
        return QStringLiteral("sudo firewall-cmd --permanent --add-port=%1/tcp --add-port=%1/udp --add-service=kdeconnect && sudo firewall-cmd --reload").arg(port);
    return {};
}

void Daemon::detectFirewall()
{
    // Only reported: the rules are the user's to change
    for (const QString &unit : {QStringLiteral("ufw"), QStringLiteral("firewalld")}) {
        auto *p = new QProcess(this);
        connect(p, &QProcess::finished, this, [this, p, unit](int code) {
            p->deleteLater();
            if (code == 0 && m_firewall.isEmpty()) {
                m_firewall = unit;
                emit firewallChanged();
            }
        });
        connect(p, &QProcess::errorOccurred, p, &QObject::deleteLater);
        p->start(QStringLiteral("systemctl"), {QStringLiteral("is-active"), QStringLiteral("--quiet"), unit});
    }
}

// ------------------------------------------------------------------ windows

QQmlApplicationEngine *Daemon::engine()
{
    if (!m_engine) {
        m_engine = new QQmlApplicationEngine(this);
        m_engine->rootContext()->setContextProperty(QStringLiteral("share"), this);
    }
    return m_engine;
}

QQuickWindow *Daemon::createWindow(const QString &file, const QVariantMap &props)
{
    QQmlComponent component(engine(), QUrl(QStringLiteral("qrc:/qml/") + file));
    QObject *obj = component.createWithInitialProperties(props, m_engine->rootContext());
    if (!obj) {
        qWarning() << component.errors();
        return nullptr;
    }
    auto *window = qobject_cast<QQuickWindow *>(obj);
    if (!window) {
        delete obj;
        return nullptr;
    }
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
    return window;
}

void Daemon::showWindow(const QString &page)
{
    if (!m_window) {
        m_window = createWindow(QStringLiteral("Main.qml"));
        if (!m_window)
            return;
        connect(m_window, &QWindow::visibleChanged, this, [this](bool visible) {
            // Closed: free the window (the daemon stays small)
            if (!visible && m_window)
                m_window->deleteLater();
        });
    }
    if (!page.isEmpty())
        QMetaObject::invokeMethod(m_window, "openPage", Q_ARG(QVariant, page));
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

void Daemon::pickAndSend(const QStringList &paths)
{
    QStringList files;
    for (const QString &p : paths) {
        const QString local = p.startsWith(QLatin1String("file://")) ? QUrl(p).toLocalFile() : p;
        if (QFileInfo::exists(local))
            files << QFileInfo(local).absoluteFilePath();
    }
    if (files.isEmpty())
        return;
    if (m_picker)
        m_picker->close();
    m_picker = createWindow(QStringLiteral("Picker.qml"), {{QStringLiteral("files"), files}});
    if (!m_picker)
        return;
    connect(m_picker, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible && m_picker)
            m_picker->deleteLater();
    });
    m_picker->show();
    m_picker->requestActivate();
}

void Daemon::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable() && !qEnvironmentVariableIsSet("LINGMO_SHARE_TRAY"))
        return;
    m_tray = new QSystemTrayIcon(QIcon::fromTheme(QStringLiteral("smartphone"), QIcon::fromTheme(QStringLiteral("phone"))), this);
    m_trayMenu = new QMenu;
    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            showWindow();
    });
    updateTray();
}

void Daemon::updateTray()
{
    if (!m_tray)
        return;
    // Shown only when there is something to show: a paired phone or a transfer
    QStringList lines;
    m_trayMenu->clear();
    for (const KdcDevice &d : m_kdc->devices()) {
        if (!d.paired())
            continue;
        QString line = d.name;
        if (!d.reachable)
            line += QStringLiteral(" · ") + tr("away");
        else if (d.battery >= 0)
            line += QStringLiteral(" · %1%").arg(d.battery) + (d.charging ? QStringLiteral(" ⚡") : QString());
        lines << line;
        QAction *a = m_trayMenu->addAction(QIcon::fromTheme(QStringLiteral("smartphone")), line);
        a->setEnabled(false);
        if (d.reachable) {
            const QString id = d.id;
            m_trayMenu->addAction(QIcon::fromTheme(QStringLiteral("find-location")), tr("Ring %1").arg(d.name), this,
                                  [this, id] { m_kdc->ring(id); });
        }
    }
    const int active = m_transfers->activeCount();
    if (active > 0)
        lines << tr("%n transfer(s) in progress", nullptr, active);
    if (!m_trayMenu->isEmpty())
        m_trayMenu->addSeparator();
    m_trayMenu->addAction(QIcon::fromTheme(QStringLiteral("smartphone")), tr("Open Phone and devices"), this, [this] { showWindow(); });
    m_tray->setToolTip(lines.isEmpty() ? tr("Phone and devices") : lines.join(QLatin1Char('\n')));
    m_tray->setVisible(!lines.isEmpty() || qEnvironmentVariableIsSet("LINGMO_SHARE_TRAY"));
}

// ------------------------------------------------------------------ devices

static QString plainId(const QString &id) { return id.section(QLatin1Char(':'), 1); }
static bool isAndroid(const QString &id) { return id.startsWith(QLatin1String("kdc:")); }

void Daemon::pair(const QString &id) { if (isAndroid(id)) m_kdc->requestPairing(plainId(id)); }
void Daemon::unpair(const QString &id) { if (isAndroid(id)) m_kdc->unpair(plainId(id)); }
void Daemon::acceptPairing(const QString &id) { m_kdc->acceptPairing(isAndroid(id) ? plainId(id) : id); }
void Daemon::rejectPairing(const QString &id) { m_kdc->rejectPairing(isAndroid(id) ? plainId(id) : id); }
void Daemon::ring(const QString &id) { if (isAndroid(id)) m_kdc->ring(plainId(id)); }
void Daemon::sendClipboard(const QString &id) { if (isAndroid(id)) m_kdc->sendClipboard(plainId(id)); }
void Daemon::refresh(bool httpScan) { m_localSend->refresh(httpScan); m_kdc->apply(); }

void Daemon::setDiscovering(bool on)
{
    m_discoverers = qMax(0, m_discoverers + (on ? 1 : -1));
    m_localSend->setDiscovering(m_discoverers > 0);
}

void Daemon::trust(const QString &id, bool trusted)
{
    if (isAndroid(id))
        return;
    const QString fp = plainId(id);
    if (!trusted) {
        m_config->untrust(fp);
        return;
    }
    LsDevice d;
    // Only devices proven by their certificate can be trusted
    if (m_localSend->device(fp, &d) && d.verified)
        m_config->trust(fp, d.alias);
}

void Daemon::installAndroidSupport()
{
    // The Store installs packages with its own authorization flow
    if (QProcess::startDetached(QStringLiteral("lingmo-store"), {QStringLiteral("--app"), QStringLiteral("org.kde.kdeconnect")})) {
        m_installing = true;
        emit androidChanged();
        // Look again once it may be there
        auto *timer = new QTimer(this);
        timer->setInterval(3000);
        connect(timer, &QTimer::timeout, this, [this, timer] {
            if (KdeConnect::installed()) {
                timer->deleteLater();
                m_installing = false;
                m_kdc->apply();
                emit androidChanged();
            }
        });
        timer->start();
        QTimer::singleShot(10 * 60 * 1000, timer, [this, timer] {
            timer->deleteLater();
            m_installing = false;
            emit androidChanged();
        });
    }
}

void Daemon::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

QString Daemon::devicesJson() const
{
    QJsonArray array;
    for (int i = 0; i < m_devices->rowCount(); ++i) {
        const QVariantMap m = m_devices->get(m_devices->data(m_devices->index(i), DevicesModel::IdRole).toString());
        array << QJsonObject{{QStringLiteral("id"), m.value(QStringLiteral("deviceId")).toString()},
                             {QStringLiteral("name"), m.value(QStringLiteral("name")).toString()},
                             {QStringLiteral("kind"), m.value(QStringLiteral("kind")).toString()},
                             {QStringLiteral("type"), m.value(QStringLiteral("deviceType")).toString()},
                             {QStringLiteral("paired"), m.value(QStringLiteral("paired")).toBool()},
                             {QStringLiteral("trusted"), m.value(QStringLiteral("trusted")).toBool()},
                             {QStringLiteral("sendable"), m.value(QStringLiteral("sendable")).toBool()},
                             {QStringLiteral("battery"), m.value(QStringLiteral("battery")).toInt()},
                             {QStringLiteral("lingmo"), m.value(QStringLiteral("lingmo")).toBool()}};
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void Daemon::onPairingRequested(const QString &id)
{
    KdcDevice d;
    if (!m_kdc->device(id, &d))
        return;
    const QString key = QStringLiteral("pair:") + id;
    if (m_dialogs.value(key))
        return;
    QQuickWindow *w = createWindow(QStringLiteral("PairRequest.qml"),
                                   {{QStringLiteral("deviceId"), QStringLiteral("kdc:") + id},
                                    {QStringLiteral("deviceName"), d.name},
                                    {QStringLiteral("verificationKey"), d.verificationKey}});
    if (!w)
        return;
    m_dialogs.insert(key, w);
    connect(w, &QWindow::visibleChanged, this, [this, key, w](bool visible) {
        if (!visible) {
            m_dialogs.remove(key);
            w->deleteLater();
        }
    });
    w->show();
    w->requestActivate();
}

// ------------------------------------------------------------------ sending

QString Daemon::send(const QString &deviceId, const QVariant &pathsVariant)
{
    QStringList paths;
    for (const QVariant &v : pathsVariant.toList())
        paths << (v.metaType().id() == QMetaType::QUrl ? v.toUrl().toLocalFile() : v.toString());
    if (paths.isEmpty() && pathsVariant.metaType().id() == QMetaType::QStringList)
        paths = pathsVariant.toStringList();
    const QList<OutgoingFile> files = LocalSendSender::collect(paths);
    if (files.isEmpty())
        return {};
    QStringList names;
    for (const OutgoingFile &f : files)
        names << f.name;

    Transfers::Transfer t;
    t.incoming = false;
    t.title = Transfers::describe(names, files.size());
    for (const OutgoingFile &f : files)
        t.total += f.size;

    if (isAndroid(deviceId)) {
        KdcDevice d;
        if (!m_kdc->device(plainId(deviceId), &d) || !d.paired())
            return {};
        // KDE Connect reports its progress through the job view server
        QStringList local;
        for (const OutgoingFile &f : files)
            local << f.path;
        m_kdc->shareFiles(d.id, local);
        t.device = d.name;
        t.deviceIcon = DevicesModel::iconFor(QStringLiteral("android"), d.type);
        t.protocol = QStringLiteral("kdeconnect");
        t.state = Transfers::Active;
        const QString id = m_transfers->add(t);
        // Without a job server there is no progress: call it sent
        if (!QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.kde.JobViewServer"))
            || m_jobTransfer.isEmpty())
            QTimer::singleShot(1500, this, [this, id] {
                Transfers::Transfer tr;
                if (m_transfers->get(id, &tr) && tr.state == Transfers::Active && !m_jobTransfer.values().contains(id))
                    m_transfers->setState(id, Transfers::Done);
            });
        return id;
    }

    LsDevice d;
    if (!m_localSend->device(plainId(deviceId), &d))
        return {};
    t.device = d.alias;
    t.deviceIcon = DevicesModel::iconFor(QStringLiteral("localsend"), d.deviceType);
    t.protocol = QStringLiteral("localsend");
    const QString id = m_transfers->add(t);
    auto *sender = new LocalSendSender(d, m_localSend->identity(), m_localSend->info(false), this);
    trackSender(id, sender);
    sender->sendFiles(files);
    return id;
}

QString Daemon::sendText(const QString &deviceId, const QString &text)
{
    if (text.isEmpty())
        return {};
    if (isAndroid(deviceId)) {
        m_kdc->shareText(plainId(deviceId), text);
        return {};
    }
    LsDevice d;
    if (!m_localSend->device(plainId(deviceId), &d))
        return {};
    Transfers::Transfer t;
    t.title = tr("Text");
    t.device = d.alias;
    t.deviceIcon = DevicesModel::iconFor(QStringLiteral("localsend"), d.deviceType);
    t.protocol = QStringLiteral("localsend");
    const QString id = m_transfers->add(t);
    auto *sender = new LocalSendSender(d, m_localSend->identity(), m_localSend->info(false), this);
    trackSender(id, sender);
    sender->sendText(text);
    return id;
}

void Daemon::trackSender(const QString &id, LocalSendSender *sender)
{
    m_senders.insert(id, sender);
    const QString device = sender->target().alias;
    connect(sender, &LocalSendSender::progress, this, [this, id](qint64 done, qint64 total) {
        m_transfers->setProgress(id, done, total);
        Transfers::Transfer t;
        if (m_transfers->get(id, &t))
            m_feedback->update(id, t.title, total > 0 ? qreal(done) / total : 0);
    });
    connect(sender, &LocalSendSender::stateChanged, this, [this, id, sender, device](LocalSendSender::State state) {
        switch (state) {
        case LocalSendSender::Waiting:
            m_transfers->setState(id, Transfers::Waiting, tr("Waiting for %1 to accept…").arg(device));
            return;
        case LocalSendSender::NeedPin:
            m_transfers->setState(id, Transfers::Waiting, sender->errorString());
            emit pinNeeded(id, device);
            return;
        case LocalSendSender::Sending: {
            m_transfers->setState(id, Transfers::Active);
            Transfers::Transfer t;
            if (m_transfers->get(id, &t))
                m_feedback->start(id, tr("Sending to %1").arg(device), t.title, QStringLiteral("document-send"), 0);
            return;
        }
        case LocalSendSender::Done:
            m_transfers->setState(id, Transfers::Done);
            m_feedback->finish(id, tr("Sent to %1").arg(device));
            break;
        case LocalSendSender::Declined:
            m_transfers->setState(id, Transfers::Declined, sender->errorString());
            m_feedback->finish(id, QString());
            m_feedback->notify(tr("Not sent"), sender->errorString(), QStringLiteral("dialog-information"));
            break;
        case LocalSendSender::Failed:
            m_transfers->setState(id, Transfers::Failed, sender->errorString());
            m_feedback->finish(id, QString());
            m_feedback->notify(tr("Could not send to %1").arg(device), sender->errorString(), QStringLiteral("dialog-error"));
            break;
        case LocalSendSender::Cancelled:
            m_transfers->setState(id, Transfers::Cancelled);
            m_feedback->finish(id, QString());
            break;
        }
        m_senders.remove(id);
        sender->deleteLater();
    });
}

void Daemon::sendPin(const QString &transferId, const QString &pin)
{
    if (LocalSendSender *s = m_senders.value(transferId))
        s->setPin(pin);
}

void Daemon::cancelTransfer(const QString &transferId)
{
    if (LocalSendSender *s = m_senders.value(transferId)) {
        s->cancel();
        return;
    }
    for (auto it = m_incomingTransfer.cbegin(); it != m_incomingTransfer.cend(); ++it) {
        if (it.value() == transferId) {
            m_localSend->cancelIncoming(it.key());
            return;
        }
    }
    for (auto it = m_jobTransfer.cbegin(); it != m_jobTransfer.cend(); ++it) {
        if (it.value() == transferId) {
            m_jobs->cancel(it.key());
            return;
        }
    }
}

void Daemon::showInFolder(const QString &transferId)
{
    Transfers::Transfer t;
    if (!m_transfers->get(transferId, &t))
        return;
    // The folder is opened, never the received file itself
    const QString folder = t.paths.isEmpty() ? m_config->receiveFolder() : QFileInfo(t.paths.first()).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

QVariantMap Daemon::transfer(const QString &transferId) const
{
    Transfers::Transfer t;
    if (!m_transfers->get(transferId, &t))
        return {};
    return {{QStringLiteral("id"), t.id},
            {QStringLiteral("device"), t.device},
            {QStringLiteral("title"), t.title},
            {QStringLiteral("done"), t.done},
            {QStringLiteral("total"), t.total},
            {QStringLiteral("progress"), t.total > 0 ? qreal(t.done) / t.total : (t.state == Transfers::Done ? 1.0 : 0.0)},
            {QStringLiteral("state"), int(t.state)},
            {QStringLiteral("finished"), t.state != Transfers::Waiting && t.state != Transfers::Active},
            {QStringLiteral("error"), t.error},
            {QStringLiteral("paths"), t.paths}};
}

// ------------------------------------------------------------------ receiving

QString Daemon::edgeSideFor(const QString &fingerprint) const
{
    if (fingerprint.isEmpty() || m_config->edgePeer() != fingerprint.toUpper() || m_config->edgeSide() == QLatin1String("none"))
        return {};
    return m_config->edgeSide();
}

void Daemon::onIncomingRequest(const QString &sessionId)
{
    const QVariantMap info = m_localSend->incomingInfo(sessionId);
    QQuickWindow *w = createWindow(QStringLiteral("IncomingDialog.qml"), {{QStringLiteral("request"), info}});
    if (!w) {
        // No way to ask: refuse rather than accept silently
        m_localSend->decline(sessionId);
        return;
    }
    m_dialogs.insert(sessionId, w);
    connect(w, &QWindow::visibleChanged, this, [this, sessionId, w](bool visible) {
        if (visible)
            return;
        // Closed without an answer: decline
        if (m_dialogs.take(sessionId))
            m_localSend->decline(sessionId);
        w->deleteLater();
    });
    w->show();
    w->raise();
}

QVariantMap Daemon::incoming(const QString &sessionId) const
{
    return m_localSend->incomingInfo(sessionId);
}

void Daemon::answerIncoming(const QString &sessionId, bool accept, bool trustSender)
{
    QPointer<QQuickWindow> w = m_dialogs.take(sessionId);
    if (accept)
        m_localSend->accept(sessionId, trustSender);
    else
        m_localSend->decline(sessionId);
    if (w)
        w->close();
}

void Daemon::onIncomingFinished(const QString &sessionId, const QStringList &paths, bool ok, const QString &error)
{
    const QString id = m_incomingTransfer.take(sessionId);
    if (id.isEmpty())
        return;
    Transfers::Transfer t;
    m_transfers->get(id, &t);
    m_transfers->setPaths(id, paths);
    m_transfers->setState(id, ok ? Transfers::Done : (paths.isEmpty() ? Transfers::Failed : Transfers::Done), error);
    m_feedback->finish(id, ok ? tr("Received from %1").arg(t.device) : QString());

    // From the Lingmo PC next to us (proven by its certificate): fly the file in from that side
    const QString side = edgeSideFor(m_incomingSender.take(sessionId));
    if (!paths.isEmpty() && !side.isEmpty()) {
        QQuickWindow *fly = createWindow(QStringLiteral("FlyIn.qml"), {{QStringLiteral("side"), side},
                                                                      {QStringLiteral("device"), t.device},
                                                                      {QStringLiteral("title"), t.title},
                                                                      {QStringLiteral("path"), paths.first()}});
        if (fly) {
            connect(fly, &QWindow::visibleChanged, fly, [fly](bool visible) {
                if (!visible)
                    fly->deleteLater();
            });
            fly->show();
        }
    }

    if (paths.isEmpty()) {
        m_feedback->notify(tr("Nothing received from %1").arg(t.device), error, QStringLiteral("dialog-error"));
        return;
    }
    // Received files are never opened: the notification offers the folder
    m_feedback->notify(tr("Received from %1").arg(t.device),
                       ok ? tr("%1 saved in %2").arg(t.title, QDir(m_config->receiveFolder()).dirName())
                          : error,
                       QStringLiteral("document-save"),
                       {QStringLiteral("folder"), tr("Show in folder")},
                       [this, id](const QString &action) {
        if (action == QLatin1String("folder") || action == QLatin1String("default"))
            showInFolder(id);
    });
}

// ------------------------------------------------------------------ KDE Connect jobs

void Daemon::onJobStarted(int view, const QString &desktopEntry)
{
    if (!desktopEntry.contains(QLatin1String("kdeconnect")))
        return;
    Transfers::Transfer t;
    t.protocol = QStringLiteral("kdeconnect");
    t.deviceIcon = QStringLiteral("smartphone");
    t.title = tr("File");
    // A send started from here already has a row: adopt it
    QString adopt;
    for (int i = 0; i < m_transfers->rowCount(); ++i) {
        const QModelIndex idx = m_transfers->index(i);
        if (m_transfers->data(idx, Transfers::ProtocolRole).toString() == QLatin1String("kdeconnect")
            && m_transfers->data(idx, Transfers::ActiveRole).toBool()
            && !m_transfers->data(idx, Transfers::IncomingRole).toBool()
            && !m_jobTransfer.values().contains(m_transfers->data(idx, Transfers::IdRole).toString())) {
            adopt = m_transfers->data(idx, Transfers::IdRole).toString();
            break;
        }
    }
    if (adopt.isEmpty()) {
        t.incoming = true;
        adopt = m_transfers->add(t);
    }
    m_jobTransfer.insert(view, adopt);
}

void Daemon::onJobUpdated(int view, const QVariantMap &state)
{
    const QString id = m_jobTransfer.value(view);
    if (id.isEmpty())
        return;
    Transfers::Transfer t;
    if (!m_transfers->get(id, &t))
        return;
    // kdeconnectd fills "From"/"To" and the file name in the description fields
    const QString label1 = state.value(QStringLiteral("descriptionLabel1")).toString();
    const QString value1 = state.value(QStringLiteral("descriptionValue1")).toString();
    const QString value2 = state.value(QStringLiteral("descriptionValue2")).toString();
    if (t.incoming && t.device.isEmpty()) {
        QString device = label1.isEmpty() ? QString() : value1;
        if (device.isEmpty()) {
            for (const KdcDevice &d : m_kdc->devices()) {
                if (d.paired() && d.reachable)
                    device = d.name;
            }
        }
        if (!device.isEmpty()) {
            t.device = device;
            m_transfers->setDevice(id, device);
            m_transfers->setTitle(id, QFileInfo(QUrl(value2).toLocalFile()).fileName().isEmpty()
                                          ? state.value(QStringLiteral("title")).toString()
                                          : QFileInfo(QUrl(value2).toLocalFile()).fileName());
            m_feedback->start(id, tr("Receiving from %1").arg(device), t.title, QStringLiteral("document-save"), 0);
        }
    }
    const qint64 total = state.value(QStringLiteral("totalBytes")).toLongLong();
    const qint64 done = state.value(QStringLiteral("processedBytes")).toLongLong();
    if (total > 0) {
        m_transfers->setProgress(id, done, total);
        m_feedback->update(id, t.title, qreal(done) / total);
    }
    const QString dest = state.value(QStringLiteral("destUrl")).toString();
    if (!dest.isEmpty())
        m_transfers->setPaths(id, {QUrl(dest).toLocalFile()});
}

void Daemon::onJobFinished(int view, uint errorCode, const QString &errorMessage)
{
    const QString id = m_jobTransfer.take(view);
    if (id.isEmpty())
        return;
    Transfers::Transfer t;
    m_transfers->get(id, &t);
    m_transfers->setState(id, errorCode == 0 ? Transfers::Done : Transfers::Failed, errorMessage);
    m_feedback->finish(id, errorCode == 0 ? (t.incoming ? tr("Received from %1").arg(t.device) : tr("Sent to %1").arg(t.device))
                                          : QString());
}

// ------------------------------------------------------------------ drop bubbles

void Daemon::onDragStarted()
{
    emit draggingChanged();
    if (!m_config->dropEdgeEnabled() && m_config->edgeSide() == QLatin1String("none"))
        return;
    if (!m_dropEdge)
        m_dropEdge = createWindow(QStringLiteral("DropEdge.qml"));
    if (m_dropEdge)
        QMetaObject::invokeMethod(m_dropEdge, "dragStarted");
}

void Daemon::onDragEnded()
{
    emit draggingChanged();
    if (m_dropEdge)
        QMetaObject::invokeMethod(m_dropEdge, "dragEnded");
}

void Daemon::dropFinished()
{
    // Nothing kept around between drags but the hidden window
}

// ------------------------------------------------------------------ Android extras

QVariantList Daemon::phoneNotifications(const QString &id) { return isAndroid(id) ? m_kdc->notifications(plainId(id)) : QVariantList(); }
void Daemon::replyNotification(const QString &id, const QString &publicId, const QString &text) { m_kdc->replyNotification(plainId(id), publicId, text); }
void Daemon::dismissNotification(const QString &id, const QString &publicId) { m_kdc->dismissNotification(plainId(id), publicId); }
QVariantMap Daemon::media(const QString &id) { return isAndroid(id) ? m_kdc->media(plainId(id)) : QVariantMap(); }
void Daemon::mediaAction(const QString &id, const QString &action) { m_kdc->mediaAction(plainId(id), action); }
void Daemon::setMediaVolume(const QString &id, int volume) { m_kdc->setMediaVolume(plainId(id), volume); }
