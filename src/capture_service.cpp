#include "capture_service.h"
#include <QScreen>
#include <QPixmap>
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
