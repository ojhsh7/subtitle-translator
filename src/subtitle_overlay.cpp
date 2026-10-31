#include "subtitle_overlay.h"
#include <QFontMetrics>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>
#include <QTimer>
#include <algorithm>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
constexpr int Padding = 5;
constexpr int Gap = 4;
constexpr int MaxLines = 4;
}

SubtitleOverlay::SubtitleOverlay()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                        Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
    setWindowTitle(QStringLiteral("화면 위 번역 자막"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    QFont captionFont("Malgun Gothic");
    captionFont.setPixelSize(24);
    captionFont.setWeight(QFont::DemiBold);
    setFont(captionFont);
}

QRect SubtitleOverlay::placement(const QRect& source, const QRect& workArea, const QSize& size) {
    const QRect available = workArea.adjusted(4, 4, -4, -4);
    if (source.isEmpty() || !available.isValid() || size.isEmpty() ||
        size.width() > available.width() || size.height() > available.height()) return {};
    // Start where the original line starts, like a second line of the same dialogue.
    const int left = std::clamp(source.left(), available.left(), available.right() - size.width() + 1);
    int top = source.bottom() + 1 + Gap;
    if (top + size.height() - 1 > available.bottom())
        top = source.top() - Gap - size.height();
    const QRect result(QPoint(left, top), size);
    if (!available.contains(result) || result.intersects(source)) return {};
    return result;
}

bool SubtitleOverlay::showText(const QString& text, const QRect& source, const QRect& workArea) {
    if (text.trimmed().isEmpty()) { hide(); return false; }
    // Match the size of the selected original text line.
    QFont captionFont = font();
    captionFont.setPixelSize(std::clamp(int(std::clamp(source.height() * 3 / 5, 16, 36) * scale_), 12, 54));
    setFont(captionFont);
    const int width = std::min(std::clamp(source.width(), 360, 1100), workArea.width() - 8);
    if (width <= Padding * 2) { hide(); return false; }
    const int textWidth = width - Padding * 2;
    QString content = text;
    content.replace('\n', QChar::LineSeparator);
    QTextLayout layout(content, font());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    const QFontMetrics metrics(font());
    lines_.clear();
    layout.beginLayout();
    while (lines_.size() < MaxLines) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(textWidth);
        QString part = content.mid(line.textStart(), line.textLength());
        part.replace(QChar::LineSeparator, QChar(' '));
        if (lines_.size() == MaxLines - 1 && line.textStart() + line.textLength() < content.size()) {
            QString remaining = content.mid(line.textStart());
            remaining.replace(QChar::LineSeparator, QChar(' '));
            part = metrics.elidedText(remaining, Qt::ElideRight, textWidth);
        }
        lines_.append(part.trimmed());
    }
    layout.endLayout();
    const QSize desired(width, int(lines_.size()) * metrics.lineSpacing() + Padding * 2);
    const QRect target = placement(source, workArea, desired);
    if (target.isEmpty()) { hide(); return false; }
    setGeometry(target);
    update();
    show(); // WA_ShowWithoutActivating protects the first show as well.
    const HWND handle = reinterpret_cast<HWND>(winId());
    const LONG_PTR style = GetWindowLongPtrW(handle, GWL_EXSTYLE);
    SetWindowLongPtrW(handle, GWL_EXSTYLE, style | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT);
    SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return true;
}

namespace {
constexpr int FlashBorder = 3;
void makeClickThrough(QWidget* widget) {
    const HWND handle = reinterpret_cast<HWND>(widget->winId());
    const LONG_PTR style = GetWindowLongPtrW(handle, GWL_EXSTYLE);
    SetWindowLongPtrW(handle, GWL_EXSTYLE, style | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT);
    SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
}

CaptureFlash::CaptureFlash()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                        Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus),
      timer_(new QTimer(this)) {
    setWindowTitle(QStringLiteral("캡처 범위 표시"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &QWidget::hide);
}

void CaptureFlash::flash(const QRect& global, int durationMs) {
    if (global.isEmpty()) return;
    // The border sits just outside the captured pixels.
    setGeometry(global.adjusted(-FlashBorder, -FlashBorder, FlashBorder, FlashBorder));
    show();
    makeClickThrough(this);
    update();
    timer_->start(durationMs);
}

void CaptureFlash::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setPen(QPen(QColor(0, 0, 0, 200), 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    painter.setPen(QPen(QColor("#6ee7b7"), FlashBorder - 1));
    painter.drawRect(rect().adjusted(1, 1, -2, -2));
}

void SubtitleOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font());
    const int lineHeight = QFontMetrics(font()).lineSpacing();
    if (box_) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawRoundedRect(rect(), 6, 6);
    }
    for (int i = 0; i < lines_.size(); ++i) {
        const QRect line(Padding, Padding + i * lineHeight, width() - Padding * 2, lineHeight);
        const int flags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine;
        // Outline by default: no background panel unless the user chose one.
        painter.setPen(QColor(0, 0, 0, 220));
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy) painter.drawText(line.translated(dx, dy), flags, lines_[i]);
        painter.setPen(Qt::white);
        painter.drawText(line, flags, lines_[i]);
    }
}
