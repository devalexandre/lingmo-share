/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// com.lingmo.Settings /Theme as lingmo-settings-daemon offers it, for the UI
// screenshots: fake-theme [dark]

#include <QCoreApplication>
#include <QDBusConnection>
#include <QFontDatabase>
#include <QGuiApplication>

class Theme : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.lingmo.Theme")
    Q_PROPERTY(bool isDarkMode MEMBER dark)
    Q_PROPERTY(int accentColor MEMBER accent)
    Q_PROPERTY(double systemFontPointSize MEMBER size)
    Q_PROPERTY(QString systemFont MEMBER font)
    Q_PROPERTY(QString systemFixedFont MEMBER font)
    Q_PROPERTY(double devicePixelRatio MEMBER dpr)
public:
    bool dark = false;
    int accent = 0;
    double size = 9;
    double dpr = 1;
    QString font;
signals:
    void darkModeChanged(bool darkMode);
    void accentColorChanged(int accentColor);
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    Theme theme;
    theme.dark = app.arguments().contains(QStringLiteral("dark"));
    theme.font = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerObject(QStringLiteral("/Theme"), &theme, QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals);
    if (!bus.registerService(QStringLiteral("com.lingmo.Settings")))
        return 1;
    return app.exec();
}

#include "fake-theme.moc"
