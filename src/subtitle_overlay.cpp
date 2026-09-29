#include "subtitle_overlay.h"
#include <QFontMetrics>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>
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
    captionFont.setPixelSize(std::clamp(source.height() * 3 / 5, 16, 36));
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

void SubtitleOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font());
    const int lineHeight = QFontMetrics(font()).lineSpacing();
    for (int i = 0; i < lines_.size(); ++i) {
        const QRect line(Padding, Padding + i * lineHeight, width() - Padding * 2, lineHeight);
        const int flags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine;
        // Outline only: no background panel is painted.
        painter.setPen(QColor(0, 0, 0, 220));
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy) painter.drawText(line.translated(dx, dy), flags, lines_[i]);
        painter.setPen(Qt::white);
        painter.drawText(line, flags, lines_[i]);
    }
}
