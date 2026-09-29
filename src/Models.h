// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 David Eichendorf and FileClump contributors
#pragma once

#include <QAbstractItemModel>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>

#include "Common.h"

// "% of Parent" style cell: a filled bar with the percentage on top.
class PercentDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
};

class TreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Col { ColName, ColPct, ColSize, ColAlloc, ColItems, ColFiles, ColFolders, ColModified, ColCount };

    using QAbstractItemModel::QAbstractItemModel;
    void setResult(ScanResult* r);
    ScanResult* result() const { return m_res; }
    static Node* nodeAt(const QModelIndex& idx) { return static_cast<Node*>(idx.internalPointer()); }
    QModelIndex indexFor(Node* n) const;
    QString displayName(const Node* n) const;

    QModelIndex index(int row, int col, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return ColCount; }
    bool hasChildren(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& idx, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    ScanResult* m_res = nullptr;
};

class TreeProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool lessThan(const QModelIndex& l, const QModelIndex& r) const override;
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
};

class ExtModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { ColExt, ColType, ColPct, ColSize, ColAlloc, ColFiles, ColCount };

    using QAbstractTableModel::QAbstractTableModel;
    void setResult(ScanResult* r);
    void refresh();
    uint16_t extAt(int row) const { return m_rows[row]; }

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    int columnCount(const QModelIndex& = {}) const override { return ColCount; }
    QVariant data(const QModelIndex& idx, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    ScanResult* m_res = nullptr;
    std::vector<uint16_t> m_rows;
};

// Flat list of every file, largest first ("File View" tab).
class FileListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { ColName, ColSize, ColAlloc, ColModified, ColFolder, ColCount };

    using QAbstractTableModel::QAbstractTableModel;
    void setResult(ScanResult* r);
    void ensureBuilt();
    void refresh(); // drop deleted files
    void setFilter(const QString& pattern);
    Node* nodeAt(int row) const { return m_view[row]; }
    size_t totalFiles() const { return m_all.size(); }

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_view.size()); }
    int columnCount(const QModelIndex& = {}) const override { return ColCount; }
    QVariant data(const QModelIndex& idx, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    void sort(int column, Qt::SortOrder order) override;

private:
    void applyFilter();
    void sortView();

    ScanResult* m_res = nullptr;
    bool m_built = false;
    std::vector<Node*> m_all, m_view;
    QString m_filter;
    int m_sortCol = ColSize;
    Qt::SortOrder m_sortOrder = Qt::DescendingOrder;
};
