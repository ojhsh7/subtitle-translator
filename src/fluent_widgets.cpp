#include "fluent_widgets.h"
#include "subtitle_overlay.h"
#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace Look {
QFont iconFont(int pixelSize) {
    static const QString family = QFontDatabase::hasFamily(QStringLiteral("Segoe Fluent Icons"))
        ? QStringLiteral("Segoe Fluent Icons") : QStringLiteral("Segoe MDL2 Assets");
    QFont font(family);
    font.setPixelSize(pixelSize);
    return font;
}
QIcon icon(char16_t glyph, const QPalette& palette, int size) {
    QIcon made;
    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Disabled}) {
        QPixmap pixmap(QSize(size, size) * 2);
        pixmap.setDevicePixelRatio(2);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setFont(iconFont(size));
        painter.setPen(palette.color(group, QPalette::ButtonText));
        painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, QString(QChar(glyph)));
        painter.end();
        made.addPixmap(pixmap, group == QPalette::Active ? QIcon::Normal : QIcon::Disabled);
    }
    return made;
}
QColor secondary(const QPalette& palette) {
    QColor c = palette.color(QPalette::WindowText);
    c.setAlphaF(0.62f);
    return c;
}
QColor hairline(const QPalette& palette) {
    QColor c = palette.color(QPalette::WindowText);
    c.setAlphaF(0.12f);
    return c;
}
QColor rowFill(const QPalette& palette) {
    const QColor window = palette.color(QPalette::Window);
    return window.lightnessF() > 0.5 ? window.lighter(103) : window.lighter(118);
}
}

namespace {
QColor withAlpha(QColor c, float alpha) { c.setAlphaF(alpha); return c; }
QFont sized(QFont font, int pixelSize, int weight = QFont::Normal) {
    font.setPixelSize(pixelSize);
    font.setWeight(QFont::Weight(weight));
    return font;
}
// The Windows 11 switch: a 40x20 pill, accent-filled with a light knob when on, outlined when off.
void paintSwitch(QPainter& p, const QRect& area, bool on, bool enabled, bool focused, const QPalette& palette) {
    const QRectF track(area.right() - 40 + 0.5, area.center().y() - 10 + 0.5, 39, 19);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor text = palette.color(QPalette::WindowText);
    const QColor accent = palette.color(QPalette::Accent);
    const float fade = enabled ? 1.0f : 0.4f;
    if (focused) {
        p.setPen(QPen(text, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(track.adjusted(-3, -3, 3, 3), 13, 13);
    }
    if (on) {
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(accent, fade));
        p.drawRoundedRect(track, 9.5, 9.5);
        p.setBrush(withAlpha(palette.color(QPalette::HighlightedText), fade));
        p.drawEllipse(QPointF(track.right() - 10, track.center().y()), 6, 6);
    } else {
        p.setPen(QPen(withAlpha(text, 0.62f * fade), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(track, 9.5, 9.5);
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(text, 0.62f * fade));
        p.drawEllipse(QPointF(track.left() + 10, track.center().y()), 5, 5);
    }
}
}

Note::Note(const QString& text, QWidget* parent) : QLabel(text, parent) {
    setWordWrap(true);
    setFont(sized(font(), 12));
    tint();
}
void Note::changeEvent(QEvent* event) {
    QLabel::changeEvent(event);
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ThemeChange) tint();
}
void Note::tint() {
    QPalette p = palette();
    p.setColor(QPalette::WindowText, Look::secondary(QApplication::palette()));
    setPalette(p);
}

GlyphLabel::GlyphLabel(char16_t glyph, int size, QWidget* parent) : QWidget(parent), glyph_(glyph), size_(size) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}
void GlyphLabel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setFont(Look::iconFont(size_));
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(rect(), Qt::AlignCenter, QString(QChar(glyph_)));
}

ToggleSwitch::ToggleSwitch(const QString& text, QWidget* parent) : QCheckBox(text, parent) {
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}
QSize ToggleSwitch::sizeHint() const {
    const QFontMetrics body(font()), small(sized(font(), 12));
    const QString label = text().isEmpty() ? QStringLiteral("끔") : text();
    int width = body.horizontalAdvance(label) + 12 + 40 + 4;
    if (!hint_.isEmpty()) width += small.horizontalAdvance(hint_) + 12;
    return {width, qMax(32, body.height() + 12)};
}
void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect area = rect().adjusted(0, 0, -2, 0);
    const bool enabled = isEnabled();
    paintSwitch(p, area, isChecked(), enabled, hasFocus() && focusPolicy() != Qt::NoFocus, palette());
    QColor ink = palette().color(QPalette::WindowText);
    if (!enabled) ink.setAlphaF(0.4f);
    int right = area.right() - 40 - 12;
    if (text().isEmpty()) {
        // Windows Settings states the value next to the switch.
        p.setPen(ink);
        p.drawText(QRect(0, 0, right, height()), Qt::AlignRight | Qt::AlignVCenter, isChecked() ? QStringLiteral("켬") : QStringLiteral("끔"));
        return;
    }
    if (!hint_.isEmpty()) {
        p.setFont(sized(font(), 12));
        p.setPen(Look::secondary(palette()));
        const int hintWidth = QFontMetrics(p.font()).horizontalAdvance(hint_);
        p.drawText(QRect(right - hintWidth, 0, hintWidth, height()), Qt::AlignRight | Qt::AlignVCenter, hint_);
        right -= hintWidth + 12;
        p.setFont(font());
    }
    p.setPen(ink);
    p.drawText(QRect(0, 0, right, height()), Qt::AlignLeft | Qt::AlignVCenter, text());
}

SwitchRow::SwitchRow(QWidget* parent) : QPushButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}
QSize SwitchRow::sizeHint() const { return {360, 60}; }
void SwitchRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QColor fill = Look::rowFill(pal);
    if (isDown()) fill = fill.darker(104);
    else if (underMouse()) fill = pal.color(QPalette::Window).lightnessF() > 0.5 ? fill.darker(102) : fill.lighter(108);
    p.setPen(QPen(isChecked() ? pal.color(QPalette::Accent) : Look::hairline(pal), 1));
    p.setBrush(fill);
    p.drawRoundedRect(frame, 4, 4);
    const QStringList parts = text().split(QLatin1Char('\n'));
    const QString title = parts.value(0), shortcut = parts.value(1);
    const QRect inner = rect().adjusted(16, 0, -16, 0);
    paintSwitch(p, inner, isChecked(), isEnabled(), hasFocus(), pal);
    int right = inner.right() - 40 - 14;
    if (!shortcut.isEmpty()) {
        p.setFont(sized(font(), 12));
        p.setPen(Look::secondary(pal));
        const int w = QFontMetrics(p.font()).horizontalAdvance(shortcut);
        p.drawText(QRect(right - w, 0, w, height()), Qt::AlignRight | Qt::AlignVCenter, shortcut);
        right -= w + 14;
    }
    const QFont titleFont = sized(font(), 15, QFont::DemiBold);
    const QFont descriptionFont = sized(font(), 12);
    const int titleHeight = QFontMetrics(titleFont).height(), descriptionHeight = QFontMetrics(descriptionFont).height();
    const int top = (height() - titleHeight - descriptionHeight - 2) / 2;
    p.setFont(titleFont);
    p.setPen(pal.color(QPalette::WindowText));
    p.drawText(QRect(inner.left(), top, right - inner.left(), titleHeight), Qt::AlignLeft | Qt::AlignVCenter, title);
    p.setFont(descriptionFont);
    p.setPen(Look::secondary(pal));
    p.drawText(QRect(inner.left(), top + titleHeight + 2, right - inner.left(), descriptionHeight), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(descriptionFont).elidedText(description_, Qt::ElideRight, right - inner.left()));
}

CaptionStage::CaptionStage(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(118);
    setAccessibleName(QStringLiteral("마지막 대사"));
}
void CaptionStage::setContent(const QString& source, const QString& translation) {
    source_ = source.simplified();
    translation_ = translation.simplified();
    // Screen readers (and the QA harness) read the line in the name: "마지막 대사: original / translation".
    QString name = QStringLiteral("마지막 대사");
    if (!source_.isEmpty()) name += QStringLiteral(": ") + source_;
    if (!translation_.isEmpty()) name += QStringLiteral(" / ") + translation_;
    setAccessibleName(name);
    update();
}
void CaptionStage::setStyle(double scale, bool box) { scale_ = scale; box_ = box; update(); }
void CaptionStage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor::fromRgba(Look::Scene));
    p.drawRoundedRect(rect(), 8, 8);
    const QRect inner = rect().adjusted(16, 12, -16, -12);
    if (translation_.isEmpty() && source_.isEmpty()) {
        p.setFont(sized(font(), 13));
        p.setPen(QColor::fromRgba(Look::Source));
        p.drawText(inner, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, placeholder_);
        return;
    }
    int top = inner.top();
    if (!source_.isEmpty()) {
        const QFont sourceFont = sized(font(), 13);
        const QFontMetrics metrics(sourceFont);
        p.setFont(sourceFont);
        p.setPen(QColor::fromRgba(Look::Source));
        p.drawText(QRect(inner.left(), top, inner.width(), metrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   metrics.elidedText(source_, Qt::ElideRight, inner.width()));
        top += metrics.height() + 6;
    }
    // The translation in the overlay's own style: same weight, size scale and background choice.
    QFont captionFont(QStringLiteral("Malgun Gothic"));
    captionFont.setWeight(QFont::DemiBold);
    captionFont.setPixelSize(qBound(14, int(20 * scale_), 30));
    const QFontMetrics metrics(captionFont);
    const QString shown = translation_.isEmpty() ? source_ : translation_;
    const QRect area(inner.left(), top, inner.width(), inner.bottom() - top + 1);
    const QString elided = metrics.elidedText(shown, Qt::ElideRight, area.width() * qMax(1, area.height() / metrics.lineSpacing()));
    QRect used = metrics.boundingRect(area, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, elided);
    p.setFont(captionFont);
    SubtitleOverlay::paintCaption(p, area, used.adjusted(-6, -3, 6, 3), elided, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, box_);
}

SettingRow::SettingRow(char16_t glyph, const QString& title, const QString& description, QWidget* control, QWidget* parent)
    : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(16, 10, 16, 10);
    row->setSpacing(14);
    if (glyph) row->addWidget(new GlyphLabel(glyph, 16));
    auto* texts = new QVBoxLayout;
    texts->setSpacing(1);
    auto* heading = new QLabel(title);
    heading->setWordWrap(true);
    texts->addWidget(heading);
    description_ = new Note(description);
    description_->setVisible(!description.isEmpty());
    texts->addWidget(description_);
    row->addLayout(texts, 1);
    if (control) row->addWidget(control, 0, Qt::AlignVCenter);
    setMinimumHeight(56);
}
void SettingRow::setDescription(const QString& text) {
    description_->setText(text);
    description_->setVisible(!text.isEmpty());
}
void SettingRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Look::hairline(palette()), 1));
    p.setBrush(Look::rowFill(palette()));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
}
