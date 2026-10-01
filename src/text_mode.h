#pragma once
#include "mode_context.h"
#include <QWidget>

// Text mode (stage 3): clipboard and screen-area articles, translated paragraph by paragraph.
class TextMode final : public QWidget {
    Q_OBJECT
public:
    explicit TextMode(const ModeContext& context, QWidget* parent = nullptr);
    void translateClipboard();
private:
    ModeContext ctx_;
};
