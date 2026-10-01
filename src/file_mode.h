#pragma once
#include "mode_context.h"
#include <QWidget>

// Subtitle file mode (stage 2): .srt/.vtt/.ass translated into a new file next to the original.
class FileMode final : public QWidget {
    Q_OBJECT
public:
    explicit FileMode(const ModeContext& context, QWidget* parent = nullptr);
    bool running() const { return false; }
private:
    ModeContext ctx_;
};
