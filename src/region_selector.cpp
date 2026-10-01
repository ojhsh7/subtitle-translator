#include "region_selector.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
RegionSelector::RegionSelector(const QPixmap& snapshot, const QRect& screenGeometry)
    : snapshot_(snapshot), hint_(QStringLiteral("자막 영역을 드래그하세요 · Esc: 취소 · 한 모니터 안에서 선택")) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setGeometry(screenGeometry);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
}
void RegionSelector::setSuggestion(const QRect& region) {
    selection_ = region.intersected(QRect(QPoint(0, 0), size()));
    hint_ = QStringLiteral("Enter: 이 영역 사용 · 드래그: 다시 그리기 · Esc: 취소");
    update();
}
void RegionSelector::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawPixmap(rect(), snapshot_);
    p.fillRect(rect(), QColor(0, 0, 0, 110));
    if (!selection_.isEmpty()) {
        p.save();
        p.setClipRect(selection_);
        p.drawPixmap(rect(), snapshot_);
        p.restore();
        p.setPen(QPen(QColor("#6ee7b7"), 2));
        p.drawRect(selection_.adjusted(0, 0, -1, -1));
    }
    const int width = std::min(640, this->width() - 32);
    p.fillRect(QRect(16, 16, width, 44), QColor(20, 26, 36, 230));
    p.setPen(Qt::white);
    p.drawText(QRect(28, 16, width - 24, 44), Qt::AlignVCenter, hint_);
}
void RegionSelector::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        start_ = e->position().toPoint();
        dragging_ = true;
        selection_ = {};
    }
}
void RegionSelector::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_) {
        selection_ = QRect(start_, e->position().toPoint()).normalized().intersected(rect());
        update();
    }
}
void RegionSelector::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && dragging_) {
        selection_ = QRect(start_, e->position().toPoint()).normalized().intersected(rect());
        dragging_ = false;
        if (selection_.width() >= 8 && selection_.height() >= 8) accept();
        else { selection_ = {}; update(); }
    }
}
void RegionSelector::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && selection_.width() >= 8 && selection_.height() >= 8) {
        accept();
        return;
    }
    QDialog::keyPressEvent(e); // Esc rejects
}
