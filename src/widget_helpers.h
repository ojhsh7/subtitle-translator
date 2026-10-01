#pragma once
#include <QString>
class QWidget;
class QLabel;
namespace Ui {
// A short first-visit guide for a mode (at most three lines) with "다시 보지 않기".
QWidget* hint(const QString& key, const QString& text, QWidget* parent = nullptr);
// Status-style panel that follows the light/dark palette instead of fixed colours.
void panel(QLabel* label);
}
