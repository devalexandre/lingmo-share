/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "qrcode.h"

#include <QPainter>

#include <qrencode.h>

namespace QrCode {

QImage image(const QString &text, int size, const QColor &fg, const QColor &bg)
{
    QRcode *code = QRcode_encodeString(text.toUtf8().constData(), 0, QR_ECLEVEL_M, QR_MODE_8, 1);
    if (!code)
        return {};
    const int modules = code->width;
    const int quiet = 2;
    const int total = modules + quiet * 2;
    QImage img(total, total, QImage::Format_RGB32);
    img.fill(bg);
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (code->data[y * modules + x] & 1)
                img.setPixelColor(x + quiet, y + quiet, fg);
        }
    }
    QRcode_free(code);
    // Whole pixels per module: sharp edges for phone cameras
    const int scale = qMax(1, size / total);
    return img.scaled(total * scale, total * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

}
