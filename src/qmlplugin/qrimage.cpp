/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "qrimage.h"
#include "common/qrcode.h"

#include <QPainter>

QrImage::QrImage(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
}

void QrImage::setText(const QString &text)
{
    if (m_text == text)
        return;
    m_text = text;
    emit textChanged();
    update();
}

void QrImage::setColor(const QColor &c)
{
    m_color = c;
    emit textChanged();
    update();
}

void QrImage::setBackground(const QColor &c)
{
    m_background = c;
    emit textChanged();
    update();
}

void QrImage::paint(QPainter *painter)
{
    const int side = int(qMin(width(), height()));
    if (side <= 0 || m_text.isEmpty())
        return;
    const QImage img = QrCode::image(m_text, side, m_color, m_background);
    if (img.isNull())
        return;
    // Centered, whole pixels per module
    painter->fillRect(boundingRect(), m_background);
    const int x = int((width() - img.width()) / 2);
    const int y = int((height() - img.height()) / 2);
    painter->drawImage(QPoint(x, y), img);
}
