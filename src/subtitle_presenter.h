#pragma once
#include "subtitle_overlay.h"
#include <QObject>
#include <QRect>
#include <QTimer>
class QWidget;

// Shows translated captions on screen for every mode: one overlay window, the style of the active
// mode, capture exclusion (our captions are never read back) and timed hiding.
class SubtitlePresenter final : public QObject {
public:
    explicit SubtitlePresenter(QObject* parent = nullptr);
    ~SubtitlePresenter() override;
    SubtitleOverlay* overlay() const { return overlay_; }
    void setStyle(const SubtitleOverlay::Style& style);
    SubtitleOverlay::Style style() const { return overlay_->style(); }
    // Setting "번역 창을 화면 캡처에서 숨기기". Also applied to every window passed to track().
    void setExcludeFromCapture(bool exclude);
    bool excludeFromCapture() const { return exclude_; }
    // True when exclusion is wanted and Windows applied it, so captions over the source are safe.
    bool exclusionActive() const;
    void track(QWidget* window); // result, history and text windows follow the same setting
    void applyExclusion(QWidget* window) const;
    bool show(const QString& text, const QRect& sourceGlobal, const QRect& workArea);
    void hide();
    void hideAfter(int ms); // unless show() comes first
    bool isVisible() const;
    // Short notice near the bottom of a screen (mode switched, video protected), hidden after `ms`.
    void toast(const QString& text, const QRect& screenArea, int ms = 1800);
private:
    SubtitleOverlay* overlay_;
    QTimer hideTimer_;
    bool exclude_ = true;
    bool toasting_ = false;
    SubtitleOverlay::Style beforeToast_;
    QList<QWidget*> tracked_;
};
