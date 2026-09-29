#pragma once
#include <QImage>
#include <QRect>
#include <QString>
class QScreen;
namespace Capture {
// Coordinates are logical pixels relative to one screen.
QImage region(QScreen* screen, const QRect& rect, QString& error);
}
