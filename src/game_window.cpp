#include "game_window.h"
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QtGui/qscreen_platform.h>
#include <string>
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>

namespace GameWindow {
namespace {
QString processPath(DWORD pid) {
    // Limited information also works for most elevated games.
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    std::wstring buffer(32768, L'\0');
    DWORD size = DWORD(buffer.size());
    QString path;
    if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) path = QString::fromWCharArray(buffer.data(), int(size));
    CloseHandle(process);
    return path;
}
QString className(HWND hwnd) {
    wchar_t name[256]{};
    return QString::fromWCharArray(name, GetClassNameW(hwnd, name, 256));
}
QString keyOf(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    const QString path = processPath(pid);
    // A protected process hides its path; its window class still tells games apart.
    return path.isEmpty() ? QStringLiteral("class:") + className(hwnd) : normalizedKey(path);
}
// A window the user can see and capture: shown, not minimized, not cloaked (other virtual
// desktop, suspended UWP frame), not click-through (overlays), with a client area.
bool visibleTarget(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) return false;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    RECT client{};
    return GetClientRect(hwnd, &client) && client.right > 0 && client.bottom > 0;
}
bool shell(HWND hwnd) {
    const QString name = className(hwnd);
    return name == QLatin1String("Progman") || name == QLatin1String("WorkerW") ||
           name == QLatin1String("Shell_TrayWnd") || name == QLatin1String("Shell_SecondaryTrayWnd");
}
QRect clientRect(HWND hwnd) {
    RECT r{};
    POINT origin{0, 0};
    if (!GetClientRect(hwnd, &r) || !ClientToScreen(hwnd, &origin)) return {};
    return QRect(origin.x, origin.y, r.right - r.left, r.bottom - r.top);
}
Info describe(HWND hwnd) {
    Info info;
    info.handle = quintptr(hwnd);
    info.key = keyOf(hwnd);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    const QString path = processPath(pid);
    info.name = path.isEmpty() ? className(hwnd) : QFileInfo(path).fileName();
    wchar_t title[256]{};
    info.title = QString::fromWCharArray(title, GetWindowTextW(hwnd, title, 256));
    info.client = clientRect(hwnd);
    return info;
}
QScreen* screenOf(HMONITOR monitor) {
    for (QScreen* screen : QGuiApplication::screens())
        if (auto* native = screen->nativeInterface<QNativeInterface::QWindowsScreen>(); native && native->handle() == monitor)
            return screen;
    return nullptr;
}
QPoint monitorOrigin(HMONITOR monitor) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return {};
    return {info.rcMonitor.left, info.rcMonitor.top};
}
}

quint32 currentProcess() { return quint32(GetCurrentProcessId()); }

Info at(const QPoint& logical, quint32 ignoreProcess) {
    const QRect point = toPhysical(QRect(logical, QSize(1, 1)));
    if (point.isEmpty()) return {};
    struct Search { POINT point; DWORD ignore; HWND found; } search{{point.x(), point.y()}, DWORD(ignoreProcess), nullptr};
    // EnumWindows walks top-level windows from the top of the z-order: the first hit is the visible one.
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        auto* s = reinterpret_cast<Search*>(data);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        RECT frame{};
        if ((s->ignore && pid == s->ignore) || !visibleTarget(hwnd) || !GetWindowRect(hwnd, &frame) ||
            !PtInRect(&frame, s->point)) return TRUE;
        s->found = hwnd;
        return FALSE;
    }, LPARAM(&search));
    if (!search.found || shell(search.found)) return {};
    const Info info = describe(search.found);
    return info.isValid() ? info : Info{};
}

Info find(const QString& key, quintptr hint) {
    if (key.isEmpty()) return {};
    auto belongs = [&](HWND hwnd) { return hwnd && IsWindow(hwnd) && visibleTarget(hwnd) && keyOf(hwnd) == key; };
    const HWND foreground = GetAncestor(GetForegroundWindow(), GA_ROOT);
    if (belongs(foreground)) return describe(foreground);
    if (belongs(reinterpret_cast<HWND>(hint))) return describe(reinterpret_cast<HWND>(hint));
    // Not in front and no usable hint: the program's largest visible window (a launcher is usually smaller).
    struct Search { const QString* key; HWND best; qint64 area; } search{&key, nullptr, 0};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        auto* s = reinterpret_cast<Search*>(data);
        RECT client{};
        if (!visibleTarget(hwnd) || shell(hwnd) || !GetClientRect(hwnd, &client) || keyOf(hwnd) != *s->key) return TRUE;
        const qint64 area = qint64(client.right) * client.bottom;
        if (area > s->area) { s->area = area; s->best = hwnd; }
        return TRUE;
    }, LPARAM(&search));
    if (!search.best) return {};
    const Info info = describe(search.best);
    return info.isValid() ? info : Info{};
}

Info foreground(quint32 ignoreProcess) {
    const HWND hwnd = GetAncestor(GetForegroundWindow(), GA_ROOT);
    DWORD pid = 0;
    if (!hwnd || !GetWindowThreadProcessId(hwnd, &pid) || (ignoreProcess && pid == ignoreProcess) ||
        !visibleTarget(hwnd) || shell(hwnd)) return {};
    const Info info = describe(hwnd);
    return info.isValid() ? info : Info{};
}

QRect toPhysical(const QRect& logical) {
    QScreen* screen = QGuiApplication::screenAt(logical.center());
    auto* native = screen ? screen->nativeInterface<QNativeInterface::QWindowsScreen>() : nullptr;
    if (!native) return {};
    const qreal dpr = screen->devicePixelRatio();
    const QPointF topLeft = QPointF(logical.topLeft() - screen->geometry().topLeft()) * dpr + monitorOrigin(native->handle());
    return QRect(topLeft.toPoint(), (QSizeF(logical.size()) * dpr).toSize());
}

Area toScreen(const QRect& physical) {
    const RECT r{physical.left(), physical.top(), physical.left() + physical.width(), physical.top() + physical.height()};
    const HMONITOR monitor = MonitorFromRect(&r, MONITOR_DEFAULTTONULL); // the monitor holding most of it
    QScreen* screen = monitor ? screenOf(monitor) : nullptr;
    if (!screen) return {};
    const qreal dpr = screen->devicePixelRatio();
    const QPointF topLeft = QPointF(physical.topLeft() - monitorOrigin(monitor)) / dpr;
    // Captures must stay inside one screen.
    const QRect rect = QRect(topLeft.toPoint(), (QSizeF(physical.size()) / dpr).toSize())
                           .intersected(QRect(QPoint(0, 0), screen->size()));
    return {screen, rect};
}

QString normalizedKey(const QString& path) { return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower(); }

QRect relativeTo(const QRect& global, const QRect& client) { return global.translated(-client.topLeft()); }

QRect place(const QRect& relative, const QSize& reference, const QRect& client) {
    if (relative.isEmpty() || reference.isEmpty() || client.isEmpty()) return {};
    QRect rect = relative;
    if (reference != client.size()) {
        // The window was resized: games scale their dialogue box with the window.
        const double sx = double(client.width()) / reference.width(), sy = double(client.height()) / reference.height();
        const int left = qRound(relative.x() * sx), top = qRound(relative.y() * sy);
        rect = QRect(left, top, qRound((relative.x() + relative.width()) * sx) - left,
                     qRound((relative.y() + relative.height()) * sy) - top);
    }
    // Only the game's own pixels belong to its region.
    return rect.translated(client.topLeft()).intersected(client);
}
}
