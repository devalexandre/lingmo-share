/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QRCODE_H
#define QRCODE_H

#include <QColor>
#include <QImage>
#include <QString>

namespace QrCode {
// The QR code of text (libqrencode, made locally), size x size pixels with a
// quiet zone; dark modules in fg, the rest in bg
QImage image(const QString &text, int size, const QColor &fg = Qt::black, const QColor &bg = Qt::white);
}

#endif
