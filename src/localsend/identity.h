/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LOCALSEND_IDENTITY_H
#define LOCALSEND_IDENTITY_H

#include <QSslCertificate>
#include <QSslKey>
#include <QString>

/*
 * The TLS identity of this device, like the official LocalSend apps: an RSA
 * 2048 key and a self-signed certificate ("CN=LocalSend User"). Its SHA-256
 * (uppercase hex of the DER) is the fingerprint other devices know us by.
 * Created once and kept in ~/.local/share/lingmo-share (mode 0600).
 */
class Identity
{
public:
    // Loads it from dir, creating it the first time. False on failure.
    bool load(const QString &dir);

    const QSslCertificate &certificate() const { return m_cert; }
    const QSslKey &key() const { return m_key; }
    QString fingerprint() const { return m_fingerprint; }

    static QString fingerprintOf(const QSslCertificate &cert);

private:
    static bool generate(const QString &certPath, const QString &keyPath);

    QSslCertificate m_cert;
    QSslKey m_key;
    QString m_fingerprint;
};

#endif
