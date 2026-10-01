#pragma once
#include <QDialog>
#include <QPixmap>
#include <QPoint>
class QMouseEvent;
class QKeyEvent;
class RegionSelector : public QDialog {
public:
    RegionSelector(const QPixmap& snapshot, const QRect& screenGeometry);
    QRect selectedRegion() const { return selection_; }
    // A suggested area (screen-relative logical pixels) the user accepts with Enter or redraws.
    void setSuggestion(const QRect& region);
    void setHint(const QString& hint) { hint_ = hint; update(); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    QPixmap snapshot_;
    QPoint start_;
    QRect selection_;
    QString hint_;
    bool dragging_ = false;
};
