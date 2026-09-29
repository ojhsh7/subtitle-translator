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
