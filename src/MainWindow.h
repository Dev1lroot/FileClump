// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 David Eichendorf and FileClump contributors
#pragma once

#include <QElapsedTimer>
#include <QMainWindow>

#include <memory>
#include <thread>
#include <vector>

#include "Scanner.h"

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;
class QTabWidget;
class QTimer;
class QTreeView;
class ExtModel;
class FileListModel;
class Treemap;
class TreeModel;
class TreeProxy;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    void startScan(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    QWidget* buildTopPanel();
    void buildMenus();
    void showAbout();
    void populateDrives();
    QString currentTarget();
    void onScanClicked();
    void onScanFinished(ScanResult* r);
    void onProgressTick();
    void updateSpaceInfo(const QString& path);
    void updateStatus(const std::vector<Node*>& nodes);

    std::vector<Node*> treeSelection() const;
    std::vector<Node*> fileSelection() const;
    void onTreeSelectionChanged();
    void onExtSelectionChanged();
    void selectInTree(Node* n);

    void showContextMenu(std::vector<Node*> nodes, const QPoint& globalPos);
    void openInTerminal(const QString& dir);
    void deleteNodes(const std::vector<Node*>& nodes, bool trash);
    void afterRemoval();
    void exportCsv(Node* from);
    void loadSettings();
    void saveSettings();

    // top panel
    QComboBox* m_drive = nullptr;
    QPushButton* m_scanBtn = nullptr;
    QLabel* m_scanStatus = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel *m_selValue = nullptr, *m_totalValue = nullptr, *m_usedValue = nullptr, *m_freeValue = nullptr;

    QTabWidget* m_tabs = nullptr;
    QTreeView* m_tree = nullptr;
    QTableView* m_extView = nullptr;
    QTableView* m_fileView = nullptr;
    QLineEdit* m_fileFilter = nullptr;
    QWidget* m_mapBox = nullptr;
    QLabel* m_mapPath = nullptr;
    Treemap* m_map = nullptr;
    QLabel* m_statusSel = nullptr;
    QLabel* m_statusPath = nullptr;

    TreeModel* m_treeModel = nullptr;
    TreeProxy* m_treeProxy = nullptr;
    ExtModel* m_extModel = nullptr;
    QSortFilterProxyModel* m_extProxy = nullptr;
    FileListModel* m_fileModel = nullptr;

    QAction *m_actShowMap = nullptr, *m_actAlloc = nullptr, *m_actNames = nullptr;
    QAction *m_actOneFs = nullptr, *m_actHardlinks = nullptr;

    std::unique_ptr<Scanner> m_scanner;
    std::thread m_thread;
    std::unique_ptr<ScanResult> m_result;
    QTimer* m_progressTimer = nullptr;
    QElapsedTimer m_elapsed;
    QString m_scanPath;
    quint64 m_expectedBytes = 0;
    bool m_syncing = false;
};
