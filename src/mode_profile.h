#pragma once
#include "subtitle_overlay.h"
#include <QPointer>
#include <QRect>
#include <QString>
class QScreen;

// A mode = input (screen, clipboard, file) + presentation (caption, side-by-side, file) + settings.
enum class Mode { Game = 0, Video = 1, Text = 2, File = 3 };
namespace Modes {
constexpr int Count = 4;
QString id(Mode mode);    // "game", "video", "text", "file"
QString label(Mode mode); // "게임", "영상", "글", "자막 파일"
Mode fromId(const QString& id, Mode fallback = Mode::Game);
// Settings key of a per-mode value. The game mode keeps the keys it has always used.
QString languageKey(Mode mode, bool target);
SubtitleOverlay::Style loadStyle(Mode mode, bool exclusionAvailable);
void saveStyle(Mode mode, const SubtitleOverlay::Style& style);
}

// A screen rectangle remembered for a mode; only valid on the same monitor, resolution and scale.
struct SavedRegion {
    QPointer<QScreen> screen;
    QRect screenGeometry;
    qreal dpr = 1;
    QRect rect;  // logical, relative to the screen
    QRect probe; // physical rectangle of the whole player window (protected-video hint), may be empty
    bool valid() const;
    QRect global() const { return rect.translated(screenGeometry.topLeft()); }
    QString describe() const;
    static SavedRegion load(const QString& prefix); // e.g. "modes/video/capture"
    void save(const QString& prefix) const;
};
