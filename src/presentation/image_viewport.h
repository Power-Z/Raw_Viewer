#pragma once

#include "application/image_types.h"

#include <QImage>
#include <QPointF>
#include <QWidget>

#include <memory>

namespace rawviewer::presentation {

class ImageViewport final : public QWidget {
    Q_OBJECT

public:
    explicit ImageViewport(QWidget* parent = nullptr);

    void setImage(std::shared_ptr<const application::DecodedImage> image,
                  bool preserveView = false);
    void clearImage();
    void fitToWindow();
    double zoom() const noexcept { return zoom_; }

signals:
    void fileDropped(const QString& path);
    void imageCoordinateChanged(qint64 x, qint64 y, bool inside);
    void zoomChanged(double zoom);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QPointF imageCoordinate(const QPointF& widgetPoint) const;
    void publishCoordinate(const QPointF& widgetPoint);

    std::shared_ptr<const application::DecodedImage> image_;
    QImage preview_;
    double zoom_ = 1.0;
    QPointF offset_;
    QPointF lastMouse_;
    bool dragging_ = false;
    bool fitMode_ = true;
};

} // namespace rawviewer::presentation
