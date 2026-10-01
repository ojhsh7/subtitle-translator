#include "subtitle_presenter.h"
#include "capture_service.h"
#include <QWidget>

SubtitlePresenter::SubtitlePresenter(QObject* parent) : QObject(parent), overlay_(new SubtitleOverlay) {
    hideTimer_.setSingleShot(true);
    connect(&hideTimer_, &QTimer::timeout, this, [this] {
        overlay_->hide();
        if (toasting_) { toasting_ = false; overlay_->setStyle(beforeToast_); }
    });
}
SubtitlePresenter::~SubtitlePresenter() { delete overlay_; }
void SubtitlePresenter::setStyle(const SubtitleOverlay::Style& style) {
    if (toasting_) { beforeToast_ = style; return; }
    overlay_->setStyle(style);
}
void SubtitlePresenter::setExcludeFromCapture(bool exclude) {
    exclude_ = exclude;
    applyExclusion(overlay_);
    for (QWidget* window : tracked_) applyExclusion(window);
}
bool SubtitlePresenter::exclusionActive() const {
    return exclude_ && Capture::exclusionSupported();
}
void SubtitlePresenter::track(QWidget* window) {
    if (!window || tracked_.contains(window)) return;
    tracked_ << window;
    connect(window, &QObject::destroyed, this, [this, window] { tracked_.removeAll(window); });
    applyExclusion(window);
}
void SubtitlePresenter::applyExclusion(QWidget* window) const {
    if (window) Capture::setExcludedFromCapture(window, exclude_);
}
bool SubtitlePresenter::show(const QString& text, const QRect& sourceGlobal, const QRect& workArea) {
    hideTimer_.stop();
    if (toasting_) { toasting_ = false; overlay_->setStyle(beforeToast_); }
    const bool shown = overlay_->showText(text, sourceGlobal, workArea);
    // Re-applied after every show: the native window may have been created just now.
    if (shown) applyExclusion(overlay_);
    return shown;
}
void SubtitlePresenter::hide() {
    hideTimer_.stop();
    overlay_->hide();
    if (toasting_) { toasting_ = false; overlay_->setStyle(beforeToast_); }
}
void SubtitlePresenter::hideAfter(int ms) {
    if (overlay_->isVisible()) hideTimer_.start(ms);
}
bool SubtitlePresenter::isVisible() const { return overlay_->isVisible(); }
void SubtitlePresenter::toast(const QString& text, const QRect& screenArea, int ms) {
    if (!toasting_) beforeToast_ = overlay_->style();
    toasting_ = true;
    overlay_->setStyle({SubtitleOverlay::Placement::Cover, 70, 80});
    const QRect band(screenArea.left() + screenArea.width() / 4, screenArea.bottom() - screenArea.height() / 6,
                     screenArea.width() / 2, 56);
    if (overlay_->showText(text, band, screenArea)) applyExclusion(overlay_);
    hideTimer_.start(ms);
}
