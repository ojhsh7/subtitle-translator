#include "file_mode.h"
#include <QLabel>
#include <QVBoxLayout>

FileMode::FileMode(const ModeContext& context, QWidget* parent) : QWidget(parent), ctx_(context) {
    auto* layout = new QVBoxLayout(this);
    auto* label = new QLabel(QStringLiteral("자막 파일 모드는 준비 중입니다."));
    label->setWordWrap(true);
    layout->addWidget(label);
    layout->addStretch(1);
}
