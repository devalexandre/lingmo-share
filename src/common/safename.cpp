/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "safename.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace SafeName {

static QString cleanPart(QString part)
{
    QString out;
    out.reserve(part.size());
    for (const QChar c : std::as_const(part)) {
        // Control characters and characters other systems forbid
        if (c.unicode() < 0x20 || c.unicode() == 0x7f || QStringLiteral("<>:\"|?*").contains(c))
            out += QLatin1Char('_');
        else
            out += c;
    }
    out = out.trimmed();
    // No hidden files (".bashrc"), no "." / ".." and no trailing dots
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('.')))
        out.chop(1);
    // 255 bytes per name on most file systems
    while (out.toUtf8().size() > 200)
        out.chop(1);
    return out.trimmed();
}

QString sanitize(const QString &name)
{
    QString n = name;
    n.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList raw = n.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QStringList parts;
    for (const QString &p : raw) {
        const QString clean = cleanPart(p);
        if (!clean.isEmpty())
            parts << clean;
    }
    // Folders sent by LocalSend keep their structure, but not too deep
    while (parts.size() > 8)
        parts.removeAt(parts.size() - 2);
    if (parts.isEmpty())
        return QStringLiteral("file");
    return parts.join(QLatin1Char('/'));
}

bool isInside(const QString &folder, const QString &path)
{
    QString base = QFileInfo(folder).canonicalFilePath();
    if (base.isEmpty())
        base = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    // The target may not exist yet: resolve its closest existing parent
    QFileInfo info(path);
    QString probe = QDir::cleanPath(info.absoluteFilePath());
    QString rest;
    while (!QFileInfo::exists(probe) && probe != QLatin1String("/") && !probe.isEmpty()) {
        rest = QFileInfo(probe).fileName() + (rest.isEmpty() ? QString() : QStringLiteral("/") + rest);
        probe = QFileInfo(probe).absolutePath();
    }
    QString resolved = QFileInfo(probe).canonicalFilePath();
    if (!rest.isEmpty())
        resolved += QLatin1Char('/') + rest;
    resolved = QDir::cleanPath(resolved);
    return resolved == base || resolved.startsWith(base + QLatin1Char('/'));
}

QString uniqueTarget(const QString &folder, const QString &relative)
{
    const QString rel = sanitize(relative);
    QString path = QDir::cleanPath(folder + QLatin1Char('/') + rel);
    if (!isInside(folder, path))
        return {};

    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()) || !isInside(folder, info.absolutePath()))
        return {};

    if (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink())
        return path;

    const QString dir = info.absolutePath();
    QString base = info.completeBaseName();
    QString suffix = info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();
    // "archive.tar.gz" -> "archive (2).tar.gz"
    if (info.fileName().endsWith(QLatin1String(".tar.gz")) || info.fileName().endsWith(QLatin1String(".tar.xz"))) {
        base = info.fileName().chopped(7);
        suffix = info.fileName().right(7);
    }
    for (int i = 2; i < 10000; ++i) {
        const QString candidate = QStringLiteral("%1/%2 (%3)%4").arg(dir, base).arg(i).arg(suffix);
        if (!QFileInfo::exists(candidate) && !QFileInfo(candidate).isSymLink())
            return candidate;
    }
    return {};
}

}
