// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 David Eichendorf and FileClump contributors
#include "Models.h"

#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>

#include <algorithm>
#include <cstring>
#include <strings.h>

// ---------------------------------------------------------------- IconCache

IconCache& IconCache::get()
{
    static IconCache c;
    return c;
}

IconCache::IconCache()
{
    folder = QIcon::fromTheme(QStringLiteral("folder"));
    drive = QIcon::fromTheme(QStringLiteral("drive-harddisk"), folder);
    group = QIcon::fromTheme(QStringLiteral("document-multiple"), QIcon::fromTheme(QStringLiteral("folder-documents")));
    folderLocked = QIcon::fromTheme(QStringLiteral("folder-locked"), folder);
}

void IconCache::reset(const ScanResult* r)
{
    m_res = r;
    size_t n = r ? r->exts.size() : 0;
    m_icons.assign(n, QIcon());
    m_swatches.assign(n, QIcon());
    m_types.assign(n, QString());
}

QMimeType IconCache::mime(uint16_t id)
{
    return m_db.mimeTypeForFile(QStringLiteral("x.") + QString::fromStdString(m_res->exts[id].name),
        QMimeDatabase::MatchExtension);
}

QString IconCache::extLabel(uint16_t id) const
{
    if (!m_res || id == 0)
        return QString();
    return QLatin1Char('.') + QFile::decodeName(m_res->exts[id].name.c_str());
}

QIcon IconCache::forExt(uint16_t id)
{
    if (!m_res || id >= m_icons.size())
        return {};
    QIcon& ic = m_icons[id];
    if (ic.isNull()) {
        QMimeType mt = id ? mime(id) : m_db.mimeTypeForName(QStringLiteral("application/octet-stream"));
        ic = QIcon::fromTheme(mt.iconName(),
            QIcon::fromTheme(mt.genericIconName(), QIcon::fromTheme(QStringLiteral("text-x-generic"))));
    }
    return ic;
}

QIcon IconCache::swatch(uint16_t id)
{
    if (!m_res || id >= m_swatches.size())
        return {};
    QIcon& ic = m_swatches[id];
    if (ic.isNull()) {
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        QColor c = QColor::fromRgb(m_res->exts[id].color);
        QLinearGradient g(0, 0, 16, 16);
        g.setColorAt(0, c.lighter(135));
        g.setColorAt(1, c.darker(130));
        p.fillRect(1, 1, 14, 14, g);
        p.setPen(QColor(0, 0, 0, 90));
        p.drawRect(1, 1, 13, 13);
        ic = QIcon(pm);
    }
    return ic;
}

QString IconCache::typeName(uint16_t id)
{
    if (!m_res || id >= m_types.size())
        return {};
    QString& t = m_types[id];
    if (t.isEmpty()) {
        if (id == 0) {
            t = QStringLiteral("(No Extension)");
        } else {
            QMimeType mt = mime(id);
            if (mt.isDefault())
                t = QString::fromStdString(m_res->exts[id].name).toUpper() + QStringLiteral(" File");
            else
                t = mt.comment();
        }
    }
    return t;
}

// ---------------------------------------------------------- PercentDelegate

void PercentDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const
{
    QStyleOptionViewItem o(opt);
    initStyleOption(&o, idx);
    const QString text = o.text;
    o.text.clear();
    const QWidget* w = o.widget;
    QStyle* st = w ? w->style() : QApplication::style();
    st->drawControl(QStyle::CE_ItemViewItem, &o, p, w);

    double f = std::clamp(idx.data(Roles::Fraction).toDouble(), 0.0, 1.0);
    QRect r = o.rect.adjusted(2, 2, -2, -2);
    p->save();
    p->fillRect(r, QColor(255, 255, 255, 40));
    if (f > 0)
        p->fillRect(QRect(r.left(), r.top(), std::max(1, int(r.width() * f + 0.5)), r.height()), QColor(125, 125, 255));
    const bool sel = o.state & QStyle::State_Selected;
    p->setPen(o.palette.color(sel ? QPalette::HighlightedText : QPalette::Text));
    p->drawText(r.adjusted(2, 0, -3, 0), Qt::AlignRight | Qt::AlignVCenter, text);
    p->restore();
}

// ---------------------------------------------------------------- TreeModel

void TreeModel::setResult(ScanResult* r)
{
    beginResetModel();
    m_res = r;
    endResetModel();
}

QModelIndex TreeModel::indexFor(Node* n) const
{
    if (!m_res || !n)
        return {};
    if (n == m_res->root)
        return createIndex(0, 0, n);
    return createIndex(n->row(), 0, n);
}

QString TreeModel::displayName(const Node* n) const
{
    if (n == m_res->root)
        return QFile::decodeName(m_res->rootPath.c_str());
    if (n->type == NT_Group)
        return QStringLiteral("[%1 Files in %2]").arg(fmtCount(n->files), nodePath(m_res, n->parent));
    return nodeName(n);
}

QModelIndex TreeModel::index(int row, int col, const QModelIndex& parent) const
{
    if (!m_res || row < 0 || col < 0 || col >= ColCount)
        return {};
    if (!parent.isValid())
        return row == 0 ? createIndex(0, col, m_res->root) : QModelIndex();
    Node* p = nodeAt(parent);
    if (uint32_t(row) >= p->nkids)
        return {};
    return createIndex(row, col, &p->kids[row]);
}

QModelIndex TreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid() || !m_res)
        return {};
    Node* n = nodeAt(child);
    Node* p = n->parent;
    if (!p)
        return {};
    return createIndex(p == m_res->root ? 0 : p->row(), 0, p);
}

int TreeModel::rowCount(const QModelIndex& parent) const
{
    if (!m_res)
        return 0;
    if (!parent.isValid())
        return 1;
    if (parent.column() > 0)
        return 0;
    return int(nodeAt(parent)->nkids);
}

bool TreeModel::hasChildren(const QModelIndex& parent) const
{
    if (!m_res)
        return false;
    if (!parent.isValid())
        return true;
    if (parent.column() > 0)
        return false;
    return nodeAt(parent)->nkids > 0;
}

QVariant TreeModel::data(const QModelIndex& idx, int role) const
{
    if (!idx.isValid() || !m_res)
        return {};
    const Node* n = nodeAt(idx);
    const int c = idx.column();
    const bool file = n->type == NT_File;
    auto fraction = [n] {
        const Node* p = n->parent;
        return (!p) ? 1.0 : (p->size ? double(n->size) / double(p->size) : 0.0);
    };

    switch (role) {
    case Qt::DisplayRole:
        switch (c) {
        case ColName: return displayName(n);
        case ColPct: return fmtPct(fraction());
        case ColSize: return fmtSize(n->size);
        case ColAlloc: return fmtSize(n->alloc);
        case ColItems: return file ? QVariant() : fmtCount(quint64(n->files) + n->folders);
        case ColFiles: return file ? QVariant() : fmtCount(n->files);
        case ColFolders: return n->type == NT_Dir ? fmtCount(n->folders) : QVariant();
        case ColModified: return fmtTime(n->mtime);
        }
        break;
    case Qt::TextAlignmentRole:
        return QVariant::fromValue(c == ColName ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignRight | Qt::AlignVCenter));
    case Qt::DecorationRole:
        if (c != ColName)
            break;
        if (n == m_res->root)
            return IconCache::get().drive;
        if (n->type == NT_Dir)
            return (n->flags & NF_Error) ? IconCache::get().folderLocked : IconCache::get().folder;
        if (n->type == NT_Group)
            return IconCache::get().group;
        return IconCache::get().forExt(n->ext);
    case Qt::ForegroundRole:
        if (n->flags & (NF_Error | NF_OtherFs | NF_Hardlink))
            return QColor(140, 140, 140);
        break;
    case Qt::ToolTipRole:
        if (n->flags & NF_Virtual)
            return QStringLiteral("Virtual filesystem - skipped");
        if (n->flags & NF_OtherFs)
            return QStringLiteral("Mount point of another filesystem - not scanned");
        if (n->flags & NF_Error)
            return QStringLiteral("Could not be read (permission denied?)");
        if (n->flags & NF_Hardlink)
            return QStringLiteral("Hard link - already counted elsewhere");
        if (n->flags & NF_Symlink)
            return QStringLiteral("Symbolic link");
        break;
    case Roles::Fraction:
        return fraction();
    }
    return {};
}

QVariant TreeModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal)
        return {};
    if (role == Qt::TextAlignmentRole)
        return QVariant::fromValue(section == ColName ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    static const char* names[] = {"Folder", "% of Parent", "Size", "Allocated", "Items", "Files", "Folders", "Modified"};
    return QString::fromLatin1(names[section]);
}

// ---------------------------------------------------------------- TreeProxy

bool TreeProxy::lessThan(const QModelIndex& l, const QModelIndex& r) const
{
    const Node* a = TreeModel::nodeAt(l);
    const Node* b = TreeModel::nodeAt(r);
    switch (l.column()) {
    case TreeModel::ColName: {
        // Group row ("[N Files in ...]") always goes after folders.
        if ((a->type == NT_Group) != (b->type == NT_Group))
            return b->type == NT_Group;
        return strcasecmp(a->name ? a->name : "", b->name ? b->name : "") < 0;
    }
    case TreeModel::ColAlloc: return a->alloc < b->alloc;
    case TreeModel::ColItems: return quint64(a->files) + a->folders < quint64(b->files) + b->folders;
    case TreeModel::ColFiles: return a->files < b->files;
    case TreeModel::ColFolders: return a->folders < b->folders;
    case TreeModel::ColModified: return a->mtime < b->mtime;
    default: return a->size < b->size;
    }
}

bool TreeProxy::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    if (!parent.isValid())
        return true;
    const Node* p = TreeModel::nodeAt(parent);
    return uint32_t(row) < p->nkids && !(p->kids[row].flags & NF_Deleted);
}

// ----------------------------------------------------------------- ExtModel

void ExtModel::setResult(ScanResult* r)
{
    m_res = r;
    refresh();
}

void ExtModel::refresh()
{
    beginResetModel();
    m_rows.clear();
    if (m_res) {
        for (size_t i = 0; i < m_res->exts.size(); ++i)
            if (m_res->exts[i].files > 0)
                m_rows.push_back(uint16_t(i));
        std::sort(m_rows.begin(), m_rows.end(), [this](uint16_t a, uint16_t b) { return m_res->exts[a].size > m_res->exts[b].size; });
    }
    endResetModel();
}

QVariant ExtModel::data(const QModelIndex& idx, int role) const
{
    if (!idx.isValid() || !m_res)
        return {};
    const uint16_t id = m_rows[idx.row()];
    const ExtInfo& e = m_res->exts[id];
    const int c = idx.column();
    const double total = double(m_res->root->size);
    const double frac = total > 0 ? double(e.size) / total : 0.0;
    auto& ic = IconCache::get();

    switch (role) {
    case Qt::DisplayRole:
        switch (c) {
        case ColExt: return ic.extLabel(id);
        case ColType: return ic.typeName(id);
        case ColPct: return fmtPct(frac);
        case ColSize: return fmtSize(e.size);
        case ColAlloc: return fmtSize(e.alloc);
        case ColFiles: return fmtCount(e.files);
        }
        break;
    case Roles::Sort:
        switch (c) {
        case ColExt: return ic.extLabel(id);
        case ColType: return ic.typeName(id);
        case ColAlloc: return qulonglong(e.alloc);
        case ColFiles: return e.files;
        default: return qulonglong(e.size);
        }
    case Qt::DecorationRole:
        if (c == ColExt)
            return ic.swatch(id);
        if (c == ColType)
            return ic.forExt(id);
        break;
    case Qt::TextAlignmentRole:
        return QVariant::fromValue(c <= ColType ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignRight | Qt::AlignVCenter));
    case Roles::Fraction:
        return frac;
    }
    return {};
}

QVariant ExtModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal)
        return {};
    if (role == Qt::TextAlignmentRole)
        return QVariant::fromValue(section <= ColType ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    static const char* names[] = {"Extension", "File Type", "Percent", "Size", "Allocated", "Files"};
    return QString::fromLatin1(names[section]);
}

// ------------------------------------------------------------ FileListModel

void FileListModel::setResult(ScanResult* r)
{
    beginResetModel();
    m_res = r;
    m_built = false;
    m_all.clear();
    m_view.clear();
    endResetModel();
}

void FileListModel::ensureBuilt()
{
    if (m_built || !m_res)
        return;
    m_built = true;
    m_all.clear();
    m_all.reserve(m_res->root->files);
    std::vector<Node*> stack {m_res->root};
    while (!stack.empty()) {
        Node* n = stack.back();
        stack.pop_back();
        for (uint32_t i = 0; i < n->nkids; ++i) {
            Node* k = &n->kids[i];
            if (k->flags & NF_Deleted)
                continue;
            if (k->type == NT_File)
                m_all.push_back(k);
            else
                stack.push_back(k);
        }
    }
    std::sort(m_all.begin(), m_all.end(), [](const Node* a, const Node* b) { return a->size > b->size; });
    applyFilter();
}

void FileListModel::refresh()
{
    if (!m_built)
        return;
    m_all.erase(std::remove_if(m_all.begin(), m_all.end(), ScanResult::isDeleted), m_all.end());
    applyFilter();
}

void FileListModel::setFilter(const QString& pattern)
{
    m_filter = pattern.trimmed();
    if (m_built)
        applyFilter();
}

void FileListModel::applyFilter()
{
    beginResetModel();
    if (m_filter.isEmpty()) {
        m_view = m_all;
    } else {
        m_view.clear();
        const bool wild = m_filter.contains(QLatin1Char('*')) || m_filter.contains(QLatin1Char('?'))
            || m_filter.contains(QLatin1Char('['));
        const bool byPath = m_filter.contains(QLatin1Char('/'));
        QRegularExpression re;
        if (wild)
            re = QRegularExpression(QRegularExpression::wildcardToRegularExpression(m_filter),
                QRegularExpression::CaseInsensitiveOption);
        for (Node* n : m_all) {
            const QString s = byPath ? nodePath(m_res, n) : nodeName(n);
            if (wild ? re.match(s).hasMatch() : s.contains(m_filter, Qt::CaseInsensitive))
                m_view.push_back(n);
        }
    }
    sortView();
    endResetModel();
}

void FileListModel::sortView()
{
    const bool desc = m_sortOrder == Qt::DescendingOrder;
    auto cmp = [this, desc](const Node* a, const Node* b) {
        bool lt;
        switch (m_sortCol) {
        case ColName: lt = strcasecmp(a->name, b->name) < 0; break;
        case ColAlloc: lt = a->alloc < b->alloc; break;
        case ColModified: lt = a->mtime < b->mtime; break;
        case ColFolder: lt = a->parent < b->parent; break;
        default: lt = a->size < b->size; break;
        }
        return lt;
    };
    if (desc)
        std::stable_sort(m_view.begin(), m_view.end(), [&](const Node* a, const Node* b) { return cmp(b, a); });
    else
        std::stable_sort(m_view.begin(), m_view.end(), cmp);
}

void FileListModel::sort(int column, Qt::SortOrder order)
{
    m_sortCol = column;
    m_sortOrder = order;
    beginResetModel();
    sortView();
    endResetModel();
}

QVariant FileListModel::data(const QModelIndex& idx, int role) const
{
    if (!idx.isValid() || !m_res)
        return {};
    const Node* n = m_view[idx.row()];
    const int c = idx.column();
    switch (role) {
    case Qt::DisplayRole:
        switch (c) {
        case ColName: return nodeName(n);
        case ColSize: return fmtSize(n->size);
        case ColAlloc: return fmtSize(n->alloc);
        case ColModified: return fmtTime(n->mtime);
        case ColFolder: return nodePath(m_res, n->parent);
        }
        break;
    case Qt::DecorationRole:
        if (c == ColName)
            return IconCache::get().forExt(n->ext);
        break;
    case Qt::TextAlignmentRole:
        return QVariant::fromValue((c == ColName || c == ColFolder) ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignRight | Qt::AlignVCenter));
    }
    return {};
}

QVariant FileListModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    static const char* names[] = {"File Name", "Size", "Allocated", "Modified", "Folder"};
    return QString::fromLatin1(names[section]);
}
