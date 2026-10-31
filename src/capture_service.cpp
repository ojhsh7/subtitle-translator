#include "capture_service.h"
#include <QScreen>
#include <QPixmap>
#include <algorithm>
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
        error = QStringLiteral("선택 영역이 유효하지 않아요. 모니터와 영역을 다시 선택하세요.");
        return {};
    }
    if (qint64(rect.width()) * rect.height() * screen->devicePixelRatio() *
        screen->devicePixelRatio() > 16000000) {
        error = QStringLiteral("영역이 너무 커요. 자막 부분만 작게 선택하세요.");
        return {};
    }
    auto image = screen->grabWindow(0, rect.x(), rect.y(), rect.width(), rect.height()).toImage();
    if (image.isNull()) error = QStringLiteral("화면 캡처에 실패했어요. 게임을 창 모드로 실행해 보세요.");
    return image;
}
