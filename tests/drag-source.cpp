/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// A window that drags the files given on the command line (text/uri-list),
// like a file manager would. Driven by xdotool in the drop-bubble test.

#include <QApplication>
#include <QDrag>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QTextStream>
#include <QUrl>

class Source : public QLabel
{
public:
    QList<QUrl> urls;
    QPoint pressPos;
    using QLabel::QLabel;

protected:
    void mousePressEvent(QMouseEvent *e) override { pressPos = e->position().toPoint(); }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & Qt::LeftButton) || (e->position().toPoint() - pressPos).manhattanLength() < 8)
            return;
        auto *drag = new QDrag(this);
        auto *mime = new QMimeData;
        mime->setUrls(urls);
        drag->setMimeData(mime);
        const Qt::DropAction result = drag->exec(Qt::CopyAction);
        QTextStream(stdout) << "drag-source: drop result " << int(result) << Qt::endl;
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Source w(QStringLiteral("Drag me"));
    for (const QString &a : app.arguments().mid(1))
        w.urls << QUrl::fromLocalFile(QFileInfo(a).absoluteFilePath());
    w.setAlignment(Qt::AlignCenter);
    w.resize(220, 140);
    w.move(60, 60);
    w.setWindowTitle(QStringLiteral("drag-source"));
    w.show();
    return app.exec();
}
