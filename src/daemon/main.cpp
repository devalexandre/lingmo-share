/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "daemon.h"
#include "common/shareconfig.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QTranslator>

#include <csignal>
#include <cstdio>

static volatile std::sig_atomic_t s_interrupted = 0;

static const QString s_service = QStringLiteral("com.lingmo.Share");
static const QString s_path = QStringLiteral("/Share");

// com.lingmo.Share: what the CLI, the file manager and Lingmo Settings call
class ShareBus : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.lingmo.Share")
public:
    explicit ShareBus(Daemon *daemon)
        : QObject(daemon)
        , m_daemon(daemon)
    {
    }

public slots:
    void Show(const QString &page) { m_daemon->showWindow(page); }
    void PickAndSend(const QStringList &paths) { m_daemon->pickAndSend(paths); }
    QString SendFiles(const QStringList &paths, const QString &deviceId) { return m_daemon->send(deviceId, paths); }
    QString SendText(const QString &text, const QString &deviceId) { return m_daemon->sendText(deviceId, text); }
    QString Devices() const { return m_daemon->devicesJson(); }
    QString Transfer(const QString &id) const
    {
        return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_daemon->transfer(id))).toJson(QJsonDocument::Compact));
    }
    void CancelTransfer(const QString &id) { m_daemon->cancelTransfer(id); }
    void Refresh(bool httpScan) { m_daemon->refresh(httpScan); }
    void SetDiscovering(bool on) { m_daemon->setDiscovering(on); }
    void InstallAndroidSupport() { m_daemon->installAndroidSupport(); }
    void Quit() { qApp->quit(); }

private:
    Daemon *m_daemon;
};

static void loadTranslations(QCoreApplication *app)
{
    const QString locale = QLocale::system().name();
    auto *qt = new QTranslator(app);
    if (qt->load(QStringLiteral("qtbase_") + locale, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app->installTranslator(qt);
    auto *own = new QTranslator(app);
    const QString dir = qEnvironmentVariableIsSet("LINGMO_SHARE_TRANSLATIONS")
                            ? qEnvironmentVariable("LINGMO_SHARE_TRANSLATIONS") : QStringLiteral(TRANSLATIONS_DIR);
    if (own->load(QLocale::system(), QStringLiteral("lingmo-share"), QStringLiteral("_"), dir))
        app->installTranslator(own);
}

// The Lingmo platform theme doesn't always hand the icon theme over at startup
static void applyIconTheme()
{
    const QString current = QIcon::themeName();
    if (!current.isEmpty() && current != QLatin1String("hicolor"))
        return;
    QSettings theme(QStringLiteral("lingmoos"), QStringLiteral("theme"));
    const bool dark = theme.value(QStringLiteral("DarkMode"), false).toBool();
    for (const QString &name : {theme.value(dark ? QStringLiteral("DarkIconTheme") : QStringLiteral("IconTheme")).toString(),
                                QStringLiteral("Lingmo"), QStringLiteral("WhiteSur"), QStringLiteral("breeze")}) {
        if (!name.isEmpty() && QDir(QStringLiteral("/usr/share/icons/") + name).exists()) {
            QIcon::setThemeName(name);
            return;
        }
    }
}

static int printErr(const QString &text)
{
    QTextStream(stderr) << text << Qt::endl;
    return 1;
}

static QDBusInterface *bus()
{
    auto *iface = new QDBusInterface(s_service, s_path, s_service, QDBusConnection::sessionBus());
    iface->setTimeout(5000);
    return iface;
}

// CLI: talk to the running daemon (D-Bus activation starts it when needed)
static int runCli(const QStringList &args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("cli", "Send files to phones and nearby devices"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("send | devices"));
    QCommandLineOption to(QStringLiteral("to"), QCoreApplication::translate("cli", "Device id (see 'devices')"), QStringLiteral("id"));
    QCommandLineOption pick(QStringLiteral("pick"), QCoreApplication::translate("cli", "Choose the device in a window"));
    QCommandLineOption text(QStringLiteral("text"), QCoreApplication::translate("cli", "Send a text instead of files"), QStringLiteral("text"));
    QCommandLineOption wait(QStringLiteral("no-wait"), QCoreApplication::translate("cli", "Return right away"));
    parser.addOptions({to, pick, text, wait});
    parser.process(args);
    const QStringList pos = parser.positionalArguments();
    const QString command = pos.value(0);

    std::unique_ptr<QDBusInterface> iface(bus());
    if (command == QLatin1String("devices")) {
        QDBusReply<QString> reply = iface->call(QStringLiteral("Devices"));
        if (!reply.isValid())
            return printErr(reply.error().message());
        const QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
        QTextStream out(stdout);
        for (const QJsonValue &v : doc.array()) {
            const QJsonObject o = v.toObject();
            out << o.value(QStringLiteral("id")).toString() << '\t' << o.value(QStringLiteral("name")).toString() << '\t'
                << o.value(QStringLiteral("kind")).toString()
                << (o.value(QStringLiteral("sendable")).toBool() ? QString() : QStringLiteral("\t(not reachable)")) << Qt::endl;
        }
        return 0;
    }
    if (command != QLatin1String("send"))
        return printErr(QStringLiteral("usage: lingmo-share send [--to ID | --pick] [--text TEXT] FILES...\n       lingmo-share devices"));

    QStringList files;
    for (const QString &f : pos.mid(1)) {
        const QString path = f.startsWith(QLatin1String("file://")) ? QUrl(f).toLocalFile() : f;
        if (!QFileInfo::exists(path))
            return printErr(QStringLiteral("not found: ") + f);
        files << QFileInfo(path).absoluteFilePath();
    }
    if (!parser.isSet(to) || parser.isSet(pick)) {
        if (files.isEmpty())
            return printErr(QStringLiteral("nothing to send"));
        iface->call(QStringLiteral("PickAndSend"), files);
        return 0;
    }
    QDBusReply<QString> reply = parser.isSet(text)
                                    ? iface->call(QStringLiteral("SendText"), parser.value(text), parser.value(to))
                                    : iface->call(QStringLiteral("SendFiles"), files, parser.value(to));
    if (!reply.isValid())
        return printErr(reply.error().message());
    if (reply.value().isEmpty())
        return printErr(QStringLiteral("unknown device or nothing to send: ") + parser.value(to));
    if (parser.isSet(wait))
        return 0;
    // Follow the transfer until it ends
    const QString id = reply.value();
    QTextStream out(stdout);
    int lastPercent = -1;
    // Ctrl+C (or a timeout) cancels the transfer instead of leaving it behind
    std::signal(SIGINT, [](int) { s_interrupted = 1; });
    std::signal(SIGTERM, [](int) { s_interrupted = 1; });
    for (;;) {
        if (s_interrupted) {
            iface->call(QStringLiteral("CancelTransfer"), id);
            return printErr(QStringLiteral("cancelled"));
        }
        QDBusReply<QString> state = iface->call(QStringLiteral("Transfer"), id);
        if (!state.isValid())
            return printErr(state.error().message());
        const QJsonObject o = QJsonDocument::fromJson(state.value().toUtf8()).object();
        const int percent = int(o.value(QStringLiteral("progress")).toDouble() * 100);
        if (percent != lastPercent) {
            out << percent << "%" << Qt::endl;
            lastPercent = percent;
        }
        if (o.value(QStringLiteral("finished")).toBool()) {
            const int st = o.value(QStringLiteral("state")).toInt();
            if (st == 2) {
                out << "done" << Qt::endl;
                return 0;
            }
            return printErr(o.value(QStringLiteral("error")).toString().isEmpty() ? QStringLiteral("failed")
                                                                                  : o.value(QStringLiteral("error")).toString());
        }
        QThread::msleep(300);
    }
}

int main(int argc, char *argv[])
{
    const QStringList raw = [&] {
        QStringList l;
        for (int i = 0; i < argc; ++i)
            l << QString::fromLocal8Bit(argv[i]);
        return l;
    }();
    const QString command = raw.value(1);

    if (command == QLatin1String("send") || command == QLatin1String("devices")) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("lingmo-share"));
        loadTranslations(&app);
        return runCli(app.arguments());
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("lingmoos"));
    QApplication::setApplicationName(QStringLiteral("lingmo-share"));
    QApplication::setApplicationDisplayName(QObject::tr("Phone and devices"));
    QApplication::setDesktopFileName(QStringLiteral("lingmo-share"));
    QApplication::setApplicationVersion(QStringLiteral(LINGMO_SHARE_VERSION));
    // The daemon lives on when its windows close
    QApplication::setQuitOnLastWindowClosed(false);
    loadTranslations(&app);
    applyIconTheme();
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("lingmo-share"), QIcon::fromTheme(QStringLiteral("smartphone"))));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption daemonOpt(QStringLiteral("daemon"), QStringLiteral("Run in the background"));
    QCommandLineOption pageOpt(QStringLiteral("page"), QStringLiteral("Open a page: devices, transfers, phone, settings"), QStringLiteral("page"));
    parser.addOptions({daemonOpt, pageOpt});
    parser.process(app);

    QDBusConnection session = QDBusConnection::sessionBus();
    // Already running: hand over and leave
    if (session.interface()->isServiceRegistered(s_service)) {
        if (parser.isSet(daemonOpt))
            return 0;
        std::unique_ptr<QDBusInterface> iface(bus());
        iface->call(QStringLiteral("Show"), parser.value(pageOpt));
        return 0;
    }

    Daemon daemon;
    auto *adaptor = new ShareBus(&daemon);
    if (!session.registerObject(s_path, adaptor, QDBusConnection::ExportAllSlots) || !session.registerService(s_service)) {
        // Lost a race with another instance
        if (!parser.isSet(daemonOpt)) {
            std::unique_ptr<QDBusInterface> iface(bus());
            iface->call(QStringLiteral("Show"), parser.value(pageOpt));
        }
        return 0;
    }
    daemon.start();
    if (!parser.isSet(daemonOpt))
        QTimer::singleShot(0, &daemon, [&daemon, &parser, &pageOpt] { daemon.showWindow(parser.value(pageOpt)); });
    return app.exec();
}

#include "main.moc"
