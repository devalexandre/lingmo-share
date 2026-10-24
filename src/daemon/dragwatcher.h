/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRAGWATCHER_H
#define DRAGWATCHER_H

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QTimer>

/*
 * Notices drags anywhere on the X11 desktop, without a window in the way: an
 * XDND source takes ownership of the XdndSelection selection when a drag
 * starts, and XFixes tells us. While the drag goes on, the pointer buttons are
 * polled to see it end. Idle cost: one event filter, no timer.
 */
class DragWatcher : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit DragWatcher(QObject *parent = nullptr);
    ~DragWatcher() override;

    bool start();
    bool dragging() const { return m_dragging; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void dragStarted();
    void dragEnded();

private:
    void poll();

    bool m_started = false;
    bool m_dragging = false;
    quint8 m_xfixesEvent = 0;
    quint32 m_xdndSelection = 0;
    int m_released = 0;
    QTimer m_poll;
};

#endif
