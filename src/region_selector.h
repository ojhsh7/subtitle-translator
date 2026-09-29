#pragma once
#include <QDialog>
#include <QPixmap>
#include <QPoint>
class QMouseEvent;
class RegionSelector : public QDialog {
public:
    RegionSelector(const QPixmap& snapshot, const QRect& screenGeometry);
    QRect selectedRegion() const { return selection_; }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    QPixmap snapshot_;
    QPoint start_;
    QRect selection_;
    bool dragging_ = false;
};
