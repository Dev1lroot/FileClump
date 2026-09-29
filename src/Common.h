// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 David Eichendorf and FileClump contributors
#pragma once

#include <QDateTime>
#include <QFile>
#include <QIcon>
#include <QLocale>
#include <QMimeDatabase>
#include <QString>

#include <vector>

#include "Scanner.h"

namespace Roles {
enum {
    Sort = Qt::UserRole + 1,
    Fraction, // 0..1, drawn as a bar by PercentDelegate
};
}

inline QString fmtCount(quint64 n) { return QLocale().toString(qulonglong(n)); }

inline QString fmtSize(quint64 b)
{
    if (b == 0)
        return QStringLiteral("0");
    if (b < 1024)
        return QStringLiteral("%1 Bytes").arg(b);
    static const char* units[] = {"KB", "MB", "GB", "TB", "PB"};
    double v = double(b) / 1024.0;
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    return QLocale().toString(v, 'f', 1) + QLatin1Char(' ') + QLatin1String(units[u]);
}

inline QString fmtPct(double f) { return QLocale().toString(f * 100.0, 'f', 1) + QStringLiteral(" %"); }

inline QString fmtTime(qint64 t)
{
    if (t <= 0)
        return {};
    return QDateTime::fromSecsSinceEpoch(t).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

inline QString nodeName(const Node* n) { return n && n->name ? QFile::decodeName(n->name) : QString(); }
inline QString nodePath(const ScanResult* r, const Node* n) { return QFile::decodeName(r->fullPath(n).c_str()); }

// Per-extension icons, type names and color swatches, cached by extension id.
class IconCache {
public:
    static IconCache& get();
    void reset(const ScanResult* r);

    QIcon forExt(uint16_t id);
    QIcon swatch(uint16_t id);
    QString typeName(uint16_t id);
    QString extLabel(uint16_t id) const;

    QIcon folder, drive, group, folderLocked;

private:
    IconCache();
    QMimeType mime(uint16_t id);

    const ScanResult* m_res = nullptr;
    QMimeDatabase m_db;
    std::vector<QIcon> m_icons, m_swatches;
    std::vector<QString> m_types;
};
