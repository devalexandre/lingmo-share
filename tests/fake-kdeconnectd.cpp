/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// A stand-in for kdeconnectd: the same D-Bus names, paths, interfaces and
// signatures (taken from kdeconnect-kde 26.08), with two phones: "Galaxy S23"
// (paired, 82 % battery, 3/4 signal, music playing, two notifications and
// SMS) and "Pixel 8" (not paired). Every call is printed on stdout.
//
//   fake-kdeconnectd [--request-pair SECONDS] [--incoming-file SECONDS]

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QFileInfo>

static void say(const QString &what)
{
    QTextStream(stdout) << "fake-kdeconnectd: " << what << Qt::endl;
}

static const QString s_base = QStringLiteral("/modules/kdeconnect");

struct Address { QString address; };
struct Attachment { qint64 part = 0; QString mime, data, id; };
struct Message {
    qint32 event = 1;
    QString body;
    QList<Address> addresses;
    qint64 date = 0;
    qint32 type = 1, read = 1;
    qint64 thread = 0;
    qint32 uid = 0;
    qint64 sub = -1;
    QList<Attachment> attachments;
};
Q_DECLARE_METATYPE(Address)
Q_DECLARE_METATYPE(Attachment)
Q_DECLARE_METATYPE(Message)

QDBusArgument &operator<<(QDBusArgument &a, const Address &v) { a.beginStructure(); a << v.address; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, Address &v) { a.beginStructure(); a >> v.address; a.endStructure(); return a; }
QDBusArgument &operator<<(QDBusArgument &a, const Attachment &v) { a.beginStructure(); a << v.part << v.mime << v.data << v.id; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, Attachment &v) { a.beginStructure(); a >> v.part >> v.mime >> v.data >> v.id; a.endStructure(); return a; }
QDBusArgument &operator<<(QDBusArgument &a, const Message &m)
{
    a.beginStructure();
    a << m.event << m.body << m.addresses << m.date << m.type << m.read << m.thread << m.uid << m.sub << m.attachments;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, Message &m)
{
    a.beginStructure();
    a >> m.event >> m.body >> m.addresses >> m.date >> m.type >> m.read >> m.thread >> m.uid >> m.sub >> m.attachments;
    a.endStructure();
    return a;
}

static Message msg(qint64 thread, qint32 uid, const QString &who, const QString &body, bool sent, int minutesAgo)
{
    Message m;
    m.body = body;
    m.addresses = {Address{who}};
    m.date = QDateTime::currentMSecsSinceEpoch() - qint64(minutesAgo) * 60000;
    m.type = sent ? 2 : 1;
    m.thread = thread;
    m.uid = uid;
    return m;
}

class Device;

class Daemon : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.daemon")
public:
    QList<Device *> deviceList;
    QString name = QStringLiteral("fake");
    QStringList disabled;
public slots:
    QStringList devices(bool onlyReachable, bool onlyPaired);
    QString announcedName() { return name; }
    void setAnnouncedName(const QString &n) { name = n; say("setAnnouncedName " + n); emit announcedNameChanged(n); }
    QString selfId() { return QStringLiteral("fakeself"); }
    QStringList linkProviders()
    {
        return {QStringLiteral("LanLinkProvider|lan|") + (disabled.contains("lan") ? "disabled" : "enabled"),
                QStringLiteral("BluetoothLinkProvider|bluetooth|") + (disabled.contains("bluetooth") ? "disabled" : "enabled")};
    }
    void setLinkProviderState(const QString &p, bool on)
    {
        say(QStringLiteral("setLinkProviderState %1 %2").arg(p).arg(on));
        if (on) disabled.removeAll(p); else if (!disabled.contains(p)) disabled << p;
    }
signals:
    void deviceAdded(const QString &id);
    void deviceRemoved(const QString &id);
    void deviceVisibilityChanged(const QString &id, bool visible);
    void deviceListChanged();
    void announcedNameChanged(const QString &name);
};

class Device : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device")
    Q_PROPERTY(QString type MEMBER m_type)
    Q_PROPERTY(QString name MEMBER m_name)
    Q_PROPERTY(QString iconName READ iconName)
    Q_PROPERTY(QString verificationKey MEMBER m_key)
    Q_PROPERTY(QString statusIconName READ iconName)
    Q_PROPERTY(bool isReachable MEMBER m_reachable)
    Q_PROPERTY(bool isPaired READ isPaired)
    Q_PROPERTY(bool isPairRequested READ isPairRequested)
    Q_PROPERTY(bool isPairRequestedByPeer READ isPairRequestedByPeer)
    Q_PROPERTY(int pairState MEMBER m_pairState)
    Q_PROPERTY(QStringList supportedPlugins READ loadedPlugins)
public:
    Device(Daemon *d, const QString &id, const QString &name, int pairState, const QString &key)
        : m_daemon(d), m_id(id), m_name(name), m_key(key), m_pairState(pairState) {}
    QString m_id, m_type = QStringLiteral("smartphone"), m_name, m_key;
    bool m_reachable = true;
    int m_pairState = 0;
    QStringList m_enabled = {"kdeconnect_battery", "kdeconnect_connectivity_report", "kdeconnect_share",
                             "kdeconnect_findmyphone", "kdeconnect_clipboard", "kdeconnect_notifications",
                             "kdeconnect_sms", "kdeconnect_mprisremote"};
    QString iconName() const { return QStringLiteral("smartphone"); }
    bool isPaired() const { return m_pairState == 3; }
    bool isPairRequested() const { return m_pairState == 1; }
    bool isPairRequestedByPeer() const { return m_pairState == 2; }
    void setPair(int s) { m_pairState = s; emit pairStateChanged(s); }
public slots:
    QStringList loadedPlugins() const { return isPaired() ? m_enabled : QStringList(); }
    bool hasPlugin(const QString &p) const { return loadedPlugins().contains(p); }
    bool isPluginEnabled(const QString &p) const { return m_enabled.contains(p); }
    void setPluginEnabled(const QString &p, bool on)
    {
        say(QStringLiteral("%1 setPluginEnabled %2 %3").arg(m_name, p).arg(on));
        if (on && !m_enabled.contains(p)) m_enabled << p;
        if (!on) m_enabled.removeAll(p);
        emit pluginsChanged();
    }
    void requestPairing()
    {
        say(m_name + " requestPairing");
        setPair(1);
        // The phone accepts after a moment
        QTimer::singleShot(2500, this, [this] { if (m_pairState == 1) { setPair(3); say(m_name + " paired"); } });
    }
    void acceptPairing() { say(m_name + " acceptPairing"); setPair(3); }
    void cancelPairing() { say(m_name + " cancelPairing"); setPair(0); }
    void unpair() { say(m_name + " unpair"); setPair(0); }
    QString encryptionInfo() { return QStringLiteral("fake"); }
signals:
    void pluginsChanged();
    void reachableChanged(bool);
    void pairStateChanged(int);
    void pairingFailed(const QString &);
    void nameChanged(const QString &);
    void typeChanged(const QString &);
    void statusIconNameChanged();
private:
    Daemon *m_daemon;
};

QStringList Daemon::devices(bool onlyReachable, bool onlyPaired)
{
    QStringList out;
    for (Device *d : std::as_const(deviceList)) {
        if ((onlyReachable && !d->m_reachable) || (onlyPaired && !d->isPaired()))
            continue;
        out << d->m_id;
    }
    return out;
}

class Battery : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.battery")
    Q_PROPERTY(int charge MEMBER charge)
    Q_PROPERTY(bool isCharging MEMBER charging)
    Q_PROPERTY(bool hasBattery READ hasBattery)
    Q_PROPERTY(QString iconName READ iconName)
public:
    int charge = 82;
    bool charging = false;
    bool hasBattery() const { return true; }
    QString iconName() const { return QStringLiteral("battery-080"); }
signals:
    void refreshed(bool isCharging, int charge);
};

class Connectivity : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.connectivity_report")
    Q_PROPERTY(QString cellularNetworkType MEMBER type)
    Q_PROPERTY(int cellularNetworkStrength MEMBER strength)
public:
    QString type = QStringLiteral("5G");
    int strength = 3;
signals:
    void refreshed(QString cellularNetworkType, int cellularNetworkStrength);
};

class FindMyPhone : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.findmyphone")
public slots:
    void ring() { say("ring"); }
};

class Clipboard : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.clipboard")
public slots:
    void sendClipboard() { say("sendClipboard"); }
};

// Reports a KIO-like job to org.kde.JobViewServer, as kdeconnectd does
static void fakeJob(QObject *ctx, QVariantMap start, qint64 total, std::function<void()> done)
{
    QDBusInterface server(QStringLiteral("org.kde.JobViewServer"), QStringLiteral("/JobViewServer"),
                          QStringLiteral("org.kde.JobViewServerV2"));
    QDBusMessage reply = server.call(QStringLiteral("requestView"), QStringLiteral("org.kde.kdeconnect.daemon"), 0, QVariantMap());
    if (reply.type() != QDBusMessage::ReplyMessage) {
        say("no job view server: " + reply.errorMessage());
        if (done) done();
        return;
    }
    const QString path = reply.arguments().first().value<QDBusObjectPath>().path();
    say("job view " + path);
    auto *timer = new QTimer(ctx);
    auto step = std::make_shared<int>(0);
    QDBusMessage first = QDBusMessage::createMethodCall(QStringLiteral("org.kde.JobViewServer"), path,
                                                       QStringLiteral("org.kde.JobViewV3"), QStringLiteral("update"));
    first << start;
    QDBusConnection::sessionBus().call(first, QDBus::NoBlock);
    QObject::connect(timer, &QTimer::timeout, ctx, [timer, step, path, total, done] {
        ++*step;
        const qint64 processed = qMin<qint64>(total, total * *step / 10);
        QDBusMessage up = QDBusMessage::createMethodCall(QStringLiteral("org.kde.JobViewServer"), path,
                                                        QStringLiteral("org.kde.JobViewV3"), QStringLiteral("update"));
        up << QVariantMap{{QStringLiteral("processedBytes"), processed}, {QStringLiteral("totalBytes"), total},
                          {QStringLiteral("percent"), uint(processed * 100 / total)}};
        QDBusConnection::sessionBus().call(up, QDBus::NoBlock);
        if (processed >= total) {
            timer->deleteLater();
            QDBusMessage end = QDBusMessage::createMethodCall(QStringLiteral("org.kde.JobViewServer"), path,
                                                             QStringLiteral("org.kde.JobViewV3"), QStringLiteral("terminate"));
            end << uint(0) << QString() << QVariantMap();
            QDBusConnection::sessionBus().call(end, QDBus::NoBlock);
            if (done) done();
        }
    });
    timer->start(400);
}

class Share : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.share")
public:
    QString deviceId, deviceName;
public slots:
    void shareUrl(const QString &url) { shareUrls({url}); }
    void shareUrls(const QStringList &urls)
    {
        say("shareUrls " + urls.join(QLatin1Char(' ')));
        qint64 total = 0;
        for (const QString &u : urls)
            total += QFileInfo(QUrl(u).toLocalFile()).size();
        fakeJob(this, {{QStringLiteral("title"), QStringLiteral("Sending to %1").arg(deviceName)},
                       {QStringLiteral("descriptionLabel1"), QStringLiteral("To")},
                       {QStringLiteral("descriptionValue1"), deviceName}}, qMax<qint64>(total, 1), {});
    }
    void shareText(const QString &text) { say("shareText " + text); }
    void openFile(const QString &file) { say("openFile " + file); }
public:
    // The phone sends a photo: written where Lingmo told kdeconnectd to put files
    void receive()
    {
        QSettings cfg(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                          + QStringLiteral("/kdeconnect/%1/kdeconnect_share/config").arg(deviceId), QSettings::IniFormat);
        const QString dir = cfg.value(QStringLiteral("incoming_path"), QDir::homePath() + "/Downloads").toString();
        const QString path = dir + QStringLiteral("/IMG_20261024_0915.jpg");
        say("incoming file to " + path);
        fakeJob(this, {{QStringLiteral("title"), QStringLiteral("Receiving file")},
                       {QStringLiteral("descriptionLabel1"), QStringLiteral("From")},
                       {QStringLiteral("descriptionValue1"), deviceName},
                       {QStringLiteral("descriptionLabel2"), QStringLiteral("To")},
                       {QStringLiteral("descriptionValue2"), QUrl::fromLocalFile(path).toString()},
                       {QStringLiteral("destUrl"), QUrl::fromLocalFile(path).toString()}},
                2400000, [this, path] {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write(QByteArray(2400000, 'x'));
            emit shareReceived(QUrl::fromLocalFile(path).toString());
        });
    }
signals:
    void shareReceived(const QString &url);
};

class Notification : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.notifications.notification")
    Q_PROPERTY(QString internalId MEMBER id)
    Q_PROPERTY(QString appName MEMBER app)
    Q_PROPERTY(QString ticker READ ticker)
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(QString text MEMBER text)
    Q_PROPERTY(QString iconPath MEMBER icon)
    Q_PROPERTY(bool dismissable MEMBER dismissable)
    Q_PROPERTY(bool hasIcon READ hasIcon)
    Q_PROPERTY(bool silent MEMBER silent)
    Q_PROPERTY(QString replyId MEMBER replyId)
public:
    QString id, app, title, text, icon, replyId;
    bool dismissable = true, silent = false;
    QString ticker() const { return title + ": " + text; }
    bool hasIcon() const { return false; }
public slots:
    void dismiss() { say("dismiss " + id); }
    void reply() { say("reply dialog " + id); }
    void sendReply(const QString &m) { say("sendReply " + id + " " + m); }
};

class Notifications : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.notifications")
public:
    QStringList ids;
public slots:
    QStringList activeNotifications() { return ids; }
    void sendReply(const QString &replyId, const QString &message) { say("notifications sendReply " + replyId + " " + message); }
    void sendAction(const QString &key, const QString &action) { say("sendAction " + key + " " + action); }
signals:
    void notificationPosted(const QString &publicId);
    void notificationRemoved(const QString &publicId);
    void notificationUpdated(const QString &publicId);
    void allNotificationsRemoved();
};

class Media : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.mprisremote")
    Q_PROPERTY(int volume MEMBER volume)
    Q_PROPERTY(int length MEMBER length)
    Q_PROPERTY(bool isPlaying MEMBER playing)
    Q_PROPERTY(int position MEMBER position)
    Q_PROPERTY(QStringList playerList MEMBER players)
    Q_PROPERTY(QString player MEMBER player)
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(QString artist MEMBER artist)
    Q_PROPERTY(QString album MEMBER album)
    Q_PROPERTY(bool canSeek MEMBER canSeek)
public:
    int volume = 60, length = 215000, position = 42000;
    bool playing = true, canSeek = true;
    QStringList players = {QStringLiteral("Spotify")};
    QString player = QStringLiteral("Spotify"), title = QStringLiteral("Águas de Março"),
            artist = QStringLiteral("Elis Regina & Tom Jobim"), album = QStringLiteral("Elis & Tom");
public slots:
    void sendAction(const QString &a)
    {
        say("media " + a);
        if (a == QLatin1String("PlayPause")) playing = !playing;
        emit propertiesChanged();
    }
    void seek(int offset) { say(QStringLiteral("seek %1").arg(offset)); }
    void requestPlayerList() {}
signals:
    void propertiesChanged();
};

// An adaptor on the device object, like kdeconnectd's ConversationsDbusInterface
class Conversations : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.conversations")
public:
    explicit Conversations(QObject *parent) : QDBusAbstractAdaptor(parent) { setAutoRelaySignals(false); }
    QList<Message> all;
public slots:
    QVariantList activeConversations()
    {
        QHash<qint64, Message> latest;
        for (const Message &m : std::as_const(all))
            if (!latest.contains(m.thread) || latest[m.thread].date < m.date)
                latest[m.thread] = m;
        QVariantList out;
        for (const Message &m : std::as_const(latest))
            out << QVariant::fromValue(m); // av of (isa(s)xiixixa(xsss)), as kdeconnectd
        return out;
    }
    void requestConversation(qint64 id, int start, int end)
    {
        say(QStringLiteral("requestConversation %1 %2 %3").arg(id).arg(start).arg(end));
        QTimer::singleShot(200, this, [this, id] {
            quint64 n = 0;
            for (const Message &m : std::as_const(all))
                if (m.thread == id) { emit conversationUpdated(QDBusVariant(QVariant::fromValue(m))); ++n; }
            emit conversationLoaded(id, n);
        });
    }
    void replyToConversation(qint64 id, const QString &text, const QVariantList &)
    {
        say(QStringLiteral("replyToConversation %1 %2").arg(id).arg(text));
        Message m = msg(id, 900 + all.size(), all.isEmpty() ? QString() : all.first().addresses.first().address, text, true, 0);
        for (const Message &o : std::as_const(all)) if (o.thread == id) m.addresses = o.addresses;
        all << m;
        emit conversationUpdated(QDBusVariant(QVariant::fromValue(m)));
    }
    void sendWithoutConversation(const QDBusArgument &addresses, const QString &text, const QVariantList &)
    {
        QStringList to;
        addresses.beginArray();
        while (!addresses.atEnd()) {
            QDBusVariant v;
            addresses >> v;
            Address a = qdbus_cast<Address>(v.variant());
            to << a.address;
        }
        addresses.endArray();
        say("sendWithoutConversation " + to.join(",") + " " + text);
    }
    void requestAllConversationThreads() { say("requestAllConversationThreads"); }
    void requestAttachmentFile(qint64, const QString &) {}
signals:
    void conversationCreated(const QDBusVariant &msg);
    void conversationRemoved(qint64 conversationID);
    void conversationUpdated(const QDBusVariant &msg);
    void conversationLoaded(qint64 conversationID, quint64 messageCount);
};

// Test hooks: dbus-send --session --dest=org.kde.kdeconnect /test org.lingmo.FakeKdeConnect.<method>
class Control : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.lingmo.FakeKdeConnect")
public:
    Device *pixel = nullptr;
    Share *share = nullptr;
public slots:
    void requestPair() { say("Pixel 8 asks to pair"); pixel->setPair(2); }
    void incomingFile() { share->receive(); }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<Address>();
    qDBusRegisterMetaType<QList<Address>>();
    qDBusRegisterMetaType<Attachment>();
    qDBusRegisterMetaType<QList<Attachment>>();
    qDBusRegisterMetaType<Message>();

    QDBusConnection bus = QDBusConnection::sessionBus();
    const auto opts = QDBusConnection::ExportAllContents;
    auto *daemon = new Daemon;
    bus.registerObject(s_base, daemon, opts);

    auto add = [&](const QString &id, const QString &name, int pair, const QString &key) {
        auto *d = new Device(daemon, id, name, pair, key);
        daemon->deviceList << d;
        const QString path = s_base + "/devices/" + id;
        auto *conv = new Conversations(d);
        bus.registerObject(path, d, opts | QDBusConnection::ExportAdaptors);
        auto *bat = new Battery;
        bat->setParent(d);
        bus.registerObject(path + "/battery", bat, opts);
        auto *con = new Connectivity;
        con->setParent(d);
        bus.registerObject(path + "/connectivity_report", con, opts);
        bus.registerObject(path + "/findmyphone", new FindMyPhone, opts);
        bus.registerObject(path + "/clipboard", new Clipboard, opts);
        auto *share = new Share;
        share->deviceId = id;
        share->deviceName = name;
        bus.registerObject(path + "/share", share, opts);
        auto *notes = new Notifications;
        bus.registerObject(path + "/notifications", notes, opts);
        bus.registerObject(path + "/mprisremote", new Media, opts);
        return std::make_tuple(d, conv, notes, share, bat);
    };

    auto [galaxy, conv, notes, share, battery] = add(QStringLiteral("a1b2c3d4_galaxy"), QStringLiteral("Galaxy S23"), 3, QStringLiteral("8F3A 2C91"));
    auto [pixel, conv2, notes2, share2, battery2] = add(QStringLiteral("e5f6a7b8_pixel"), QStringLiteral("Pixel 8"), 0, QStringLiteral("47D1 B03E"));
    Q_UNUSED(conv2); Q_UNUSED(notes2); Q_UNUSED(share2); Q_UNUSED(battery2);

    conv->all = {msg(11, 1, QStringLiteral("+55 11 98888-1234"), QStringLiteral("Chego em 10 minutos"), false, 30),
                 msg(11, 2, QStringLiteral("+55 11 98888-1234"), QStringLiteral("Beleza, te espero na portaria"), true, 28),
                 msg(11, 3, QStringLiteral("+55 11 98888-1234"), QStringLiteral("Cheguei!"), false, 18),
                 msg(12, 4, QStringLiteral("Banco"), QStringLiteral("Seu código de verificação é 482913"), false, 120),
                 msg(13, 5, QStringLiteral("+55 21 97777-0000"), QStringLiteral("Amanhã tem churrasco?"), false, 600)};

    auto note = [&](const QString &id, const QString &app, const QString &title, const QString &text, bool reply) {
        auto *n = new Notification;
        n->id = id; n->app = app; n->title = title; n->text = text;
        n->replyId = reply ? QStringLiteral("reply-") + id : QString();
        bus.registerObject(s_base + "/devices/a1b2c3d4_galaxy/notifications/" + id, n, opts);
        notes->ids << id;
    };
    note(QStringLiteral("n1"), QStringLiteral("WhatsApp"), QStringLiteral("Mãe"), QStringLiteral("Vem jantar hoje?"), true);
    note(QStringLiteral("n2"), QStringLiteral("Agenda"), QStringLiteral("Reunião às 15h"), QStringLiteral("Sala 3 · em 20 minutos"), false);

    auto *control = new Control;
    control->pixel = pixel;
    control->share = share;
    bus.registerObject(QStringLiteral("/test"), control, QDBusConnection::ExportAllSlots);

    if (!bus.registerService(QStringLiteral("org.kde.kdeconnect"))) {
        say("org.kde.kdeconnect is taken");
        return 1;
    }
    say("ready");

    const QStringList args = app.arguments();
    const int pairAt = args.indexOf(QStringLiteral("--request-pair"));
    if (pairAt > 0) {
        Device *p = pixel;
        QTimer::singleShot(int(args.value(pairAt + 1).toDouble() * 1000), p, [p] {
            say("Pixel 8 asks to pair");
            p->setPair(2);
        });
    }
    const int fileAt = args.indexOf(QStringLiteral("--incoming-file"));
    if (fileAt > 0) {
        Share *s = share;
        QTimer::singleShot(int(args.value(fileAt + 1).toDouble() * 1000), s, [s] { s->receive(); });
    }
    // The battery drains a little, as phones do
    Battery *b = battery;
    auto *drain = new QTimer(&app);
    QObject::connect(drain, &QTimer::timeout, b, [b] { b->charge = qMax(5, b->charge - 1); emit b->refreshed(b->charging, b->charge); });
    drain->start(60000);
    return app.exec();
}

#include "fake-kdeconnectd.moc"
