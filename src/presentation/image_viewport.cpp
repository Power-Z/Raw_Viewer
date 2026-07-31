#include "presentation/image_viewport.h"

#include <QDragEnterEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace rawviewer::presentation {

ImageViewport::ImageViewport(QWidget* parent)
    : QWidget(parent) {
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
}

void ImageViewport::setImage(
    std::shared_ptr<const application::DecodedImage> image,
    bool preserveView) {
    image_ = std::move(image);
    if (!image_ || image_->preview.rgba.empty()) {
        clearImage();
        return;
    }
    const auto& source = image_->preview;
    preview_ = QImage(source.rgba.data(),
                      source.width,
                      source.height,
                      source.width * 4,
                      QImage::Format_RGBA8888).copy();
    if (preserveView) {
        update();
    } else {
        fitMode_ = true;
        fitToWindow();
    }
}

void ImageViewport::clearImage() {
    image_.reset();
    preview_ = {};
    zoom_ = 1.0;
    offset_ = {};
    update();
}

void ImageViewport::fitToWindow() {
    if (!image_ || image_->metadata.width == 0 || image_->metadata.height == 0) {
        return;
    }
    const double xScale =
        static_cast<double>(width()) / image_->metadata.width;
    const double yScale =
        static_cast<double>(height()) / image_->metadata.height;
    zoom_ = std::min(xScale, yScale);
    const double displayWidth = image_->metadata.width * zoom_;
    const double displayHeight = image_->metadata.height * zoom_;
    offset_ = QPointF((width() - displayWidth) / 2.0,
                      (height() - displayHeight) / 2.0);
    fitMode_ = true;
    emit zoomChanged(zoom_);
    update();
}

void ImageViewport::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base).darker(135));
    if (preview_.isNull() || !image_) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(),
                         Qt::AlignCenter,
                         tr("将图片拖到这里，或从左侧文件树打开"));
        return;
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 1.0);
    const QRectF target(offset_.x(),
                        offset_.y(),
                        image_->metadata.width * zoom_,
                        image_->metadata.height * zoom_);
    painter.drawImage(target, preview_);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(target);
}

void ImageViewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fitMode_) {
        fitToWindow();
    }
}

void ImageViewport::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && image_) {
        dragging_ = true;
        fitMode_ = false;
        lastMouse_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void ImageViewport::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        offset_ += event->position() - lastMouse_;
        lastMouse_ = event->position();
        update();
    }
    publishCoordinate(event->position());
}

void ImageViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        unsetCursor();
    }
}

void ImageViewport::wheelEvent(QWheelEvent* event) {
    if (!image_) {
        return;
    }
    const QPointF anchor = event->position();
    const QPointF sourceAnchor = imageCoordinate(anchor);
    const double factor = std::pow(1.0015, event->angleDelta().y());
    zoom_ = std::clamp(zoom_ * factor, 0.0001, 64.0);
    offset_ = anchor - sourceAnchor * zoom_;
    fitMode_ = false;
    emit zoomChanged(zoom_);
    publishCoordinate(anchor);
    update();
    event->accept();
}

void ImageViewport::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() &&
        event->mimeData()->urls().size() == 1 &&
        event->mimeData()->urls().first().isLocalFile()) {
        event->acceptProposedAction();
    }
}

void ImageViewport::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.size() == 1 && urls.first().isLocalFile()) {
        emit fileDropped(urls.first().toLocalFile());
        event->acceptProposedAction();
    }
}

QPointF ImageViewport::imageCoordinate(const QPointF& widgetPoint) const {
    return (widgetPoint - offset_) / zoom_;
}

void ImageViewport::publishCoordinate(const QPointF& widgetPoint) {
    if (!image_) {
        emit imageCoordinateChanged(0, 0, false);
        return;
    }
    const QPointF point = imageCoordinate(widgetPoint);
    const qint64 x = static_cast<qint64>(std::floor(point.x()));
    const qint64 y = static_cast<qint64>(std::floor(point.y()));
    const bool inside =
        x >= 0 && y >= 0 &&
        static_cast<std::uint64_t>(x) < image_->metadata.width &&
        static_cast<std::uint64_t>(y) < image_->metadata.height;
    emit imageCoordinateChanged(x, y, inside);
}

} // namespace rawviewer::presentation
