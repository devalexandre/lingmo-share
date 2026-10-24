/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "common/safename.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestSafeName : public QObject
{
    Q_OBJECT
private slots:
    void sanitize_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("plain") << "photo.jpg" << "photo.jpg";
        QTest::newRow("folder") << "Album/a.jpg" << "Album/a.jpg";
        QTest::newRow("dotdot") << "../../.bashrc" << "bashrc";
        QTest::newRow("dotdot-mid") << "a/../../b.txt" << "a/b.txt";
        QTest::newRow("absolute") << "/etc/passwd" << "etc/passwd";
        QTest::newRow("windows") << "C:\\Users\\x\\evil.exe" << "C_/Users/x/evil.exe";
        QTest::newRow("hidden") << ".ssh/authorized_keys" << "ssh/authorized_keys";
        QTest::newRow("control") << QString::fromUtf8("a\nb\x01.txt") << "a_b_.txt";
        QTest::newRow("empty") << "" << "file";
        QTest::newRow("dots") << "...." << "file";
        QTest::newRow("unicode") << QString::fromUtf8("férias 2026 ☀️.png") << QString::fromUtf8("férias 2026 ☀️.png");
    }

    void sanitize()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(SafeName::sanitize(in), out);
    }

    void uniqueAndInside()
    {
        QTemporaryDir dir;
        const QString base = dir.path();
        const QString first = SafeName::uniqueTarget(base, QStringLiteral("x.txt"));
        QCOMPARE(first, base + "/x.txt");
        QFile f(first);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QCOMPARE(SafeName::uniqueTarget(base, QStringLiteral("x.txt")), base + "/x (2).txt");
        QCOMPARE(SafeName::uniqueTarget(base, QStringLiteral("../../x.txt")), base + "/x (2).txt");
        QCOMPARE(SafeName::uniqueTarget(base, QStringLiteral("a.tar.gz")), base + "/a.tar.gz");

        // A symlinked folder inside the receive folder must not lead outside it
        QTemporaryDir outside;
        QVERIFY(QFile::link(outside.path(), base + "/link"));
        QVERIFY(SafeName::uniqueTarget(base, QStringLiteral("link/evil.txt")).isEmpty());
        // A symlink planted with the target name is never written through
        QVERIFY(QFile::link(outside.path() + "/target", base + "/planted.txt"));
        QCOMPARE(SafeName::uniqueTarget(base, QStringLiteral("planted.txt")), base + "/planted (2).txt");
        QVERIFY(SafeName::isInside(base, base + "/sub/new.txt"));
        QVERIFY(!SafeName::isInside(base, base + "/../x"));
    }
};

QTEST_GUILESS_MAIN(TestSafeName)
#include "tst_safename.moc"
