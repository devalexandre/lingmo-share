#include "shareintents.h"
#include "daemon.h"

#include <LingmoIntents/intentservice.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <memory>

using LingmoIntents::IntentCall;
using LingmoIntents::IntentResult;

namespace {

QString stateName(int state)
{
    static const char *names[] = {"waiting", "active", "done", "failed", "declined", "cancelled"};
    return state >= 0 && state < 6 ? QString::fromLatin1(names[state]) : QStringLiteral("unknown");
}

// A device by id, or by name (case-insensitive) among the reachable ones
QJsonObject findDevice(Daemon *daemon, const QString &ref, QStringList *names)
{
    const QJsonArray devices = QJsonDocument::fromJson(daemon->devicesJson().toUtf8()).array();
    for (const QJsonValue &v : devices) {
        const QJsonObject d = v.toObject();
        if (d.value(QStringLiteral("id")).toString() == ref)
            return d;
    }
    for (const QJsonValue &v : devices) {
        const QJsonObject d = v.toObject();
        if (d.value(QStringLiteral("sendable")).toBool(true))
            names->append(d.value(QStringLiteral("name")).toString());
        if (d.value(QStringLiteral("name")).toString().compare(ref, Qt::CaseInsensitive) == 0)
            return d;
    }
    return {};
}

}

void publishShareIntents(Daemon *daemon)
{
    auto *intents = new LingmoIntents::IntentService(QStringLiteral("lingmo-share"), daemon);
    intents->handleAsync(QStringLiteral("share.send"), [daemon](const IntentCall &call) {
        const QVariantMap p = call.params();
        const QStringList files = p.value(QStringLiteral("files")).toStringList();
        const QString text = p.value(QStringLiteral("text")).toString();
        if (files.isEmpty() == text.isEmpty())
            return call.finish(IntentResult::failed(QObject::tr("Send either files or a text."),
                                                    LingmoIntents::Errors::InvalidParams));
        QStringList names;
        const QJsonObject device = findDevice(daemon, p.value(QStringLiteral("device")).toString(), &names);
        if (device.isEmpty())
            return call.finish(IntentResult::failed(
                QObject::tr("Device not found: %1. Nearby: %2")
                    .arg(p.value(QStringLiteral("device")).toString(),
                         names.isEmpty() ? QObject::tr("none") : names.join(QStringLiteral(", ")))));
        const QString deviceId = device.value(QStringLiteral("id")).toString();
        const QString id = files.isEmpty() ? daemon->sendText(deviceId, text) : daemon->send(deviceId, files);
        if (id.isEmpty())
            return call.finish(IntentResult::failed(QObject::tr("The device can't receive now.")));
        auto result = [daemon, id, device]() {
            const QVariantMap t = daemon->transfer(id);
            return QVariantMap{{QStringLiteral("transferId"), id},
                               {QStringLiteral("device"), device.value(QStringLiteral("name")).toString()},
                               {QStringLiteral("state"), stateName(t.value(QStringLiteral("state")).toInt())},
                               {QStringLiteral("progress"), t.value(QStringLiteral("progress")).toDouble()}};
        };
        if (!p.value(QStringLiteral("wait")).toBool())
            return call.finish(IntentResult::ok(result()));
        // Wait for the transfer (polled like `lingmo-share send`), at most ~100 s
        auto *timer = new QTimer(daemon);
        auto ticks = std::make_shared<int>(0);
        QObject::connect(timer, &QTimer::timeout, daemon, [daemon, id, call, result, timer, ticks] {
            const QVariantMap t = daemon->transfer(id);
            const bool finished = t.value(QStringLiteral("finished")).toBool();
            if (!finished && ++*ticks < 330)
                return;
            timer->deleteLater();
            const int state = t.value(QStringLiteral("state")).toInt();
            if (finished && state != 2) {
                const QString error = t.value(QStringLiteral("error")).toString();
                return call.finish(IntentResult::failed(error.isEmpty() ? QObject::tr("The transfer did not complete (%1).").arg(stateName(state))
                                                                        : error));
            }
            call.finish(IntentResult::ok(result()));
        });
        timer->start(300);
    });
    intents->publish();     // /Intents next to /Share on com.lingmo.Share
}
