#pragma once
#include <QCheckBox>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

// Windows 11 look drawn from the system palette, so light and dark mode follow Windows.
// Design notes: docs/plan/2026-11-01-design-pass.ko.md.
namespace Look {
// Segoe Fluent Icons code points; Segoe MDL2 Assets has the same ones on Windows 10.
enum Glyph : char16_t {
    Settings = 0xE713, SelectArea = 0xE8E6, Translate = 0xE8C1, Mouse = 0xE962, More = 0xE712,
    Captions = 0xE7F0, Keyboard = 0xE765, Globe = 0xE774, Scan = 0xEE6F, Camera = 0xE722,
    Info = 0xE946, System = 0xE770, Forward = 0xE72A, Document = 0xE8A5, Edit = 0xE70F,
};
// The stage shows a line the way it looks over a game: a dim scene, white caption, dark outline.
constexpr QRgb Scene = 0xFF1F2228, Caption = 0xFFFFFFFF, Source = 0xFFC9CED6;
QFont iconFont(int pixelSize);
QIcon icon(char16_t glyph, const QPalette& palette, int size = 16);
QColor secondary(const QPalette& palette); // descriptions, shortcuts, status
QColor hairline(const QPalette& palette);  // 1 px edges of rows and buttons
QColor rowFill(const QPalette& palette);   // setting rows, a step off the window colour
}

// Secondary text (12 px, dimmer). Re-tints itself when Windows switches light and dark.
class Note : public QLabel {
public:
    explicit Note(const QString& text = {}, QWidget* parent = nullptr);
protected:
    void changeEvent(QEvent* event) override;
private:
    void tint();
};

// A system icon drawn from the icon font in the current text colour.
class GlyphLabel : public QWidget {
public:
    explicit GlyphLabel(char16_t glyph, int size = 16, QWidget* parent = nullptr);
    QSize sizeHint() const override { return {size_ + 4, size_ + 4}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    char16_t glyph_;
    int size_;
};

// Windows 11 on/off switch. Its text sits at the left, an optional hint (a shortcut) before the
// switch, and "켬"/"끔" when there is no text. A QCheckBox, so checkbox code keeps working; the
// whole row is clickable.
class ToggleSwitch : public QCheckBox {
public:
    explicit ToggleSwitch(const QString& text = {}, QWidget* parent = nullptr);
    void setHint(const QString& hint) { hint_ = hint; updateGeometry(); update(); }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    void paintEvent(QPaintEvent*) override;
    bool hitButton(const QPoint& pos) const override { return rect().contains(pos); }
private:
    QString hint_;
};

// The main switch row: title over a one-line description at the left, the shortcut and a switch at
// the right. text() is "title\nshortcut".
class SwitchRow : public QPushButton {
public:
    explicit SwitchRow(QWidget* parent = nullptr);
    void setDescription(const QString& description) { description_ = description; update(); }
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString description_;
};

// The last line as it appears over the game: the original, dim, above the translation drawn with
// the caption renderer and the user's size and background choices.
class CaptionStage : public QWidget {
public:
    explicit CaptionStage(QWidget* parent = nullptr);
    void setContent(const QString& source, const QString& translation);
    void setStyle(double scale, bool box);
    void setPlaceholder(const QString& text) { placeholder_ = text; update(); }
    QString placeholder() const { return placeholder_; }
    QString translation() const { return translation_; }
    QSize sizeHint() const override { return {420, 118}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString source_, translation_, placeholder_;
    double scale_ = 1.0;
    bool box_ = false;
};

// One settings line, as in Windows Settings: icon, title and description at the left, the control
// at the right.
class SettingRow : public QWidget {
public:
    SettingRow(char16_t glyph, const QString& title, const QString& description, QWidget* control, QWidget* parent = nullptr);
    void setDescription(const QString& text);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Note* description_;
};
