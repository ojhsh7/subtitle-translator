#include "text_mode.h"
#include <QLabel>
#include <QVBoxLayout>

TextMode::TextMode(const ModeContext& context, QWidget* parent) : QWidget(parent), ctx_(context) {
    auto* layout = new QVBoxLayout(this);
    auto* label = new QLabel(QStringLiteral("글 모드는 준비 중입니다."));
    label->setWordWrap(true);
    layout->addWidget(label);
    layout->addStretch(1);
}
void TextMode::translateClipboard() {
    if (ctx_.status) ctx_.status(QStringLiteral("글 모드는 준비 중입니다."));
}
