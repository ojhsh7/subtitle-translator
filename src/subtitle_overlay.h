#pragma once
#include <QWidget>
#include <QStringList>
class QPainter;

// Windows UI adapter only. The shared core knows nothing about desktop windows.
class SubtitleOverlay final : public QWidget {
public:
    SubtitleOverlay();
    bool showText(const QString& text, const QRect& source, const QRect& workArea);
    // scale: multiplies the size matched to the original line (0.8-1.5). box: a translucent dark
    // panel behind the text instead of an outline only.
    void setStyle(double scale, bool box) { scale_ = scale; box_ = box; update(); }
    // Caption text as the overlay draws it: white with a dark outline, or with box, white on a
    // translucent dark panel filling `panel`. Shared with the main window's stage.
    static void paintCaption(QPainter& painter, const QRect& textArea, const QRect& panel, const QString& text, int flags, bool box);
    static QRect placement(const QRect& source, const QRect& workArea, const QSize& size);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QStringList lines_;
    double scale_ = 1.0;
    bool box_ = false;
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
