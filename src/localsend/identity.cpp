/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "identity.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <memory>

QString Identity::fingerprintOf(const QSslCertificate &cert)
{
    return QString::fromLatin1(QCryptographicHash::hash(cert.toDer(), QCryptographicHash::Sha256).toHex().toUpper());
}

template<typename T, void (*F)(T *)>
struct Deleter {
    void operator()(T *p) const { F(p); }
};
using PKeyPtr = std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY, EVP_PKEY_free>>;
using X509Ptr = std::unique_ptr<X509, Deleter<X509, X509_free>>;
using BioPtr = std::unique_ptr<BIO, Deleter<BIO, BIO_free_all>>;

static QByteArray bioData(BIO *bio)
{
    char *data = nullptr;
    const long size = BIO_get_mem_data(bio, &data);
    return QByteArray(data, int(size));
}

static bool writePrivate(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.write(data);
    return file.commit();
}

bool Identity::generate(const QString &certPath, const QString &keyPath)
{
    PKeyPtr pkey(EVP_RSA_gen(2048));
    if (!pkey)
        return false;

    X509Ptr x509(X509_new());
    X509_set_version(x509.get(), 2);
    // Random positive 127-bit serial
    unsigned char serial[16];
    RAND_bytes(serial, sizeof serial);
    serial[0] &= 0x7f;
    BIGNUM *bn = BN_bin2bn(serial, sizeof serial, nullptr);
    BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(x509.get()));
    BN_free(bn);

    // Valid from the past (clocks drift) for a long time: the fingerprint is the
    // identity, rotating it would make trusted devices ask again
    X509_gmtime_adj(X509_getm_notBefore(x509.get()), -86400L * 2);
    ASN1_TIME_set_string(X509_getm_notAfter(x509.get()), "20751231235959Z");
    X509_set_pubkey(x509.get(), pkey.get());

    X509_NAME *name = X509_get_subject_name(x509.get());
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("LocalSend User"), -1, -1, 0);
    X509_set_issuer_name(x509.get(), name);
    if (!X509_sign(x509.get(), pkey.get(), EVP_sha256()))
        return false;

    BioPtr keyBio(BIO_new(BIO_s_mem()));
    BioPtr certBio(BIO_new(BIO_s_mem()));
    if (!PEM_write_bio_PrivateKey(keyBio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr)
        || !PEM_write_bio_X509(certBio.get(), x509.get()))
        return false;

    return writePrivate(keyPath, bioData(keyBio.get())) && writePrivate(certPath, bioData(certBio.get()));
}

bool Identity::load(const QString &dir)
{
    QDir().mkpath(dir);
    const QString certPath = dir + QStringLiteral("/localsend-cert.pem");
    const QString keyPath = dir + QStringLiteral("/localsend-key.pem");

    for (int attempt = 0; attempt < 2; ++attempt) {
        QFile certFile(certPath), keyFile(keyPath);
        if (certFile.open(QIODevice::ReadOnly) && keyFile.open(QIODevice::ReadOnly)) {
            const QList<QSslCertificate> certs = QSslCertificate::fromData(certFile.readAll(), QSsl::Pem);
            const QSslKey key(keyFile.readAll(), QSsl::Rsa, QSsl::Pem);
            if (!certs.isEmpty() && !key.isNull()) {
                m_cert = certs.first();
                m_key = key;
                m_fingerprint = fingerprintOf(m_cert);
                return true;
            }
        }
        if (attempt == 0 && !generate(certPath, keyPath)) {
            qWarning() << "lingmo-share: could not create the TLS identity in" << dir;
            return false;
        }
    }
    return false;
}
