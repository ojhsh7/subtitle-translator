#include "widget_helpers.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>

namespace Ui {
QWidget* hint(const QString& key, const QString& text, QWidget* parent) {
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("modeHint"));
    frame->setStyleSheet(QStringLiteral("#modeHint { background: palette(alternate-base); border: 1px solid palette(mid); border-radius: 6px; }"));
    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(12, 8, 8, 8);
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(label, 1);
    auto* close = new QPushButton(QStringLiteral("다시 보지 않기"));
    close->setAccessibleName(QStringLiteral("안내 닫기, 다시 보지 않기"));
    layout->addWidget(close, 0, Qt::AlignTop);
    const QString settingKey = QStringLiteral("hints/") + key;
    QObject::connect(close, &QPushButton::clicked, frame, [frame, settingKey] {
        QSettings().setValue(settingKey, false);
        frame->hide();
    });
    frame->setVisible(QSettings().value(settingKey, true).toBool());
    return frame;
}
void panel(QLabel* label) {
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("background: palette(alternate-base); color: palette(text); padding: 10px; border-radius: 6px;"));
}
}
