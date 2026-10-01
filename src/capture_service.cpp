#include "capture_service.h"
#include "game_window.h"
#include <QOperatingSystemVersion>
#include <QScreen>
#include <QPixmap>
#include <QWidget>
#include <algorithm>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
QRect Capture::aroundPoint(const QRect& screenGeometry, const QPoint& globalPoint, const QSize& requestedSize) {
    if (!screenGeometry.contains(globalPoint) || screenGeometry.width() < 8 || screenGeometry.height() < 8 ||
        requestedSize.width() < 8 || requestedSize.height() < 8) return {};
    const QSize size = requestedSize.boundedTo(screenGeometry.size());
    const QPoint local = globalPoint - screenGeometry.topLeft();
    return QRect(QPoint(std::clamp(local.x() - size.width() / 2, 0, screenGeometry.width() - size.width()),
                        std::clamp(local.y() - size.height() / 2, 0, screenGeometry.height() - size.height())), size);
}
QImage Capture::region(QScreen* screen, const QRect& rect, QString& error) {
    error.clear();
    if (!screen || rect.width() < 8 || rect.height() < 8 ||
        !QRect(QPoint(0, 0), screen->size()).contains(rect)) {
        error = QStringLiteral("선택 영역이 유효하지 않습니다. 모니터와 영역을 다시 선택하세요.");
        return {};
    }
    if (qint64(rect.width()) * rect.height() * screen->devicePixelRatio() *
        screen->devicePixelRatio() > 16000000) {
        error = QStringLiteral("영역이 너무 큽니다. 자막 부분만 작게 선택하세요.");
        return {};
    }
    auto image = screen->grabWindow(0, rect.x(), rect.y(), rect.width(), rect.height()).toImage();
    if (image.isNull()) error = QStringLiteral("화면 캡처에 실패했습니다. 게임을 창 모드로 실행해 보세요.");
    return image;
}
QRect Capture::physicalRect(QScreen* screen, const QRect& logicalLocal) {
    if (!screen || logicalLocal.isEmpty()) return {};
    return GameWindow::toPhysical(logicalLocal.translated(screen->geometry().topLeft()));
}
QImage Capture::physical(const QRect& r, qreal dpr, QString& error) {
    error.clear();
    if (r.width() < 8 || r.height() < 8 || qint64(r.width()) * r.height() > 16000000) {
        error = QStringLiteral("선택 영역이 유효하지 않습니다. 모니터와 영역을 다시 선택하세요.");
        return {};
    }
    HDC screen = GetDC(nullptr);
    if (!screen) { error = QStringLiteral("화면 캡처에 실패했습니다. 게임을 창 모드로 실행해 보세요."); return {}; }
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = r.width();
    info.bmiHeader.biHeight = -r.height(); // top-down rows, like QImage
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = memory ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    QImage image;
    if (bitmap && bits) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        // Same raster operation as Qt's screen grab, so both paths see the same pixels.
        const BOOL copied = BitBlt(memory, 0, 0, r.width(), r.height(), screen, r.x(), r.y(), SRCCOPY | CAPTUREBLT);
        GdiFlush();
        if (copied) {
            image = QImage(r.width(), r.height(), QImage::Format_RGB32);
            const auto* source = static_cast<const quint32*>(bits);
            for (int y = 0; y < r.height(); ++y) {
                auto* row = reinterpret_cast<quint32*>(image.scanLine(y));
                const quint32* in = source + qsizetype(y) * r.width();
                for (int x = 0; x < r.width(); ++x) row[x] = in[x] | 0xff000000u; // GDI leaves alpha at 0
            }
            image.setDevicePixelRatio(dpr);
        }
        SelectObject(memory, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (image.isNull()) error = QStringLiteral("화면 캡처에 실패했습니다. 게임을 창 모드로 실행해 보세요.");
    return image;
}
bool Capture::exclusionSupported() {
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 19041);
}
bool Capture::setExcludedFromCapture(QWidget* window, bool excluded) {
    if (!window || !exclusionSupported()) return false;
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    return SetWindowDisplayAffinity(handle, excluded ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE) != 0;
}
bool Capture::isExcludedFromCapture(QWidget* window) {
    if (!window || !window->internalWinId()) return false;
    DWORD affinity = WDA_NONE;
    return GetWindowDisplayAffinity(reinterpret_cast<HWND>(window->internalWinId()), &affinity) &&
           affinity == WDA_EXCLUDEFROMCAPTURE;
}
