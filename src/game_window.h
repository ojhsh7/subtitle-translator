#pragma once
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
class QScreen;

// Windows adapter: which game (program) owns part of the screen and where its window is now,
// so every game keeps its own subtitle region and the region follows the game window.
// Window positions are physical pixels (Win32); Qt widgets and captures use logical pixels.
namespace GameWindow {
struct Info {
    quintptr handle = 0; // top-level HWND
    QString key;         // settings key: program path, lower case, '/' separators
    QString name;        // program file name for display, e.g. "game.exe"
    QString title;       // window title for display
    QRect client;        // client area, physical pixels, global
    bool isValid() const { return handle != 0 && !key.isEmpty() && !client.isEmpty(); }
};
// The visible top-level window of another program under a logical global point.
// ignoreProcess: our own process id (tests pass 0 to accept their own windows).
Info at(const QPoint& logical, quint32 ignoreProcess);
// The window of a saved game now: the foreground window when it belongs to the game,
// otherwise the hinted or largest visible window. Invalid when not running or minimized.
Info find(const QString& key, quintptr hint = 0);
quint32 currentProcess();

// Coordinate conversion through the monitor that holds the rectangle.
QRect toPhysical(const QRect& logical);
struct Area { QScreen* screen = nullptr; QRect rect; }; // rect: logical, relative to the screen
Area toScreen(const QRect& physical);

// Pure helpers (unit tested).
QString normalizedKey(const QString& path);
QRect relativeTo(const QRect& global, const QRect& client);
// A stored region in the current client area, scaled when the window size changed.
QRect place(const QRect& relative, const QSize& reference, const QRect& client);
}
