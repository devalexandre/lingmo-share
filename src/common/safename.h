/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SAFENAME_H
#define SAFENAME_H

#include <QString>

namespace SafeName {

// Turns a file name chosen by the other device ("photo.jpg", "album/a.jpg",
// "../../.bashrc", "C:\\x", ...) into a relative path that stays inside the
// receive folder: no "..", no absolute paths, no hidden files, no control
// characters, at most a few folder levels. Never empty.
QString sanitize(const QString &name);

// <folder>/<relative>, with " (2)", " (3)"... added before the extension when
// a file with that name exists already (nothing is ever overwritten). Creates
// the subfolders. Empty if the result would leave the folder.
QString uniqueTarget(const QString &folder, const QString &relative);

// True if path is folder itself or inside it (after resolving symlinks of the folder)
bool isInside(const QString &folder, const QString &path);

}

#endif
