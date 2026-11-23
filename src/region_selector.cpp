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
    // P-41: a two-line card at the top centre (it sat small in the corner of a large screen), saying
    // that the whole dialogue box may be chosen. It moves to the bottom when the region is up there.
    const QString title = current_.isEmpty() ? QStringLiteral("자막이 나오는 대사창을 드래그하세요")
                                             : QStringLiteral("새로 드래그하거나 방향키로 지금 영역을 옮기세요");
    const QString detail = current_.isEmpty()
        ? QStringLiteral("이름표·테두리까지 넉넉히 골라도 글자 줄만 찾아 읽어요 · 취소 Esc")
        : QStringLiteral("Shift는 10px씩, Ctrl+방향키는 크기 · 확정 Enter · 취소 Esc");
    QFont titleFont = p.font();
    titleFont.setPixelSize(17);
    titleFont.setBold(true);
    QFont detailFont = p.font();
    detailFont.setPixelSize(13);
    const QFontMetrics titleMetrics(titleFont), detailMetrics(detailFont);
    const int width = std::max(titleMetrics.horizontalAdvance(title), detailMetrics.horizontalAdvance(detail)) + 48;
    const int height = titleMetrics.height() + detailMetrics.height() + 30;
    QRect card(0, 0, std::min(width, this->width() - 32), height);
    card.moveTopLeft(QPoint((this->width() - card.width()) / 2, 24));
    if (!shown.isEmpty() && shown.intersects(card.adjusted(0, 0, 0, 24))) card.moveBottom(this->height() - 24);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 26, 36, 235));
    p.drawRoundedRect(card, 10, 10);
    const QRect inside = card.adjusted(24, 12, -24, -12);
    p.setFont(titleFont);
    p.setPen(Qt::white);
    p.drawText(QRect(inside.left(), inside.top(), inside.width(), titleMetrics.height()), Qt::AlignHCenter | Qt::AlignVCenter, title);
    p.setFont(detailFont);
    p.setPen(QColor(205, 212, 224));
    p.drawText(QRect(inside.left(), inside.top() + titleMetrics.height() + 6, inside.width(), detailMetrics.height()),
               Qt::AlignHCenter | Qt::AlignVCenter, detail);
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
