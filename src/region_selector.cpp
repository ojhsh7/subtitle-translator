#include "region_selector.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
namespace {
constexpr int MinSide = 8;
QString sizeText(const QRect& r) { return QStringLiteral("%1 × %2").arg(r.width()).arg(r.height()); }
// A readable label just above a rectangle, or inside its top edge near the screen top.
void drawTag(QPainter& p, const QRect& bounds, const QRect& target, const QString& text, const QColor& color) {
    const QFontMetrics metrics(p.font());
    QRect tag(0, 0, metrics.horizontalAdvance(text) + 16, metrics.height() + 8);
    tag.moveBottomLeft(target.topLeft() - QPoint(0, 4));
    if (tag.top() < bounds.top()) tag.moveTopLeft(target.topLeft() + QPoint(4, 4));
    if (tag.right() > bounds.right()) tag.moveRight(bounds.right());
    p.fillRect(tag, QColor(20, 26, 36, 220));
    p.setPen(color);
    p.drawText(tag, Qt::AlignCenter, text);
}
}
RegionSelector::RegionSelector(const QPixmap& snapshot, const QRect& screenGeometry, const QRect& current)
    : snapshot_(snapshot), current_(current.intersected(QRect(QPoint(0, 0), screenGeometry.size()))) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setGeometry(screenGeometry);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    if (current_.width() < MinSide || current_.height() < MinSide) current_ = {};
}
void RegionSelector::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawPixmap(rect(), snapshot_);
    p.fillRect(rect(), QColor(0, 0, 0, 110));
    const QRect shown = !selection_.isEmpty() ? selection_ : current_;
    if (!shown.isEmpty()) {
        p.save();
        p.setClipRect(shown);
        p.drawPixmap(rect(), snapshot_);
        p.restore();
    }
    if (!selection_.isEmpty()) {
        p.setPen(QPen(QColor("#6ee7b7"), 2));
        p.drawRect(selection_.adjusted(0, 0, -1, -1));
        drawTag(p, rect(), selection_, sizeText(selection_), QColor("#6ee7b7"));
    } else if (!current_.isEmpty()) {
        p.setPen(QPen(QColor("#facc15"), 2, Qt::DashLine));
        p.drawRect(current_.adjusted(0, 0, -1, -1));
        drawTag(p, rect(), current_, QStringLiteral("지금 영역 %1").arg(sizeText(current_)), QColor("#facc15"));
    }
    const QString hint = current_.isEmpty()
        ? QStringLiteral("자막이 나오는 부분을 드래그하세요. 취소는 Esc예요.")
        : QStringLiteral("새로 드래그하거나 방향키로 지금 영역을 옮기세요(Shift는 10px, Ctrl은 크기). 확정은 Enter, 취소는 Esc예요.");
    const QRect box(16, 16, QFontMetrics(p.font()).horizontalAdvance(hint) + 32, 44);
    p.fillRect(box, QColor(20, 26, 36, 230));
    p.setPen(Qt::white);
    p.drawText(box.adjusted(12, 0, -12, 0), Qt::AlignVCenter, hint);
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
        if (selection_.width() >= MinSide && selection_.height() >= MinSide) accept();
        else { selection_ = {}; update(); }
    }
}
void RegionSelector::keyPressEvent(QKeyEvent* e) {
    if (dragging_ || current_.isEmpty()) { QDialog::keyPressEvent(e); return; }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        selection_ = current_;
        accept();
        return;
    }
    QPoint delta;
    switch (e->key()) {
    case Qt::Key_Left: delta = {-1, 0}; break;
    case Qt::Key_Right: delta = {1, 0}; break;
    case Qt::Key_Up: delta = {0, -1}; break;
    case Qt::Key_Down: delta = {0, 1}; break;
    default: QDialog::keyPressEvent(e); return;
    }
    if (e->modifiers() & Qt::ShiftModifier) delta *= 10;
    QRect next = current_;
    if (e->modifiers() & Qt::ControlModifier) {
        next.setWidth(std::max(MinSide, next.width() + delta.x()));
        next.setHeight(std::max(MinSide, next.height() + delta.y()));
        next = next.intersected(rect());
    } else {
        next.translate(delta);
        // Keep the whole region on this screen.
        next.moveLeft(std::clamp(next.left(), 0, width() - next.width()));
        next.moveTop(std::clamp(next.top(), 0, height() - next.height()));
    }
    if (next.width() >= MinSide && next.height() >= MinSide) current_ = next;
    update();
}
