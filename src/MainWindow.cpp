// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 David Eichendorf and FileClump contributors
#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStorageInfo>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include "Common.h"
#include "Models.h"
#include "Treemap.h"

namespace {

const char* kAppName = "FileClump";
const char* kVersion = "0.1";
const char* kRepoUrl = "https://github.com/Dev1lroot/FileClump";
const QString kBrowse = QStringLiteral("\x01browse");

QPixmap makeLogo(int s)
{
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(25, 25, 25));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(0, 0, s, s), s * 0.12, s * 0.12);
    auto block = [&](double x, double y, double w, double h, QColor c) {
        QRectF r(x * s, y * s, w * s, h * s);
        r.adjust(s * 0.02, s * 0.02, -s * 0.02, -s * 0.02);
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, c.lighter(150));
        g.setColorAt(1, c.darker(160));
        p.setBrush(g);
        p.drawRect(r);
    };
    block(0.06, 0.06, 0.52, 0.56, QColor(230, 120, 0));
    block(0.58, 0.06, 0.36, 0.34, QColor(200, 200, 0));
    block(0.58, 0.40, 0.36, 0.22, QColor(20, 180, 20));
    block(0.06, 0.62, 0.30, 0.32, QColor(144, 48, 230));
    block(0.36, 0.62, 0.34, 0.32, QColor(30, 110, 255));
    block(0.70, 0.62, 0.24, 0.32, QColor(230, 40, 150));
    return pm;
}

QFrame* panel()
{
    auto* f = new QFrame;
    f->setFrameShape(QFrame::StyledPanel);
    f->setFrameShadow(QFrame::Sunken);
    return f;
}

bool skipFs(const QByteArray& t)
{
    static const char* skip[] = {"squashfs", "tmpfs", "devtmpfs", "overlay", "ramfs", "efivarfs", "proc", "sysfs",
        "nsfs", "autofs", "cgroup", "cgroup2", "bpf", "tracefs", "debugfs", "securityfs", "pstore", "configfs",
        "fusectl", "binfmt_misc", "mqueue", "hugetlbfs", "devpts", "fuse.portal", "fuse.gvfsd-fuse", "fuse.snapfuse"};
    for (const char* s : skip)
        if (t == s)
            return true;
    return false;
}

QString aboutHtml()
{
    QFile lf(QStringLiteral(":/LICENSE"));
    const QString license = lf.open(QIODevice::ReadOnly) ? QString::fromUtf8(lf.readAll()).toHtmlEscaped() : QString();
    const QString threads = QString::number(std::clamp(std::thread::hardware_concurrency() * 2, 4u, 32u));

    return QStringLiteral("<p>Built with C++ / Qt ") + QString::fromLatin1(qVersion())
        + QStringLiteral(", ") + threads + QStringLiteral(" scan threads. No Electron, no web runtime.</p>") + QStringLiteral(R"(
<p><a href="#authors">Authors</a> &middot; <a href="#help">Help</a> &middot; <a href="#credits">Credits &amp; inspiration</a> &middot; <a href="#license">License</a></p>

<h3><a name="authors"></a>Authors</h3>
<p><b>Original developers (v0.1)</b></p>
<ul>
<li><b>David Eichendorf</b> (<a href="https://github.com/Dev1lroot">@Dev1lroot</a>): project author; idea, design direction and testing</li>
<li><b>Claude</b> (Anthropic, Claude Opus 5.5 via Claude Code): implementation of the initial version</li>
</ul>
<p><b>Contributors</b><br>
Everyone who has added features or fixed bugs since v0.1 is listed in
<a href="https://github.com/Dev1lroot/FileClump/blob/main/AUTHORS.md">AUTHORS.md</a>.
Contributions are welcome; see
<a href="https://github.com/Dev1lroot/FileClump/blob/main/CONTRIBUTING.md">CONTRIBUTING.md</a>.</p>

<h3><a name="credits"></a>Credits &amp; inspiration</h3>
<p>FileClump builds on ideas from a long line of treemap-based disk usage tools and research:</p>
<ul>
<li><b>Treemaps</b>: invented by Ben Shneiderman (University of Maryland, 1991).</li>
<li><b>SequoiaView</b>: Technische Universiteit Eindhoven. Introduced cushion-shaded treemaps
(Jarke J. van Wijk and Huub van de Wetering, 1999) and the squarified layout
(Mark Bruls, Kees Huizing and Jarke J. van Wijk, 2000).</li>
<li><b>WinDirStat</b>: Bernhard Seifert, Oliver Schneider and contributors
(<a href="https://windirstat.net">windirstat.net</a>). Popularised the directory tree + extension list +
coloured treemap layout.</li>
<li><b>WizTree</b>: a product of <b>Antibody Software Limited</b>
(<a href="https://diskanalyzer.com">diskanalyzer.com</a>). Showed how fast such a tool can be.</li>
</ul>
<p>All names and trademarks above belong to their respective owners. FileClump is an independent project.
It is <b>not affiliated with, endorsed by, or connected to</b> any of these projects or their authors,
including Antibody Software Limited. It contains none of their code, binaries, logos or other assets.
Everything was written from scratch for Linux.</p>
<p>FileClump is free software. It is <b>not sold</b>, has no paid edition and asks for no donations.
If you use Windows, please consider supporting the original tools.</p>

<h3><a name="help"></a>Help</h3>
<h4>Getting started</h4>
<ol>
<li>Pick a mounted drive, your home folder, or <i>Select folder&hellip;</i> in the <b>Select</b> list.</li>
<li>Press <b>Scan</b>. Press <b>Stop</b> to cancel; you still get what was scanned so far.</li>
<li>Use the tree, extension list and treemap to find what is using your space.</li>
</ol>
<p>Some folders can only be read by root, and show a lock icon. To see everything, run
<code>sudo -E fileclump /</code>.</p>

<h4>Treemap</h4>
<ul>
<li>Each block is one file. Its area is proportional to the file's size and its colour comes from its extension.</li>
<li>Folders are framed. When there is room they have a <code>name/ (size)</code> header.</li>
<li><b>Hover</b> shows the name, size and path. <b>Click</b> selects the item in the Tree View.</li>
<li><b>Double-click</b> zooms into the folder under the cursor.</li>
<li><b>Backspace</b>, <b>Alt+Up</b> or the <b>mouse back button</b> zooms out.</li>
<li><b>Right-click</b> opens the context menu.</li>
</ul>

<h4>Tree View</h4>
<ul>
<li>Folders are sorted by size. Click a column header to sort by another column.</li>
<li><b>% of Parent</b> shows how much of its parent folder an item takes.</li>
<li><b>Size</b> is the apparent size. <b>Allocated</b> is the space actually used on disk (sparse files, block rounding).</li>
<li>Each folder's files are grouped under a <code>[N Files in &hellip;]</code> row.</li>
<li>Gray rows are mount points of other filesystems, virtual filesystems, unreadable folders, or extra hard links.</li>
<li>Double-click a file to open it with its default application.</li>
</ul>

<h4>Extension list</h4>
<p>Shows the total size per file extension. The largest extensions get distinct colours and the rest are gray.
Select one or more rows (Ctrl/Shift+click) to highlight only those files on the treemap.</p>

<h4>File View</h4>
<p>Lists every file, largest first. Type a filter and press <b>Enter</b>:
a plain word matches file names, a wildcard such as <code>*.mp4</code> or <code>backup-??.tar</code> is supported,
and a filter containing <code>/</code> is matched against the full path.</p>

<h4>Context menu</h4>
<p>Open, Explore Folder, Terminal Here, Scan This Folder, Copy Path, Copy File and Size Info, Export to CSV,
Zoom in/out Treemap, treemap options, <b>Move to Trash</b> and <b>Delete Permanently</b>.
Both delete commands ask for confirmation, and the totals update without rescanning.</p>

<h4>Options menu</h4>
<ul>
<li><b>Show Treemap</b> (F9): hide it to get more room for the lists.</li>
<li><b>Show Allocated Size on Treemap</b>: size blocks by disk usage instead of apparent size.</li>
<li><b>Show Folder and File Names on Treemap</b>.</li>
<li><b>Stay on One File System</b>: don't descend into other mounts (like <code>du -x</code>). On by default.</li>
<li><b>Count Hard-Linked Files Once</b>: extra links show as 0 bytes, so totals are correct. On by default.</li>
</ul>

<h4>Keyboard shortcuts</h4>
<table cellspacing="0" cellpadding="3">
<tr><td><b>Ctrl+O</b></td><td>Scan a folder</td></tr>
<tr><td><b>F5</b></td><td>Rescan</td></tr>
<tr><td><b>F9</b></td><td>Show / hide treemap</td></tr>
<tr><td><b>Backspace</b></td><td>Zoom out treemap (when it has focus)</td></tr>
<tr><td><b>Ctrl+Q</b></td><td>Quit</td></tr>
</table>

<h3><a name="license"></a>License</h3>
<p>Copyright &copy; 2026 David Eichendorf and FileClump contributors.</p>
<p>This program is free software; you can redistribute it and/or modify it under the terms of the
<b>GNU General Public License version 2</b> as published by the Free Software Foundation
(<b>GPL-2.0-only</b>, with no &ldquo;or any later version&rdquo; option).</p>
<p>This program is distributed in the hope that it will be useful, but <b>WITHOUT ANY WARRANTY</b>; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the full license text below.</p>
)") + QStringLiteral("<pre style=\"font-size:small\">") + license + QStringLiteral("</pre>");
}

} // namespace

MainWindow::MainWindow()
{
    setWindowTitle(QString::fromLatin1(kAppName));
    setWindowIcon(QIcon(makeLogo(64)));

    m_treeModel = new TreeModel(this);
    m_treeProxy = new TreeProxy(this);
    m_treeProxy->setSourceModel(m_treeModel);
    m_extModel = new ExtModel(this);
    m_extProxy = new QSortFilterProxyModel(this);
    m_extProxy->setSourceModel(m_extModel);
    m_extProxy->setSortRole(Roles::Sort);
    m_fileModel = new FileListModel(this);

    auto* central = new QWidget;
    auto* v = new QVBoxLayout(central);
    v->setContentsMargins(4, 4, 4, 0);
    v->setSpacing(4);
    v->addWidget(buildTopPanel());

    // --- Tree View tab: folder tree + extension list
    m_tree = new QTreeView;
    m_tree->setModel(m_treeProxy);
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(TreeModel::ColSize, Qt::DescendingOrder);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setItemDelegateForColumn(TreeModel::ColPct, new PercentDelegate(m_tree));
    m_tree->setAllColumnsShowFocus(true);
    {
        QHeaderView* h = m_tree->header();
        h->setStretchLastSection(false);
        const int widths[] = {300, 95, 85, 85, 80, 80, 70, 140};
        for (int i = 0; i < TreeModel::ColCount; ++i)
            h->resizeSection(i, widths[i]);
    }

    m_extView = new QTableView;
    m_extView->setModel(m_extProxy);
    m_extView->setSortingEnabled(true);
    m_extView->sortByColumn(ExtModel::ColSize, Qt::DescendingOrder);
    m_extView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_extView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_extView->verticalHeader()->hide();
    m_extView->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 4);
    m_extView->setShowGrid(false);
    m_extView->setWordWrap(false);
    m_extView->setItemDelegateForColumn(ExtModel::ColPct, new PercentDelegate(m_extView));
    {
        const int widths[] = {90, 150, 80, 80, 80, 70};
        for (int i = 0; i < ExtModel::ColCount; ++i)
            m_extView->horizontalHeader()->resizeSection(i, widths[i]);
    }

    auto* treeSplit = new QSplitter(Qt::Horizontal);
    treeSplit->setObjectName(QStringLiteral("treeSplit"));
    treeSplit->addWidget(m_tree);
    treeSplit->addWidget(m_extView);
    treeSplit->setStretchFactor(0, 3);
    treeSplit->setStretchFactor(1, 2);

    // --- File View tab
    auto* fileTab = new QWidget;
    auto* fv = new QVBoxLayout(fileTab);
    fv->setContentsMargins(0, 4, 0, 0);
    auto* fbar = new QHBoxLayout;
    fbar->addWidget(new QLabel(QStringLiteral("Filter:")));
    m_fileFilter = new QLineEdit;
    m_fileFilter->setPlaceholderText(QStringLiteral("Name contains… or wildcard like *.mp4  (include / to match the full path)"));
    m_fileFilter->setClearButtonEnabled(true);
    fbar->addWidget(m_fileFilter);
    fv->addLayout(fbar);
    m_fileView = new QTableView;
    m_fileView->setModel(m_fileModel);
    m_fileView->setSortingEnabled(true);
    m_fileView->horizontalHeader()->setSortIndicator(FileListModel::ColSize, Qt::DescendingOrder);
    m_fileView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileView->verticalHeader()->hide();
    m_fileView->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 4);
    m_fileView->setShowGrid(false);
    m_fileView->setWordWrap(false);
    m_fileView->horizontalHeader()->setStretchLastSection(true);
    {
        const int widths[] = {280, 90, 90, 140, 400};
        for (int i = 0; i < FileListModel::ColCount; ++i)
            m_fileView->horizontalHeader()->resizeSection(i, widths[i]);
    }
    fv->addWidget(m_fileView);

    m_tabs = new QTabWidget;
    m_tabs->addTab(treeSplit, QStringLiteral("Tree View"));
    m_tabs->addTab(fileTab, QStringLiteral("File View"));

    // --- Treemap
    m_mapBox = new QWidget;
    auto* mv = new QVBoxLayout(m_mapBox);
    mv->setContentsMargins(0, 0, 0, 0);
    mv->setSpacing(2);
    m_mapPath = new QLabel;
    m_mapPath->setAlignment(Qt::AlignCenter);
    m_map = new Treemap;
    mv->addWidget(m_mapPath);
    mv->addWidget(m_map, 1);

    auto* mainSplit = new QSplitter(Qt::Vertical);
    mainSplit->setObjectName(QStringLiteral("mainSplit"));
    mainSplit->addWidget(m_tabs);
    mainSplit->addWidget(m_mapBox);
    mainSplit->setStretchFactor(0, 1);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setChildrenCollapsible(false);
    v->addWidget(mainSplit, 1);
    setCentralWidget(central);

    // --- Status bar
    m_statusSel = new QLabel;
    m_statusPath = new QLabel;
    m_statusSel->setMinimumWidth(300);
    statusBar()->addWidget(m_statusSel);
    statusBar()->addWidget(m_statusPath, 1);

    buildMenus();

    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(100);
    connect(m_progressTimer, &QTimer::timeout, this, &MainWindow::onProgressTick);

    // --- Wiring
    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onTreeSelectionChanged);
    connect(m_extView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onExtSelectionChanged);
    connect(m_fileView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        auto sel = fileSelection();
        m_map->setSelected(sel);
        updateStatus(sel);
    });
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showContextMenu(treeSelection(), m_tree->viewport()->mapToGlobal(pos));
    });
    connect(m_fileView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showContextMenu(fileSelection(), m_fileView->viewport()->mapToGlobal(pos));
    });
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        Node* n = TreeModel::nodeAt(m_treeProxy->mapToSource(idx));
        if (n && n->type == NT_File && m_result)
            QDesktopServices::openUrl(QUrl::fromLocalFile(nodePath(m_result.get(), n)));
    });
    connect(m_fileView, &QTableView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (m_result)
            QDesktopServices::openUrl(QUrl::fromLocalFile(nodePath(m_result.get(), m_fileModel->nodeAt(idx.row()))));
    });
    connect(m_fileFilter, &QLineEdit::returnPressed, this, [this] {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_fileModel->setFilter(m_fileFilter->text());
        QApplication::restoreOverrideCursor();
    });
    connect(m_fileFilter, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (t.isEmpty())
            m_fileModel->setFilter(QString());
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        if (i == 1 && m_result) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            m_fileModel->ensureBuilt();
            QApplication::restoreOverrideCursor();
        }
    });
    connect(m_map, &Treemap::nodeClicked, this, &MainWindow::selectInTree);
    connect(m_map, &Treemap::contextMenuRequested, this, [this](Node* n, const QPoint& gp) { showContextMenu({n}, gp); });
    connect(m_map, &Treemap::rootChanged, this, [this](Node* n) {
        m_mapPath->setText(n && m_result ? QStringLiteral("[%1]").arg(nodePath(m_result.get(), n)) : QString());
    });
    connect(m_map, &Treemap::hoverChanged, this, [this](Node* n) {
        if (n && m_result)
            m_statusPath->setText(nodePath(m_result.get(), n) + QStringLiteral("  (") + fmtSize(n->size) + QLatin1Char(')'));
        else
            updateStatus(m_tabs->currentIndex() == 1 ? fileSelection() : treeSelection());
    });

    populateDrives();
    loadSettings();
    updateStatus({});
}

MainWindow::~MainWindow()
{
    if (m_scanner)
        m_scanner->cancel = true;
    if (m_thread.joinable())
        m_thread.join();
}

QWidget* MainWindow::buildTopPanel()
{
    auto* top = new QWidget;
    auto* h = new QHBoxLayout(top);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);

    // Select / Scan / progress
    auto* left = panel();
    auto* g = new QGridLayout(left);
    g->setContentsMargins(6, 4, 6, 4);
    g->setVerticalSpacing(3);
    g->addWidget(new QLabel(QStringLiteral("Select:")), 0, 0);
    m_drive = new QComboBox;
    m_drive->setMinimumWidth(240);
    m_drive->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    g->addWidget(m_drive, 0, 1);
    m_scanBtn = new QPushButton(QStringLiteral("Scan"));
    m_scanBtn->setMinimumWidth(80);
    g->addWidget(m_scanBtn, 0, 2);
    m_scanStatus = new QLabel(QStringLiteral("Ready"));
    g->addWidget(m_scanStatus, 1, 0, 1, 2);
    auto* filterBtn = new QToolButton;
    filterBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-filter"), QIcon::fromTheme(QStringLiteral("edit-find"))));
    filterBtn->setToolTip(QStringLiteral("Filter files (File View)"));
    filterBtn->setAutoRaise(true);
    g->addWidget(filterBtn, 1, 2, Qt::AlignRight);
    m_progress = new QProgressBar;
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(14);
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setStyleSheet(QStringLiteral(
        "QProgressBar{border:1px solid #9a9a9a;background:#e6e6e6;border-radius:0}"
        "QProgressBar::chunk{background:#06b025}"));
    g->addWidget(m_progress, 2, 0, 1, 3);
    g->setColumnStretch(1, 1);
    h->addWidget(left, 5);

    // Selection / space info
    auto* mid = panel();
    auto* mg = new QGridLayout(mid);
    mg->setContentsMargins(8, 4, 8, 4);
    mg->setVerticalSpacing(1);
    const char* labels[] = {"Selection:", "Total Space:", "Space Used:", "Space Free:"};
    QLabel** values[] = {&m_selValue, &m_totalValue, &m_usedValue, &m_freeValue};
    for (int i = 0; i < 4; ++i) {
        mg->addWidget(new QLabel(QString::fromLatin1(labels[i])), i, 0);
        auto* val = new QLabel(QStringLiteral("-"));
        QFont f = val->font();
        f.setBold(true);
        val->setFont(f);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        val->setMinimumWidth(170);
        mg->addWidget(val, i, 1);
        *values[i] = val;
    }
    h->addWidget(mid, 3);

    // Branding
    auto* right = panel();
    auto* rh = new QHBoxLayout(right);
    rh->setContentsMargins(8, 4, 8, 4);
    auto* logo = new QLabel;
    logo->setPixmap(makeLogo(56));
    rh->addWidget(logo);
    auto* rv = new QVBoxLayout;
    rv->setSpacing(0);
    auto* title = new QLabel(QStringLiteral("%1 v%2").arg(QString::fromLatin1(kAppName), QString::fromLatin1(kVersion)));
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.35);
    title->setFont(tf);
    rv->addWidget(title);
    rv->addWidget(new QLabel(QStringLiteral("Native Linux disk space analyzer")));
    rv->addWidget(new QLabel(QStringLiteral("C++ / Qt %1 · %2 scan threads")
                                 .arg(QString::fromLatin1(qVersion()))
                                 .arg(std::clamp(std::thread::hardware_concurrency() * 2, 4u, 32u))));
    rv->addStretch();
    rh->addLayout(rv);
    rh->addStretch();
    h->addWidget(right, 4);

    connect(m_scanBtn, &QPushButton::clicked, this, &MainWindow::onScanClicked);
    connect(m_drive, &QComboBox::activated, this, [this](int i) {
        if (m_drive->itemData(i).toString() == kBrowse)
            currentTarget();
    });
    connect(filterBtn, &QToolButton::clicked, this, [this] {
        m_tabs->setCurrentIndex(1);
        m_fileFilter->setFocus();
        m_fileFilter->selectAll();
    });
    return top;
}

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(QStringLiteral("&File"));
    QAction* a = file->addAction(QIcon::fromTheme(QStringLiteral("folder-open")), QStringLiteral("Scan &Folder…"), this, [this] {
        QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Select folder to scan"), m_scanPath.isEmpty() ? QDir::homePath() : m_scanPath);
        if (!d.isEmpty()) {
            m_drive->insertItem(m_drive->count() - 1, QStringLiteral("[%1]").arg(d), d);
            m_drive->setCurrentIndex(m_drive->count() - 2);
            startScan(d);
        }
    });
    a->setShortcut(QKeySequence::Open);
    a = file->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), QStringLiteral("&Rescan"), this, [this] {
        if (!m_scanPath.isEmpty())
            startScan(m_scanPath);
    });
    a->setShortcut(QKeySequence::Refresh);
    file->addAction(QIcon::fromTheme(QStringLiteral("document-export")), QStringLiteral("&Export to CSV…"), this, [this] {
        if (m_result)
            exportCsv(m_result->root);
    });
    file->addSeparator();
    a = file->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), QStringLiteral("E&xit"), this, &QWidget::close);
    a->setShortcut(QKeySequence::Quit);

    QMenu* opt = menuBar()->addMenu(QStringLiteral("&Options"));
    m_actShowMap = opt->addAction(QStringLiteral("Show Treemap"));
    m_actShowMap->setCheckable(true);
    m_actShowMap->setChecked(true);
    m_actShowMap->setShortcut(QKeySequence(Qt::Key_F9));
    connect(m_actShowMap, &QAction::toggled, m_mapBox, &QWidget::setVisible);
    m_actAlloc = opt->addAction(QStringLiteral("Show Allocated Size on Treemap"));
    m_actAlloc->setCheckable(true);
    connect(m_actAlloc, &QAction::toggled, m_map, &Treemap::setUseAlloc);
    m_actNames = opt->addAction(QStringLiteral("Show Folder and File Names on Treemap"));
    m_actNames->setCheckable(true);
    m_actNames->setChecked(true);
    connect(m_actNames, &QAction::toggled, m_map, &Treemap::setShowNames);
    opt->addSeparator();
    m_actOneFs = opt->addAction(QStringLiteral("Stay on One File System (don't cross mount points)"));
    m_actOneFs->setCheckable(true);
    m_actOneFs->setChecked(true);
    m_actHardlinks = opt->addAction(QStringLiteral("Count Hard-Linked Files Once"));
    m_actHardlinks->setCheckable(true);
    m_actHardlinks->setChecked(true);
    opt->addSeparator();
    opt->addAction(QStringLiteral("Refresh Drive List"), this, &MainWindow::populateDrives);

    QMenu* help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QIcon::fromTheme(QStringLiteral("internet-web-browser")), QStringLiteral("Visit &GitHub"), this,
        [] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kRepoUrl))); });
    help->addSeparator();
    a = help->addAction(QIcon::fromTheme(QStringLiteral("help-about")), QStringLiteral("&About FileClump"), this, &MainWindow::showAbout);
    a->setShortcut(QKeySequence::HelpContents);
}

void MainWindow::showAbout()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("About ") + QString::fromLatin1(kAppName));
    auto* v = new QVBoxLayout(&dlg);
    auto* head = new QHBoxLayout;
    auto* logo = new QLabel;
    logo->setPixmap(makeLogo(64));
    head->addWidget(logo, 0, Qt::AlignTop);
    auto* title = new QLabel(QStringLiteral("<span style=\"font-size:x-large\"><b>%1 %2</b></span><br>"
                                            "Native Linux disk space analyzer<br>"
                                            "Free software, GPL-2.0-only &middot; <a href=\"%3\">%3</a>")
                                 .arg(QString::fromLatin1(kAppName), QString::fromLatin1(kVersion), QString::fromLatin1(kRepoUrl)));
    title->setOpenExternalLinks(true);
    head->addWidget(title, 1);
    v->addLayout(head);
    auto* body = new QTextBrowser;
    body->setOpenExternalLinks(true);
    body->setHtml(aboutHtml());
    v->addWidget(body, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(buttons);
    dlg.resize(720, 640);
    dlg.exec();
}

void MainWindow::populateDrives()
{
    const QString prev = m_drive->currentData().toString();
    m_drive->clear();
    QStringList seen;
    auto vols = QStorageInfo::mountedVolumes();
    std::sort(vols.begin(), vols.end(), [](const QStorageInfo& a, const QStorageInfo& b) { return a.rootPath() < b.rootPath(); });
    for (const QStorageInfo& si : vols) {
        if (!si.isValid() || !si.isReady() || skipFs(si.fileSystemType()))
            continue;
        const QString root = si.rootPath();
        if (root.startsWith(QLatin1String("/snap/")) || root.startsWith(QLatin1String("/var/snap/"))
            || root.startsWith(QLatin1String("/run/user/")) || root.startsWith(QLatin1String("/proc"))
            || root.startsWith(QLatin1String("/sys")) || root.startsWith(QLatin1String("/dev"))
            || root.startsWith(QLatin1String("/var/lib/docker")) || seen.contains(root))
            continue;
        seen << root;
        QString label = QStringLiteral("[%1] %2").arg(root, fmtSize(quint64(si.bytesTotal())));
        if (!si.name().isEmpty() && si.name() != root)
            label += QStringLiteral(" — %1").arg(si.name());
        label += QStringLiteral("  (%1)").arg(QString::fromLatin1(si.fileSystemType()));
        m_drive->addItem(IconCache::get().drive, label, root);
    }
    m_drive->addItem(QIcon::fromTheme(QStringLiteral("user-home")), QStringLiteral("[~] Home folder (%1)").arg(QDir::homePath()), QDir::homePath());
    m_drive->addItem(QIcon::fromTheme(QStringLiteral("folder-open")), QStringLiteral("Select folder…"), kBrowse);
    int i = m_drive->findData(prev);
    m_drive->setCurrentIndex(i >= 0 ? i : 0);
}

QString MainWindow::currentTarget()
{
    QString t = m_drive->currentData().toString();
    if (t != kBrowse)
        return t;
    QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Select folder to scan"),
        m_scanPath.isEmpty() ? QDir::homePath() : m_scanPath);
    if (d.isEmpty()) {
        m_drive->setCurrentIndex(0);
        return {};
    }
    int idx = m_drive->findData(d);
    if (idx < 0) {
        m_drive->insertItem(m_drive->count() - 1, QIcon::fromTheme(QStringLiteral("folder")), QStringLiteral("[%1]").arg(d), d);
        idx = m_drive->count() - 2;
    }
    m_drive->setCurrentIndex(idx);
    return d;
}

void MainWindow::onScanClicked()
{
    if (m_thread.joinable()) {
        m_scanner->cancel = true;
        m_scanBtn->setEnabled(false);
        return;
    }
    const QString t = currentTarget();
    if (!t.isEmpty())
        startScan(t);
}

void MainWindow::startScan(const QString& path0)
{
    if (m_thread.joinable())
        return;
    const QString path = QDir(path0).absolutePath();
    if (!QFileInfo(path).isDir()) {
        QMessageBox::warning(this, QString::fromLatin1(kAppName), QStringLiteral("Not a directory: %1").arg(path));
        return;
    }
    m_scanPath = path;
    m_scanner = std::make_unique<Scanner>();
    Scanner::Options opt;
    opt.oneFileSystem = m_actOneFs->isChecked();
    opt.dedupHardlinks = m_actHardlinks->isChecked();

    QStorageInfo si(path);
    m_expectedBytes = 0;
    if (si.isValid() && QDir(si.rootPath()) == QDir(path))
        m_expectedBytes = quint64(si.bytesTotal() - si.bytesAvailable());
    m_progress->setRange(0, m_expectedBytes ? 1000 : 0);
    m_progress->setValue(0);
    m_scanBtn->setText(QStringLiteral("Stop"));
    m_scanStatus->setText(QStringLiteral("Scanning…"));
    updateSpaceInfo(path);
    setWindowTitle(QStringLiteral("[%1] - %2").arg(path, QString::fromLatin1(kAppName)));

    m_elapsed.start();
    m_progressTimer->start();
    Scanner* sc = m_scanner.get();
    const std::string p = QFile::encodeName(path).toStdString();
    m_thread = std::thread([this, sc, p, opt] {
        ScanResult* r = sc->run(p, opt).release();
        QMetaObject::invokeMethod(this, [this, r] { onScanFinished(r); }, Qt::QueuedConnection);
    });
}

void MainWindow::onProgressTick()
{
    if (!m_scanner)
        return;
    const quint64 files = m_scanner->files.load(), bytes = m_scanner->bytes.load();
    m_scanStatus->setText(QStringLiteral("Scanning… %1 files, %2  (%3 s)")
                              .arg(fmtCount(files), fmtSize(bytes), QLocale().toString(m_elapsed.elapsed() / 1000.0, 'f', 1)));
    if (m_expectedBytes)
        m_progress->setValue(int(std::min<quint64>(990, bytes * 1000 / m_expectedBytes)));
}

void MainWindow::onScanFinished(ScanResult* raw)
{
    if (m_thread.joinable())
        m_thread.join();
    m_progressTimer->stop();
    std::unique_ptr<ScanResult> r(raw);

    // Point everything at the new result before the old one is freed.
    IconCache::get().reset(r.get());
    m_map->setResult(r.get());
    m_treeModel->setResult(r.get());
    m_extModel->setResult(r.get());
    m_fileModel->setResult(r.get());
    m_result = std::move(r);
    m_mapPath->setText(QStringLiteral("[%1]").arg(nodePath(m_result.get(), m_map->root())));
    m_extProxy->sort(m_extView->horizontalHeader()->sortIndicatorSection(), m_extView->horizontalHeader()->sortIndicatorOrder());

    const QModelIndex rootIdx = m_treeProxy->index(0, 0);
    m_tree->expand(rootIdx);
    m_tree->selectionModel()->setCurrentIndex(rootIdx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

    QString msg = QStringLiteral("%1 in %2 seconds")
                      .arg(m_result->cancelled ? QStringLiteral("Scan stopped") : QStringLiteral("Scan complete"),
                          QLocale().toString(m_result->seconds, 'f', 2));
    msg += QStringLiteral(" · %1 files").arg(fmtCount(m_result->root->files));
    if (m_result->errors)
        msg += QStringLiteral(" · %1 unreadable").arg(fmtCount(m_result->errors));
    m_scanStatus->setText(msg);
    m_scanStatus->setToolTip(QStringLiteral("%1 folders, %2 files, index uses %3 of RAM")
                                 .arg(fmtCount(m_result->root->folders), fmtCount(m_result->root->files),
                                     fmtSize(m_result->memoryBytes())));
    m_progress->setRange(0, 1000);
    m_progress->setValue(1000);
    m_scanBtn->setText(QStringLiteral("Scan"));
    m_scanBtn->setEnabled(true);
    if (m_tabs->currentIndex() == 1)
        m_fileModel->ensureBuilt();
}

void MainWindow::updateSpaceInfo(const QString& path)
{
    QStorageInfo si(path);
    m_selValue->setText(path);
    m_selValue->setToolTip(path);
    if (!si.isValid()) {
        m_totalValue->setText(QStringLiteral("-"));
        m_usedValue->setText(QStringLiteral("-"));
        m_freeValue->setText(QStringLiteral("-"));
        return;
    }
    const double total = double(si.bytesTotal());
    const quint64 freeB = quint64(si.bytesAvailable());
    const quint64 used = quint64(si.bytesTotal()) - quint64(si.bytesFree());
    auto pct = [total](double v) { return total > 0 ? QLocale().toString(v * 100.0 / total, 'f', 2) + QStringLiteral("%") : QString(); };
    m_totalValue->setText(fmtSize(quint64(si.bytesTotal())));
    m_usedValue->setText(QStringLiteral("%1  (%2)").arg(fmtSize(used), pct(double(used))));
    m_freeValue->setText(QStringLiteral("%1  (%2)").arg(fmtSize(freeB), pct(double(freeB))));
}

void MainWindow::updateStatus(const std::vector<Node*>& nodes)
{
    quint64 files = 0, size = 0;
    for (const Node* n : nodes) {
        files += n->type == NT_File ? 1 : n->files;
        size += n->size;
    }
    m_statusSel->setText(QStringLiteral("Selected Files: %1   Total Size: %2").arg(fmtCount(files), fmtSize(size)));
    m_statusPath->setText(!nodes.empty() && m_result ? nodePath(m_result.get(), nodes.front()) : QString());
}

std::vector<Node*> MainWindow::treeSelection() const
{
    std::vector<Node*> out;
    if (!m_result)
        return out;
    for (const QModelIndex& i : m_tree->selectionModel()->selectedRows(0))
        out.push_back(TreeModel::nodeAt(m_treeProxy->mapToSource(i)));
    return out;
}

std::vector<Node*> MainWindow::fileSelection() const
{
    std::vector<Node*> out;
    if (!m_result)
        return out;
    for (const QModelIndex& i : m_fileView->selectionModel()->selectedRows(0))
        out.push_back(m_fileModel->nodeAt(i.row()));
    return out;
}

void MainWindow::onTreeSelectionChanged()
{
    auto sel = treeSelection();
    if (!m_syncing)
        m_map->setSelected(sel);
    updateStatus(sel);
}

void MainWindow::onExtSelectionChanged()
{
    if (!m_result)
        return;
    std::vector<char> mask;
    const auto rows = m_extView->selectionModel()->selectedRows(0);
    if (!rows.isEmpty()) {
        mask.assign(m_result->exts.size(), 0);
        for (const QModelIndex& i : rows)
            mask[m_extModel->extAt(m_extProxy->mapToSource(i).row())] = 1;
    }
    m_map->setExtHighlight(std::move(mask));
}

void MainWindow::selectInTree(Node* n)
{
    if (!m_result || !n)
        return;
    const QModelIndex px = m_treeProxy->mapFromSource(m_treeModel->indexFor(n));
    if (!px.isValid())
        return;
    m_syncing = true;
    std::vector<QModelIndex> chain;
    for (QModelIndex p = px.parent(); p.isValid(); p = p.parent())
        chain.push_back(p);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        m_tree->expand(*it);
    m_tree->selectionModel()->setCurrentIndex(px, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_tree->scrollTo(px, QAbstractItemView::PositionAtCenter);
    m_syncing = false;
    m_map->setSelected({n});
    if (m_tabs->currentIndex() != 0)
        m_tabs->setCurrentIndex(0);
}

void MainWindow::showContextMenu(std::vector<Node*> nodes, const QPoint& gp)
{
    if (!m_result || nodes.empty())
        return;
    ScanResult* res = m_result.get();
    Node* n = nodes.front();
    const bool isFile = n->type == NT_File;
    const QString path = nodePath(res, n);
    const QString dir = isFile ? nodePath(res, n->parent) : path;
    bool deletable = false;
    for (Node* x : nodes)
        deletable |= x != res->root && x->type != NT_Group;

    QMenu menu(this);
    menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Open"), this,
        [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
    menu.addAction(QIcon::fromTheme(QStringLiteral("folder-open")), QStringLiteral("Explore Folder"), this,
        [dir] { QDesktopServices::openUrl(QUrl::fromLocalFile(dir)); });
    menu.addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), QStringLiteral("Terminal Here"), this,
        [this, dir] { openInTerminal(dir); });
    if (!isFile)
        menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), QStringLiteral("Scan This Folder"), this,
            [this, dir] { startScan(dir); });
    menu.addSeparator();
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Copy Path"), this, [res, nodes] {
        QStringList l;
        for (Node* x : nodes)
            l << nodePath(res, x);
        QApplication::clipboard()->setText(l.join(QLatin1Char('\n')));
    });
    menu.addAction(QStringLiteral("Copy File and Size Info to Clipboard"), this, [res, nodes] {
        QStringList l;
        for (Node* x : nodes)
            l << QStringLiteral("%1\t%2").arg(nodePath(res, x), fmtSize(x->size));
        QApplication::clipboard()->setText(l.join(QLatin1Char('\n')));
    });
    menu.addAction(QIcon::fromTheme(QStringLiteral("document-export")), QStringLiteral("Export to CSV…"), this,
        [this, n, res] { exportCsv(n->type == NT_File ? res->root : n); });
    menu.addSeparator();
    menu.addAction(QIcon::fromTheme(QStringLiteral("zoom-in")), QStringLiteral("Zoom in Treemap"), this, [this, n] { m_map->setRoot(n); });
    QAction* zo = menu.addAction(QIcon::fromTheme(QStringLiteral("zoom-out")), QStringLiteral("Zoom out Treemap"), this, [this] { m_map->zoomOut(); });
    zo->setEnabled(m_map->root() && m_map->root()->parent);
    menu.addSeparator();
    menu.addAction(m_actShowMap);
    menu.addAction(m_actAlloc);
    menu.addAction(m_actNames);
    menu.addSeparator();
    QAction* tr = menu.addAction(QIcon::fromTheme(QStringLiteral("user-trash")), QStringLiteral("Move to Trash"), this,
        [this, nodes] { deleteNodes(nodes, true); });
    QAction* del = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), QStringLiteral("Delete Permanently…"), this,
        [this, nodes] { deleteNodes(nodes, false); });
    del->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    tr->setEnabled(deletable);
    del->setEnabled(deletable);
    menu.exec(gp);
}

void MainWindow::openInTerminal(const QString& dir)
{
    QStringList cands;
    const QString env = qEnvironmentVariable("TERMINAL");
    if (!env.isEmpty())
        cands << env;
    cands << QStringLiteral("konsole") << QStringLiteral("gnome-terminal") << QStringLiteral("kgx")
          << QStringLiteral("xfce4-terminal") << QStringLiteral("tilix") << QStringLiteral("kitty")
          << QStringLiteral("alacritty") << QStringLiteral("wezterm") << QStringLiteral("foot")
          << QStringLiteral("x-terminal-emulator") << QStringLiteral("xterm");
    for (const QString& c : cands) {
        const QString exe = QStandardPaths::findExecutable(c);
        if (exe.isEmpty())
            continue;
        QStringList args;
        if (c == QLatin1String("konsole"))
            args << QStringLiteral("--workdir") << dir;
        else if (c == QLatin1String("gnome-terminal") || c == QLatin1String("xfce4-terminal"))
            args << QStringLiteral("--working-directory=") + dir;
        QProcess::startDetached(exe, args, dir);
        return;
    }
    QMessageBox::warning(this, QString::fromLatin1(kAppName), QStringLiteral("No terminal emulator found. Set $TERMINAL."));
}

void MainWindow::deleteNodes(const std::vector<Node*>& nodes0, bool trash)
{
    if (!m_result)
        return;
    std::vector<Node*> nodes;
    for (Node* n : nodes0)
        if (n != m_result->root && n->type != NT_Group && !ScanResult::isDeleted(n))
            nodes.push_back(n);
    if (nodes.empty())
        return;
    quint64 total = 0;
    for (Node* n : nodes)
        total += n->size;
    const QString what = nodes.size() == 1 ? QStringLiteral("\"%1\"").arg(nodePath(m_result.get(), nodes[0]))
                                           : QStringLiteral("%1 items").arg(nodes.size());
    const QString q = trash ? QStringLiteral("Move %1 (%2) to the Trash?").arg(what, fmtSize(total))
                            : QStringLiteral("Permanently delete %1 (%2)?\n\nThis cannot be undone.").arg(what, fmtSize(total));
    if (QMessageBox::question(this, trash ? QStringLiteral("Move to Trash") : QStringLiteral("Delete Permanently"), q,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;

    QStringList failed;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (Node* n : nodes) {
        if (ScanResult::isDeleted(n)) // already gone with a selected ancestor
            continue;
        const QString p = nodePath(m_result.get(), n);
        bool ok;
        if (trash)
            ok = QFile::moveToTrash(p);
        else if (n->type == NT_File)
            ok = QFile::remove(p);
        else
            ok = QDir(p).removeRecursively();
        if (ok)
            m_result->remove(n);
        else
            failed << p;
    }
    QApplication::restoreOverrideCursor();
    afterRemoval();
    if (!failed.isEmpty())
        QMessageBox::warning(this, QString::fromLatin1(kAppName),
            QStringLiteral("Could not remove:\n%1").arg(failed.mid(0, 20).join(QLatin1Char('\n'))));
}

void MainWindow::afterRemoval()
{
    m_treeProxy->invalidate();
    m_tree->viewport()->update();
    m_extModel->refresh();
    m_fileModel->refresh();
    m_map->setSelected({});
    m_map->invalidate();
    updateSpaceInfo(m_scanPath);
    onTreeSelectionChanged();
}

void MainWindow::exportCsv(Node* from)
{
    if (!m_result || !from)
        return;
    const QString fn = QFileDialog::getSaveFileName(this, QStringLiteral("Export to CSV"),
        QDir::homePath() + QStringLiteral("/disk-usage.csv"), QStringLiteral("CSV files (*.csv)"));
    if (fn.isEmpty())
        return;
    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QString::fromLatin1(kAppName), f.errorString());
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QTextStream ts(&f);
    ts << "File Name,Size,Allocated,Modified,Files,Folders\n";
    ScanResult* res = m_result.get();
    std::vector<Node*> stack {from};
    while (!stack.empty()) {
        Node* n = stack.back();
        stack.pop_back();
        if (n->flags & NF_Deleted)
            continue;
        if (n->type != NT_Group) {
            QString p = nodePath(res, n);
            if (n->type == NT_Dir && !p.endsWith(QLatin1Char('/')))
                p += QLatin1Char('/');
            p.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            ts << '"' << p << "\"," << n->size << ',' << n->alloc << ',' << fmtTime(n->mtime) << ',';
            if (n->type == NT_Dir)
                ts << n->files << ',' << n->folders;
            else
                ts << ',';
            ts << '\n';
        }
        for (uint32_t i = n->nkids; i-- > 0;)
            stack.push_back(&n->kids[i]);
    }
    QApplication::restoreOverrideCursor();
}

void MainWindow::loadSettings()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
    for (auto* sp : findChildren<QSplitter*>()) {
        if (sp->restoreState(s.value(QStringLiteral("splitter/") + sp->objectName()).toByteArray()))
            continue;
        // First run: split evenly once the real window size is known.
        QTimer::singleShot(0, sp, [sp] {
            const int total = sp->orientation() == Qt::Vertical ? sp->height() : sp->width();
            if (sp->orientation() == Qt::Vertical)
                sp->setSizes({total / 2, total - total / 2});
            else
                sp->setSizes({total * 3 / 5, total * 2 / 5});
        });
    }
    m_actShowMap->setChecked(s.value(QStringLiteral("showTreemap"), true).toBool());
    m_actAlloc->setChecked(s.value(QStringLiteral("allocOnTreemap"), false).toBool());
    m_actNames->setChecked(s.value(QStringLiteral("namesOnTreemap"), true).toBool());
    m_actOneFs->setChecked(s.value(QStringLiteral("oneFileSystem"), true).toBool());
    m_actHardlinks->setChecked(s.value(QStringLiteral("hardlinksOnce"), true).toBool());
    if (s.contains(QStringLiteral("treeHeader")))
        m_tree->header()->restoreState(s.value(QStringLiteral("treeHeader")).toByteArray());
    if (!s.contains(QStringLiteral("geometry")))
        resize(1200, 820);
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    for (auto* sp : findChildren<QSplitter*>())
        s.setValue(QStringLiteral("splitter/") + sp->objectName(), sp->saveState());
    s.setValue(QStringLiteral("showTreemap"), m_actShowMap->isChecked());
    s.setValue(QStringLiteral("allocOnTreemap"), m_actAlloc->isChecked());
    s.setValue(QStringLiteral("namesOnTreemap"), m_actNames->isChecked());
    s.setValue(QStringLiteral("oneFileSystem"), m_actOneFs->isChecked());
    s.setValue(QStringLiteral("hardlinksOnce"), m_actHardlinks->isChecked());
    s.setValue(QStringLiteral("treeHeader"), m_tree->header()->saveState());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    saveSettings();
    if (m_scanner)
        m_scanner->cancel = true;
    if (m_thread.joinable())
        m_thread.join();
    QMainWindow::closeEvent(e);
}
