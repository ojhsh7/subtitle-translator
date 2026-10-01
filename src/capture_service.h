#pragma once
#include <QImage>
#include <QRect>
#include <QString>
class QScreen;
class QWidget;
namespace Capture {
// Return a screen-relative logical rectangle, clamped to the cursor's monitor.
QRect aroundPoint(const QRect& screenGeometry, const QPoint& globalPoint, const QSize& requestedSize);
// Coordinates are logical pixels relative to one screen. GUI thread only (Qt screen grab).
QImage region(QScreen* screen, const QRect& rect, QString& error);
// Physical (native) pixels of the virtual desktop for a logical rectangle relative to `screen`.
QRect physicalRect(QScreen* screen, const QRect& logicalLocal);
// GDI copy of a physical rectangle: the same BitBlt that QScreen::grabWindow performs, but safe to
// call from a worker thread. The image carries `devicePixelRatio` like a Qt screen grab.
QImage physical(const QRect& physicalGlobal, qreal devicePixelRatio, QString& error);
// Windows 10 2004+ can keep a window visible on the monitor but out of every screen capture
// (WDA_EXCLUDEFROMCAPTURE): our own captions are then never read back, even when they cover
// the source. The same flag also hides them from screenshots, recordings and streams.
bool exclusionSupported();
bool setExcludedFromCapture(QWidget* window, bool excluded);
bool isExcludedFromCapture(QWidget* window);
}
