/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Two LocalSend peers in one process, on 127.0.0.1 with their own ports,
// certificates and settings: everything goes over real HTTPS sockets.

#include "common/shareconfig.h"
#include "localsend/localsend.h"
#include "localsend/sender.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

struct Peer {
    QTemporaryDir home;
    ShareConfig *config = nullptr;
    LocalSend *ls = nullptr;
    QString receive;

    void init(int port, const QString &name)
    {
        config = new ShareConfig(home.path() + "/share.conf", port);
        config->setDeviceName(name);
        receive = home.path() + "/Downloads";
        QDir().mkpath(receive);
        config->setReceiveFolder(receive);
        ls = new LocalSend(config);
        QVERIFY(ls->init(home.path() + "/data"));
        ls->apply();
        QCOMPARE(int(ls->mode()), int(LocalSend::Mode::Full));
    }
    ~Peer()
    {
        delete ls;
        delete config;
    }
    LsDevice asTarget() const
    {
        LsDevice d;
        d.fingerprint = ls->identity().fingerprint();
        d.alias = config->deviceName();
        d.ip = QHostAddress::LocalHost;
        d.port = quint16(config->port());
        d.protocol = QStringLiteral("https");
        return d;
    }
};

static QString makeFile(const QString &path, qint64 size)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    QByteArray chunk(64 * 1024, Qt::Uninitialized);
    qint64 left = size;
    while (left > 0) {
        for (int i = 0; i < chunk.size(); i += 4)
            *reinterpret_cast<quint32 *>(chunk.data() + i) = QRandomGenerator::global()->generate();
        const qint64 n = qMin<qint64>(left, chunk.size());
        f.write(chunk.constData(), n);
        left -= n;
    }
    f.close();
    return path;
}

static QByteArray sha(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return h.result();
}

static bool waitState(LocalSendSender *s, QList<LocalSendSender::State> states, int ms = 20000)
{
    return QTest::qWaitFor([&] { return states.contains(s->state()); }, ms);
}

class TestLocalSend : public QObject
{
    Q_OBJECT

    Peer *a = nullptr; // receiver
    Peer *b = nullptr; // sender
    QTemporaryDir src;

    LocalSendSender *sender(const LsDevice &target)
    {
        return new LocalSendSender(target, b->ls->identity(), b->ls->info(false), this);
    }

private slots:
    void initTestCase()
    {
        a = new Peer;
        b = new Peer;
        a->init(53411, QStringLiteral("Receiver PC"));
        b->init(53412, QStringLiteral("Sender PC"));
        QVERIFY(a->ls->identity().fingerprint() != b->ls->identity().fingerprint());
        QCOMPARE(a->ls->identity().fingerprint().size(), 64);
    }

    void cleanupTestCase()
    {
        delete a;
        delete b;
    }

    void identityIsStable()
    {
        Identity again;
        QVERIFY(again.load(a->home.path() + "/data"));
        QCOMPARE(again.fingerprint(), a->ls->identity().fingerprint());
        QCOMPARE(again.certificate().subjectInfo(QSslCertificate::CommonName).value(0), QStringLiteral("LocalSend User"));
        QFileInfo key(a->home.path() + "/data/localsend-key.pem");
        QCOMPARE(key.permissions() & (QFile::ReadGroup | QFile::ReadOther), QFileDevice::Permissions());
    }

    void sendAndAccept()
    {
        const QString big = makeFile(src.path() + "/big.bin", 3 * 1024 * 1024 + 17);
        const QString nested = makeFile(src.path() + "/Album/photo.jpg", 4096);
        QSignalSpy request(a->ls, &LocalSend::incomingRequest);
        QSignalSpy finished(a->ls, &LocalSend::incomingFinished);
        connect(a->ls, &LocalSend::incomingRequest, this, [this](const QString &id) {
            const QVariantMap info = a->ls->incomingInfo(id);
            QCOMPARE(info.value("name").toString(), QStringLiteral("Sender PC"));
            // The sender proved its identity with its client certificate (mTLS)
            QVERIFY(info.value("verified").toBool());
            QCOMPARE(info.value("fingerprint").toString(), b->ls->identity().fingerprint());
            a->ls->accept(id, false);
        }, Qt::SingleShotConnection);

        LocalSendSender *s = sender(a->asTarget());
        s->sendFiles(LocalSendSender::collect({big, src.path() + "/Album"}));
        QVERIFY(waitState(s, {LocalSendSender::Done, LocalSendSender::Failed, LocalSendSender::Declined}));
        QCOMPARE(s->state(), LocalSendSender::Done);
        QCOMPARE(request.count(), 1);
        QVERIFY(QTest::qWaitFor([&] { return finished.count() == 1; }, 5000));
        QVERIFY(finished.first().at(2).toBool());
        QCOMPARE(sha(a->receive + "/big.bin"), sha(big));
        QCOMPARE(sha(a->receive + "/Album/photo.jpg"), sha(nested));
        // No half files left behind
        QCOMPARE(QDir(a->receive).entryList(QDir::Hidden | QDir::Files).filter("lingmo-share-part").size(), 0);
    }

    void neverOverwrites()
    {
        const QString file = makeFile(src.path() + "/big.bin", 100);
        connect(a->ls, &LocalSend::incomingRequest, this, [this](const QString &id) { a->ls->accept(id, false); },
                Qt::SingleShotConnection);
        LocalSendSender *s = sender(a->asTarget());
        s->sendFiles(LocalSendSender::collect({file}));
        QVERIFY(waitState(s, {LocalSendSender::Done, LocalSendSender::Failed}));
        QCOMPARE(s->state(), LocalSendSender::Done);
        QCOMPARE(QFileInfo(a->receive + "/big.bin").size(), 3 * 1024 * 1024 + 17);
        QCOMPARE(QFileInfo(a->receive + "/big (2).bin").size(), 100);
    }

    void decline()
    {
        const QString file = makeFile(src.path() + "/no.txt", 10);
        connect(a->ls, &LocalSend::incomingRequest, this, [this](const QString &id) { a->ls->decline(id); },
                Qt::SingleShotConnection);
        LocalSendSender *s = sender(a->asTarget());
        s->sendFiles(LocalSendSender::collect({file}));
        QVERIFY(waitState(s, {LocalSendSender::Declined, LocalSendSender::Failed, LocalSendSender::Done}));
        QCOMPARE(s->state(), LocalSendSender::Declined);
        QVERIFY(!QFile::exists(a->receive + "/no.txt"));
    }

    void pathTraversal()
    {
        const QString file = makeFile(src.path() + "/evil.txt", 32);
        connect(a->ls, &LocalSend::incomingRequest, this, [this](const QString &id) { a->ls->accept(id, false); },
                Qt::SingleShotConnection);
        LocalSendSender *s = sender(a->asTarget());
        OutgoingFile f{file, QStringLiteral("../../../.evil-outside.txt"), 32, QStringLiteral("text/plain")};
        s->sendFiles({f});
        QVERIFY(waitState(s, {LocalSendSender::Done, LocalSendSender::Failed}));
        QCOMPARE(s->state(), LocalSendSender::Done);
        QVERIFY(QFile::exists(a->receive + "/evil-outside.txt"));
        QVERIFY(!QFile::exists(a->home.path() + "/.evil-outside.txt"));
        QVERIFY(!QFile::exists(QDir::cleanPath(a->receive + "/../../.evil-outside.txt")));
    }

    void pin()
    {
        a->config->setPin(QStringLiteral("4321"));
        a->config->setPinEnabled(true);
        const QString file = makeFile(src.path() + "/pin.txt", 10);
        connect(a->ls, &LocalSend::incomingRequest, this, [this](const QString &id) { a->ls->accept(id, false); },
                Qt::SingleShotConnection);
        LocalSendSender *s = sender(a->asTarget());
        s->sendFiles(LocalSendSender::collect({file}));
        QVERIFY(waitState(s, {LocalSendSender::NeedPin, LocalSendSender::Failed}));
        QCOMPARE(s->state(), LocalSendSender::NeedPin);
        s->setPin(QStringLiteral("0000"));
        QVERIFY(QTest::qWaitFor([&] { return s->errorString().contains("Wrong"); }, 5000));
        s->setPin(QStringLiteral("4321"));
        QVERIFY(waitState(s, {LocalSendSender::Done, LocalSendSender::Failed}));
        QCOMPARE(s->state(), LocalSendSender::Done);
        a->config->setPinEnabled(false);
    }

    void trustedSkipsQuestion()
    {
        a->config->trust(b->ls->identity().fingerprint(), QStringLiteral("Sender PC"));
        QSignalSpy request(a->ls, &LocalSend::incomingRequest);
        const QString file = makeFile(src.path() + "/trusted.txt", 1000);
        LocalSendSender *s = sender(a->asTarget());
        s->sendFiles(LocalSendSender::collect({file}));
        QVERIFY(waitState(s, {LocalSendSender::Done, LocalSendSender::Failed}));
        QCOMPARE(s->state(), LocalSendSender::Done);
        QCOMPARE(request.count(), 0);
        a->config->untrust(b->ls->identity().fingerprint());
    }

    void trustedOnlyRejectsOthers()
    {
        a->config->setVisibility(ShareConfig::TrustedOnly);
        a->ls->apply();
        QSignalSpy request(a->ls, &LocalSend::incomingRequest);
        const QString file = makeFile(src.path() + "/x.txt", 10);
        LocalSendSender *s = sender(a->asTarget());
        s->sendFiles(LocalSendSender::collect({file}));
        QVERIFY(waitState(s, {LocalSendSender::Declined, LocalSendSender::Failed, LocalSendSender::Done}));
        QCOMPARE(s->state(), LocalSendSender::Declined);
        QCOMPARE(request.count(), 0);
        a->config->setVisibility(ShareConfig::Everyone);
        a->ls->apply();
    }

    void offExposesNothing()
    {
        a->config->setVisibility(ShareConfig::Off);
        a->ls->apply();
        QCOMPARE(int(a->ls->mode()), int(LocalSend::Mode::Stopped));
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, quint16(a->config->port()));
        QVERIFY(!probe.waitForConnected(1000));
        a->config->setVisibility(ShareConfig::Everyone);
        a->ls->apply();
        QCOMPARE(int(a->ls->mode()), int(LocalSend::Mode::Full));
    }

    void wrongFingerprintIsRefused()
    {
        LsDevice impostor = a->asTarget();
        impostor.fingerprint = QString(64, QLatin1Char('A'));
        const QString file = makeFile(src.path() + "/secret.txt", 10);
        QSignalSpy request(a->ls, &LocalSend::incomingRequest);
        LocalSendSender *s = sender(impostor);
        s->sendFiles(LocalSendSender::collect({file}));
        QVERIFY(waitState(s, {LocalSendSender::Failed, LocalSendSender::Done, LocalSendSender::Declined}));
        QCOMPARE(s->state(), LocalSendSender::Failed);
        QTest::qWait(300);
        QCOMPARE(request.count(), 0);
    }

    void textMessage()
    {
        QSignalSpy message(a->ls, &LocalSend::messageReceived);
        connect(a->ls, &LocalSend::incomingRequest, this, [this](const QString &id) {
            QCOMPARE(a->ls->incomingInfo(id).value("message").toString(), QStringLiteral("olá do celular"));
            a->ls->accept(id, false);
        }, Qt::SingleShotConnection);
        LocalSendSender *s = sender(a->asTarget());
        s->sendText(QStringLiteral("olá do celular"));
        QVERIFY(waitState(s, {LocalSendSender::Done, LocalSendSender::Failed}));
        QCOMPARE(s->state(), LocalSendSender::Done);
        QCOMPARE(message.count(), 1);
        QCOMPARE(message.first().at(2).toString(), QStringLiteral("olá do celular"));
    }

    void senderCancels()
    {
        const QString file = makeFile(src.path() + "/cancel.bin", 64 * 1024 * 1024);
        QSignalSpy finished(a->ls, &LocalSend::incomingFinished);
        connect(a->ls, &LocalSend::incomingRequest, this, [this](const QString &id) { a->ls->accept(id, false); },
                Qt::SingleShotConnection);
        LocalSendSender *s = sender(a->asTarget());
        connect(s, &LocalSendSender::progress, s, [s](qint64 done, qint64) {
            if (done > 4 * 1024 * 1024)
                s->cancel();
        });
        s->sendFiles(LocalSendSender::collect({file}));
        QVERIFY(waitState(s, {LocalSendSender::Cancelled, LocalSendSender::Done, LocalSendSender::Failed}));
        QCOMPARE(s->state(), LocalSendSender::Cancelled);
        QVERIFY(QTest::qWaitFor([&] { return finished.count() == 1; }, 5000));
        QVERIFY(!finished.first().at(2).toBool());
        QVERIFY(!QFile::exists(a->receive + "/cancel.bin"));
        QCOMPARE(QDir(a->receive).entryList(QDir::Hidden | QDir::Files).filter("lingmo-share-part").size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestLocalSend)
#include "tst_localsend.moc"
