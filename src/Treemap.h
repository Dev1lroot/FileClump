// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 David Eichendorf and FileClump contributors
#pragma once

#include <QImage>
#include <QWidget>

#include <vector>

#include "Scanner.h"

class Treemap : public QWidget {
    Q_OBJECT
public:
    explicit Treemap(QWidget* parent = nullptr);

    void setResult(ScanResult* r);
    void setRoot(Node* n);
    Node* root() const { return m_root; }
    void zoomOut();
    void setSelected(const std::vector<Node*>& sel);
    void setExtHighlight(std::vector<char> mask); // empty = no highlight
    void setUseAlloc(bool on);
    void setShowNames(bool on);
    void invalidate(); // re-layout and re-render on next paint
    Node* nodeAt(const QPoint& pos) const;

signals:
    void nodeClicked(Node* n);
    void contextMenuRequested(Node* n, const QPoint& globalPos);
    void rootChanged(Node* n);
    void hoverChanged(Node* n);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    struct R {
        int x0, y0, x1, y1;
        Node* node;
        uint32_t end; // one past the last rect of this subtree
    };
    struct Item {
        double v;
        Node* node;
    };
    struct Label {
        QRect rect;
        QString text;
        bool header;
    };

    void render();
    void ensureLayout(); // lay out now if size/root/options changed since last paint
    void layout(Node* n, double x, double y, double w, double h);
    void squarify(Item* it, size_t n, double total, double x, double y, double w, double h);
    void drawCushion(int x0, int y0, int x1, int y1, uint32_t rgb);
    void fill(int x0, int y0, int x1, int y1, uint32_t rgb);
    uint32_t colorFor(const Node* file) const;
    double value(const Node* n) const;
    int rectIndexFor(const Node* n) const;
    int hitTest(const QPoint& pos) const;
    QRectF logicalRect(int idx) const;
    void updateSelectionRects();

    ScanResult* m_res = nullptr;
    Node* m_root = nullptr;
    std::vector<Node*> m_sel;
    std::vector<int> m_selIdx;
    int m_hoverIdx = -1;

    QImage m_img;
    std::vector<R> m_rects;
    std::vector<Label> m_labels;
    std::vector<double> m_colTerm;
    std::vector<char> m_extMask;
    bool m_dirty = true;
    bool m_useAlloc = false;
    bool m_showNames = true;
    double m_dpr = 1.0;
    int m_headerH = 14;
};
