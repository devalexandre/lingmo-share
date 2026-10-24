/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef QRIMAGE_H
#define QRIMAGE_H

#include <QQuickPaintedItem>
#include <QQmlEngine>

// A QR code drawn locally (libqrencode): QrImage { text: "https://…" }
class QrImage : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY textChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY textChanged)

public:
    explicit QrImage(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    QString text() const { return m_text; }
    void setText(const QString &text);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    QColor background() const { return m_background; }
    void setBackground(const QColor &c);

signals:
    void textChanged();

private:
    QString m_text;
    QColor m_color = Qt::black;
    QColor m_background = Qt::white;
};

#endif
