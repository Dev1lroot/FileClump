// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 David Eichendorf and FileClump contributors
#include "Treemap.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace {
constexpr uint32_t kFrame = 0x202020;
constexpr uint32_t kHeaderText = 0xE8E8E8;

// Light from the top-left; normalized.
constexpr double kLx = -0.50, kLy = -0.60, kLz = 0.625;
constexpr double kBulge = 1.05; // cushion curvature
constexpr double kAmbient = 0.28, kDiffuse = 0.82;

inline uint32_t shade(uint32_t rgb, double k)
{
    int r = std::min(255, int(((rgb >> 16) & 0xFF) * k));
    int g = std::min(255, int(((rgb >> 8) & 0xFF) * k));
    int b = std::min(255, int((rgb & 0xFF) * k));
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}
} // namespace

Treemap::Treemap(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumHeight(60);
}

void Treemap::setResult(ScanResult* r)
{
    m_res = r;
    m_root = r ? r->root : nullptr;
    m_sel.clear();
    m_extMask.clear();
    m_hoverIdx = -1;
    invalidate();
    emit rootChanged(m_root);
}

void Treemap::setRoot(Node* n)
{
    while (n && n->type != NT_Dir && n->parent) // files and "[N Files]" groups zoom to their folder
        n = n->parent;
    if (!n || n == m_root)
        return;
    m_root = n;
    invalidate();
    emit rootChanged(m_root);
}

void Treemap::zoomOut()
{
    if (!m_root || !m_root->parent)
        return;
    Node* p = m_root->parent;
    if (p->type == NT_Group && p->parent)
        p = p->parent;
    setRoot(p);
}

void Treemap::setSelected(const std::vector<Node*>& sel)
{
    m_sel = sel;
    updateSelectionRects();
    update();
}

void Treemap::setExtHighlight(std::vector<char> mask)
{
    m_extMask = std::move(mask);
    invalidate();
}

void Treemap::setUseAlloc(bool on)
{
    m_useAlloc = on;
    invalidate();
}

void Treemap::setShowNames(bool on)
{
    m_showNames = on;
    invalidate();
}

void Treemap::invalidate()
{
    if (m_root && ScanResult::isDeleted(m_root) && m_res)
        m_root = m_res->root;
    m_dirty = true;
    m_hoverIdx = -1;
    update();
}

double Treemap::value(const Node* n) const
{
    if (n->flags & NF_Deleted)
        return 0;
    return double(m_useAlloc ? n->alloc : n->size);
}

uint32_t Treemap::colorFor(const Node* f) const
{
    uint32_t c = m_res->exts[f->ext].color;
    if (!m_extMask.empty() && !m_extMask[f->ext])
        return 0x2C2C2C;
    return c;
}

void Treemap::fill(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    const int stride = m_img.bytesPerLine() / 4;
    uint32_t* bits = reinterpret_cast<uint32_t*>(m_img.bits());
    rgb |= 0xFF000000u;
    for (int y = y0; y < y1; ++y)
        std::fill(bits + y * stride + x0, bits + y * stride + x1, rgb);
}

// Per-rectangle "cushion": a parabolic bump lit from the top-left, which gives
// each file the gradient look and makes neighbouring blocks distinguishable.
void Treemap::drawCushion(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    const int w = x1 - x0, h = y1 - y0;
    if (w < 3 || h < 3) {
        fill(x0, y0, x1, y1, shade(rgb, 0.85));
        return;
    }
    const int stride = m_img.bytesPerLine() / 4;
    uint32_t* bits = reinterpret_cast<uint32_t*>(m_img.bits());
    if (int(m_colTerm.size()) < w)
        m_colTerm.resize(w);
    for (int i = 0; i < w; ++i)
        m_colTerm[i] = kBulge * ((i + 0.5) / w * 2.0 - 1.0);
    for (int y = 0; y < h; ++y) {
        const double ay = kBulge * ((y + 0.5) / h * 2.0 - 1.0);
        const double ay2 = ay * ay + 1.0;
        const double yl = kLz - ay * kLy; // constant part of the dot product
        uint32_t* row = bits + (y0 + y) * stride + x0;
        for (int x = 0; x < w; ++x) {
            const double ax = m_colTerm[x];
            const double d = (yl - ax * kLx) / std::sqrt(ax * ax + ay2);
            row[x] = shade(rgb, kAmbient + kDiffuse * std::max(0.0, d));
        }
    }
}

void Treemap::squarify(Item* it, size_t n, double total, double x, double y, double w, double h)
{
    while (n > 0) {
        if (w <= 0 || h <= 0 || total <= 0)
            return;
        // Remaining strip rounds to nothing: nothing left can become visible.
        if (std::lround(x) == std::lround(x + w) || std::lround(y) == std::lround(y + h))
            return;
        const double scale = (w * h) / total;
        const bool vertical = w >= h; // lay the row as a column on the left
        const double side = vertical ? h : w;
        const double side2 = side * side;
        const double amax = it[0].v * scale;

        size_t cnt = 0;
        double sum = 0, prevWorst = 1e300;
        while (cnt < n) {
            const double s = sum + it[cnt].v;
            const double S = s * scale;
            const double amin = it[cnt].v * scale;
            const double worst = std::max(side2 * amax / (S * S), (S * S) / (side2 * amin));
            if (cnt > 0 && worst > prevWorst)
                break;
            prevWorst = worst;
            sum = s;
            ++cnt;
        }

        const double thick = sum * scale / side;
        double pos = vertical ? y : x;
        for (size_t i = 0; i < cnt; ++i) {
            const double len = it[i].v * scale / thick;
            if (vertical)
                layout(it[i].node, x, pos, thick, len);
            else
                layout(it[i].node, pos, y, len, thick);
            pos += len;
        }
        if (vertical) {
            x += thick;
            w -= thick;
        } else {
            y += thick;
            h -= thick;
        }
        total -= sum;
        it += cnt;
        n -= cnt;
    }
}

void Treemap::layout(Node* n, double x, double y, double w, double h)
{
    const int x0 = int(std::lround(x)), y0 = int(std::lround(y));
    const int x1 = int(std::lround(x + w)), y1 = int(std::lround(y + h));
    if (x1 <= x0 || y1 <= y0)
        return;
    const size_t idx = m_rects.size();
    m_rects.push_back({x0, y0, x1, y1, n, 0});

    if (n->type == NT_File) {
        drawCushion(x0, y0, x1, y1, colorFor(n));
        if (m_showNames && (x1 - x0) >= 70 * m_dpr && (y1 - y0) >= 30 * m_dpr)
            m_labels.push_back({QRect(x0 + 3, y0 + 1, x1 - x0 - 6, m_headerH),
                QStringLiteral("%1 (%2)").arg(nodeName(n), fmtSize(m_useAlloc ? n->alloc : n->size)), false});
        m_rects[idx].end = uint32_t(m_rects.size());
        return;
    }

    const int pw = x1 - x0, ph = y1 - y0;
    const int minSide = int(4 * m_dpr);
    if (pw < minSide || ph < minSide) {
        // Too small to show structure: paint it in the colour of its largest file.
        const Node* d = n;
        while (d && d->type != NT_File) {
            const Node* best = nullptr;
            for (uint32_t i = 0; i < d->nkids && !best; ++i)
                if (value(&d->kids[i]) > 0)
                    best = &d->kids[i];
            d = best;
        }
        drawCushion(x0, y0, x1, y1, d ? colorFor(d) : kFrame);
        m_rects[idx].end = uint32_t(m_rects.size());
        return;
    }

    double cx = x, cy = y, cw = w, ch = h;
    if (n->type == NT_Dir) {
        fill(x0, y0, x1, y1, kFrame);
        const bool header = m_showNames && pw >= 40 * m_dpr && ph >= m_headerH * 2 + 6;
        const int hh = header ? m_headerH : 0;
        if (header) {
            QString name = (n == m_res->root) ? QFile::decodeName(m_res->rootPath.c_str()) : nodeName(n) + QLatin1Char('/');
            m_labels.push_back({QRect(x0 + 3, y0 + 1, pw - 6, hh),
                QStringLiteral("%1 (%2)").arg(name, fmtSize(m_useAlloc ? n->alloc : n->size)), true});
        }
        cx = x + 1;
        cy = y + 1 + hh;
        cw = w - 2;
        ch = h - 2 - hh;
    }

    std::vector<Item> items;
    items.reserve(n->nkids);
    double total = 0;
    for (uint32_t i = 0; i < n->nkids; ++i) {
        const double v = value(&n->kids[i]);
        if (v > 0) {
            items.push_back({v, &n->kids[i]});
            total += v;
        }
    }
    if (m_useAlloc) // children are pre-sorted by apparent size only
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.v > b.v; });
    if (!items.empty() && cw > 0 && ch > 0)
        squarify(items.data(), items.size(), total, cx, cy, cw, ch);
    m_rects[idx].end = uint32_t(m_rects.size());
}

void Treemap::render()
{
    m_dirty = false;
    m_dpr = devicePixelRatioF();
    const QSize ps(std::max(1, int(width() * m_dpr)), std::max(1, int(height() * m_dpr)));
    if (m_img.size() != ps)
        m_img = QImage(ps, QImage::Format_RGB32);
    m_img.setDevicePixelRatio(1.0);
    m_img.fill(QColor(0, 0, 0));
    m_rects.clear();
    m_labels.clear();
    m_hoverIdx = -1;
    m_headerH = int(std::ceil(14 * m_dpr));

    if (m_res && m_root && value(m_root) > 0) {
        const double b = 2 * m_dpr;
        layout(m_root, b, b, ps.width() - 2 * b, ps.height() - 2 * b);
    }

    if (!m_labels.empty()) {
        QPainter p(&m_img);
        QFont f = font();
        f.setPixelSize(std::max(8, int(11 * m_dpr)));
        p.setFont(f);
        QFontMetrics fm(f);
        for (const Label& l : m_labels) {
            const QString t = fm.elidedText(l.text, Qt::ElideRight, l.rect.width());
            if (!l.header) {
                p.setPen(QColor(0, 0, 0, 160));
                p.drawText(l.rect.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter, t);
            }
            p.setPen(QColor::fromRgb(kHeaderText));
            p.drawText(l.rect, Qt::AlignLeft | Qt::AlignVCenter, t);
        }
    }
    m_img.setDevicePixelRatio(m_dpr);
    updateSelectionRects();
}

int Treemap::rectIndexFor(const Node* n) const
{
    for (const Node* p = n; p; p = p->parent)
        for (size_t i = 0; i < m_rects.size(); ++i)
            if (m_rects[i].node == p)
                return int(i);
    return -1;
}

void Treemap::updateSelectionRects()
{
    m_selIdx.clear();
    if (m_dirty)
        return;
    for (Node* n : m_sel) {
        int i = rectIndexFor(n);
        if (i >= 0)
            m_selIdx.push_back(i);
    }
}

int Treemap::hitTest(const QPoint& pos) const
{
    if (m_rects.empty())
        return -1;
    const int px = int(pos.x() * m_dpr), py = int(pos.y() * m_dpr);
    auto contains = [&](const R& r) { return px >= r.x0 && px < r.x1 && py >= r.y0 && py < r.y1; };
    if (!contains(m_rects[0]))
        return -1;
    uint32_t i = 0;
    for (;;) {
        uint32_t j = i + 1, found = 0;
        while (j < m_rects[i].end) {
            if (contains(m_rects[j])) {
                found = j;
                break;
            }
            j = m_rects[j].end;
        }
        if (!found)
            return int(i);
        i = found;
    }
}

Node* Treemap::nodeAt(const QPoint& pos) const
{
    int i = hitTest(pos);
    return i >= 0 ? m_rects[i].node : nullptr;
}

QRectF Treemap::logicalRect(int idx) const
{
    const R& r = m_rects[idx];
    return QRectF(r.x0 / m_dpr, r.y0 / m_dpr, (r.x1 - r.x0) / m_dpr, (r.y1 - r.y0) / m_dpr);
}

void Treemap::paintEvent(QPaintEvent*)
{
    ensureLayout();
    QPainter p(this);
    p.drawImage(0, 0, m_img);
    if (!m_res) {
        p.setPen(QColor(170, 170, 170));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Select a drive or folder and press Scan"));
        return;
    }
    if (m_hoverIdx >= 0) {
        p.setPen(QPen(QColor(255, 255, 255, 170), 1));
        p.drawRect(logicalRect(m_hoverIdx).adjusted(0.5, 0.5, -0.5, -0.5));
    }
    p.setPen(QPen(Qt::white, 2));
    for (int i : m_selIdx)
        p.drawRect(logicalRect(i).adjusted(1, 1, -1, -1));
}

void Treemap::resizeEvent(QResizeEvent*)
{
    m_dirty = true;
}

void Treemap::ensureLayout()
{
    const QSize ps(std::max(1, int(width() * devicePixelRatioF())), std::max(1, int(height() * devicePixelRatioF())));
    if (m_dirty || m_img.size() != ps)
        render();
}

void Treemap::mouseMoveEvent(QMouseEvent* e)
{
    ensureLayout();
    const int i = hitTest(e->position().toPoint());
    if (i == m_hoverIdx)
        return;
    const QRectF old = m_hoverIdx >= 0 ? logicalRect(m_hoverIdx) : QRectF();
    m_hoverIdx = i;
    if (!old.isNull())
        update(old.toAlignedRect().adjusted(-2, -2, 2, 2));
    Node* n = i >= 0 ? m_rects[i].node : nullptr;
    if (n) {
        update(logicalRect(i).toAlignedRect().adjusted(-2, -2, 2, 2));
        const QString name = n->type == NT_Group ? QStringLiteral("[Files]") : nodeName(n);
        QToolTip::showText(e->globalPosition().toPoint(),
            QStringLiteral("<b>%1</b><br>Size: %2<br>Allocated: %3<br>%4")
                .arg(name.toHtmlEscaped(), fmtSize(n->size), fmtSize(n->alloc), nodePath(m_res, n).toHtmlEscaped()),
            this);
    } else {
        QToolTip::hideText();
    }
    emit hoverChanged(n);
}

void Treemap::leaveEvent(QEvent*)
{
    if (m_hoverIdx >= 0) {
        m_hoverIdx = -1;
        update();
    }
    QToolTip::hideText();
    emit hoverChanged(nullptr);
}

void Treemap::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::BackButton) {
        zoomOut();
        return;
    }
    if (e->button() == Qt::ForwardButton) {
        if (!m_sel.empty())
            setRoot(m_sel.front());
        return;
    }
    ensureLayout();
    Node* n = nodeAt(e->position().toPoint());
    if (!n)
        return;
    if (e->button() == Qt::LeftButton)
        emit nodeClicked(n);
    else if (e->button() == Qt::RightButton) {
        emit nodeClicked(n);
        emit contextMenuRequested(n, e->globalPosition().toPoint());
    }
}

// Drill one level down towards the item under the cursor.
void Treemap::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    ensureLayout();
    Node* n = nodeAt(e->position().toPoint());
    if (!n)
        return;
    while (n->parent && n->parent != m_root)
        n = n->parent;
    if (n->type == NT_Group) // skip the synthetic level
        return;
    if (n != m_root && n->type == NT_Dir)
        setRoot(n);
}

void Treemap::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Backspace || (e->key() == Qt::Key_Up && e->modifiers() & Qt::AltModifier))
        zoomOut();
    else
        QWidget::keyPressEvent(e);
}
