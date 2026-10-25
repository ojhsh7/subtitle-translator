#pragma once
#include <QDialog>
#include <QPixmap>
#include <QPoint>
class QKeyEvent;
class QMouseEvent;
class RegionSelector : public QDialog {
public:
    // current: the region in use on this screen (local logical pixels), shown dashed and adjustable by keyboard.
    RegionSelector(const QPixmap& snapshot, const QRect& screenGeometry, const QRect& current = {});
    QRect selectedRegion() const { return selection_; }
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
    QRect current_;
    bool dragging_ = false;
};
