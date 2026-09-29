#pragma once
#include <QImage>
#include <QRect>
#include <QString>
class QScreen;
namespace Capture {
// Return a screen-relative logical rectangle, clamped to the cursor's monitor.
QRect aroundPoint(const QRect& screenGeometry, const QPoint& globalPoint, const QSize& requestedSize);
// Coordinates are logical pixels relative to one screen.
QImage region(QScreen* screen, const QRect& rect, QString& error);
}
