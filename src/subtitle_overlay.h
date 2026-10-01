#pragma once
#include <QWidget>
#include <QStringList>

// Windows UI adapter only. The shared core knows nothing about desktop windows.
class SubtitleOverlay final : public QWidget {
public:
    // Below: like a second line under the original (game default). Above: over it, outside.
    // Cover: on top of the original line with a solid-enough background (video subtitles).
    enum class Placement { Below = 0, Above = 1, Cover = 2 };
    struct Style {
        Placement placement = Placement::Below;
        int fontScale = 100;        // percent of the automatic size (80, 100, 125, 150)
        int backgroundOpacity = 0;  // 0 = outline only, 100 = opaque panel
    };
    SubtitleOverlay();
    void setStyle(const Style& style) { style_ = style; }
    const Style& style() const { return style_; }
    bool showText(const QString& text, const QRect& source, const QRect& workArea);
    static QRect placement(const QRect& source, const QRect& workArea, const QSize& size);
    static QRect placement(const QRect& source, const QRect& workArea, const QSize& size, Placement placement);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QStringList lines_;
    Style style_;
};
