#pragma once
#include <QWidget>
#include <QStringList>

// Windows UI adapter only. The shared core knows nothing about desktop windows.
class SubtitleOverlay final : public QWidget {
public:
    SubtitleOverlay();
    bool showText(const QString& text, const QRect& source, const QRect& workArea);
    static QRect placement(const QRect& source, const QRect& workArea, const QSize& size);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QStringList lines_;
};

class QTimer;
// Briefly outlines the rectangle that was just captured, so the user sees what was read.
// Click-through and shown only after the capture, so it never enters an image.
class CaptureFlash final : public QWidget {
public:
    CaptureFlash();
    void flash(const QRect& global, int durationMs = 600);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QTimer* timer_;
};
