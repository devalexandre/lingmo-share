/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dragwatcher.h"

#include <QGuiApplication>

#include <xcb/xcb.h>
#include <xcb/xfixes.h>

#include <cstdlib>

static xcb_connection_t *connection()
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->connection() : nullptr;
}

static xcb_window_t rootWindow(xcb_connection_t *c)
{
    const xcb_setup_t *setup = xcb_get_setup(c);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    return it.rem ? it.data->root : XCB_NONE;
}

DragWatcher::DragWatcher(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(60);
    connect(&m_poll, &QTimer::timeout, this, &DragWatcher::poll);
}

DragWatcher::~DragWatcher()
{
    if (m_started)
        qApp->removeNativeEventFilter(this);
}

bool DragWatcher::start()
{
    xcb_connection_t *c = connection();
    if (!c || m_started)
        return m_started;
    const xcb_query_extension_reply_t *ext = xcb_get_extension_data(c, &xcb_xfixes_id);
    if (!ext || !ext->present)
        return false;
    m_xfixesEvent = ext->first_event;
    free(xcb_xfixes_query_version_reply(c, xcb_xfixes_query_version(c, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION), nullptr));

    xcb_intern_atom_reply_t *atom = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, 13, "XdndSelection"), nullptr);
    if (!atom)
        return false;
    m_xdndSelection = atom->atom;
    free(atom);

    xcb_xfixes_select_selection_input(c, rootWindow(c), m_xdndSelection,
                                      XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER);
    xcb_flush(c);
    qApp->installNativeEventFilter(this);
    m_started = true;
    return true;
}

bool DragWatcher::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "xcb_generic_event_t")
        return false;
    auto *ev = static_cast<xcb_generic_event_t *>(message);
    if ((ev->response_type & ~0x80) != m_xfixesEvent + XCB_XFIXES_SELECTION_NOTIFY)
        return false;
    auto *sel = reinterpret_cast<xcb_xfixes_selection_notify_event_t *>(ev);
    if (sel->selection != m_xdndSelection || sel->owner == XCB_NONE)
        return false;
    // A drag started somewhere (or a new one replaced it)
    m_released = 0;
    if (!m_dragging) {
        m_dragging = true;
        emit dragStarted();
    }
    m_poll.start();
    return false;
}

void DragWatcher::poll()
{
    xcb_connection_t *c = connection();
    if (!c) {
        m_poll.stop();
        return;
    }
    xcb_query_pointer_reply_t *p = xcb_query_pointer_reply(c, xcb_query_pointer(c, rootWindow(c)), nullptr);
    if (!p)
        return;
    const bool pressed = p->mask & (XCB_BUTTON_MASK_1 | XCB_BUTTON_MASK_2 | XCB_BUTTON_MASK_3);
    free(p);
    // Two polls in a row without a button: the drag ended (dropped or cancelled)
    if (pressed) {
        m_released = 0;
        return;
    }
    if (++m_released < 2)
        return;
    m_poll.stop();
    if (m_dragging) {
        m_dragging = false;
        emit dragEnded();
    }
}
