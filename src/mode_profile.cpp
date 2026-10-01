#include "mode_profile.h"
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <algorithm>

namespace Modes {
QString id(Mode mode) {
    switch (mode) {
    case Mode::Game: return QStringLiteral("game");
    case Mode::Video: return QStringLiteral("video");
    case Mode::Text: return QStringLiteral("text");
    case Mode::File: return QStringLiteral("file");
    }
    return QStringLiteral("game");
}
QString label(Mode mode) {
    switch (mode) {
    case Mode::Game: return QStringLiteral("게임");
    case Mode::Video: return QStringLiteral("영상");
    case Mode::Text: return QStringLiteral("글");
    case Mode::File: return QStringLiteral("자막 파일");
    }
    return {};
}
Mode fromId(const QString& value, Mode fallback) {
    for (int i = 0; i < Count; ++i) if (id(Mode(i)) == value) return Mode(i);
    return fallback;
}
QString languageKey(Mode mode, bool target) {
    if (mode == Mode::Game) return target ? QStringLiteral("translation/target") : QStringLiteral("ocr/languages");
    return QStringLiteral("modes/") + id(mode) + (target ? QStringLiteral("/target") : QStringLiteral("/source"));
}
SubtitleOverlay::Style loadStyle(Mode mode, bool exclusionAvailable) {
    SubtitleOverlay::Style style;
    if (mode == Mode::Video) {
        // Video captions replace the original line when our window cannot be read back.
        style.placement = exclusionAvailable ? SubtitleOverlay::Placement::Cover : SubtitleOverlay::Placement::Below;
        style.backgroundOpacity = 85;
    }
    const QSettings settings;
    const QString prefix = QStringLiteral("modes/") + id(mode) + QStringLiteral("/style/");
    style.placement = SubtitleOverlay::Placement(std::clamp(settings.value(prefix + "placement", int(style.placement)).toInt(), 0, 2));
    style.fontScale = std::clamp(settings.value(prefix + "font", style.fontScale).toInt(), 50, 200);
    style.backgroundOpacity = std::clamp(settings.value(prefix + "background", style.backgroundOpacity).toInt(), 0, 100);
    if (style.placement == SubtitleOverlay::Placement::Cover && !exclusionAvailable)
        style.placement = SubtitleOverlay::Placement::Below; // never cover a line we would read back
    return style;
}
void saveStyle(Mode mode, const SubtitleOverlay::Style& style) {
    QSettings settings;
    const QString prefix = QStringLiteral("modes/") + id(mode) + QStringLiteral("/style/");
    settings.setValue(prefix + "placement", int(style.placement));
    settings.setValue(prefix + "font", style.fontScale);
    settings.setValue(prefix + "background", style.backgroundOpacity);
}
}

bool SavedRegion::valid() const {
    return screen && !rect.isEmpty() && screen->geometry() == screenGeometry && qFuzzyCompare(screen->devicePixelRatio(), dpr);
}
QString SavedRegion::describe() const {
    if (!screen) return QStringLiteral("선택한 영역이 없습니다.");
    return QStringLiteral("%1 · 위치 (%2, %3) · %4 × %5 논리 픽셀")
        .arg(screen->name()).arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}
SavedRegion SavedRegion::load(const QString& prefix) {
    SavedRegion region;
    const QSettings settings;
    const QRect rect = settings.value(prefix + "/region").toRect();
    if (rect.isEmpty()) return region;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() != settings.value(prefix + "/screen").toString()) continue;
        if (screen->geometry() != settings.value(prefix + "/screenGeometry").toRect() ||
            !qFuzzyCompare(screen->devicePixelRatio(), settings.value(prefix + "/devicePixelRatio").toReal())) break;
        region.screen = screen;
        region.screenGeometry = screen->geometry();
        region.dpr = screen->devicePixelRatio();
        region.rect = rect;
        region.probe = settings.value(prefix + "/probe").toRect();
        break;
    }
    return region;
}
void SavedRegion::save(const QString& prefix) const {
    QSettings settings;
    settings.setValue(prefix + "/screen", screen ? screen->name() : QString());
    settings.setValue(prefix + "/screenGeometry", screenGeometry);
    settings.setValue(prefix + "/devicePixelRatio", dpr);
    settings.setValue(prefix + "/region", rect);
    settings.setValue(prefix + "/probe", probe);
}
