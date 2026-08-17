#include "presentation/image_viewport.h"

#include "application/pixel_info.h"

#include <QDragEnterEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QResizeEvent>
#include <QUrl>
#include <QVBoxLayout>
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
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    loadingOverlay_ = new QWidget(this);
    loadingOverlay_->setObjectName(QStringLiteral("rawLoadingOverlay"));
    loadingOverlay_->setAttribute(Qt::WA_StyledBackground, true);
    loadingOverlay_->setStyleSheet(
        QStringLiteral("#rawLoadingOverlay { background-color: rgba(0, 0, 0, 150); }"));
    auto* loadingLayout = new QVBoxLayout(loadingOverlay_);
    loadingLayout->addStretch();
    loadingLabel_ = new QLabel(tr("正在加载 RAW 图像…"), loadingOverlay_);
    loadingLabel_->setAlignment(Qt::AlignCenter);
    loadingLabel_->setStyleSheet(QStringLiteral("color: white; font-weight: 600;"));
    loadingProgress_ = new QProgressBar(loadingOverlay_);
    loadingProgress_->setRange(0, 0);
    loadingProgress_->setTextVisible(false);
    loadingProgress_->setFixedWidth(260);
    loadingLayout->addWidget(loadingLabel_);
    loadingLayout->addWidget(loadingProgress_, 0, Qt::AlignHCenter);
    loadingLayout->addStretch();
    loadingOverlay_->hide();
}

void ImageViewport::setImage(
    std::shared_ptr<const application::DecodedImage> image,
    bool preserveView) {
    preview_ = {};
    image_ = std::move(image);
    if (!image_) {
        clearImage();
        return;
    }
    const auto& source = image_->preview;
    if (source.hasGrayscale16()) {
        preview_ = QImage(
            reinterpret_cast<const uchar*>(source.grayscale16Pixels),
            source.width,
            source.height,
            source.grayscale16StrideSamples *
                static_cast<int>(sizeof(std::uint16_t)),
            QImage::Format_Grayscale16);
    } else if (!source.rgba.empty()) {
        preview_ = QImage(source.rgba.data(),
                          source.width,
                          source.height,
                          source.width * 4,
                          QImage::Format_RGBA8888).copy();
    }
    if (preview_.isNull()) {
        clearImage();
        return;
    }
    if (preserveView) {
        update();
    } else {
        fitMode_ = true;
        fitToWindow();
    }
}

void ImageViewport::clearImage() {
    preview_ = {};
    image_.reset();
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

void ImageViewport::setDisplayMapping(
    const domain::DisplayMapping& mapping) {
    displayMapping_ = mapping;
    update();
}

void ImageViewport::setPixelOverlayOptions(
    const PixelOverlayOptions& options) {
    overlayOptions_ = options;
    update();
}

void ImageViewport::setLoading(bool loading, const QString& message) {
    if (!message.isEmpty()) {
        loadingLabel_->setText(message);
    }
    loadingOverlay_->setVisible(loading);
    if (loading) {
        loadingOverlay_->setGeometry(rect());
        loadingOverlay_->raise();
    }
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

    const bool smoothScale =
        image_->metadata.kind != application::ImageKind::FlatRaw &&
        zoom_ < 1.0;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smoothScale);
    const QRectF target(offset_.x(),
                        offset_.y(),
                        image_->metadata.width * zoom_,
                        image_->metadata.height * zoom_);
    painter.drawImage(target, preview_);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(target);
    drawPixelOverlay(painter);
    drawStatisticsSelection(painter);
}

void ImageViewport::drawStatisticsSelection(QPainter& painter) {
    if (!statisticsSelectionVisible_ || statisticsTool_ == StatisticsSelectionTool::None) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(palette().color(QPalette::Highlight));
    pen.setCosmetic(true);
    pen.setWidth(2);
    pen.setStyle(statisticsSelectionStarted_ ? Qt::DashLine : Qt::SolidLine);
    painter.setPen(pen);
    const auto point = [this](const QPoint& source, bool center) {
        const double shift = center ? 0.5 : 0.0;
        return QPointF(offset_.x() + (source.x() + shift) * zoom_,
                       offset_.y() + (source.y() + shift) * zoom_);
    };
    if (statisticsTool_ == StatisticsSelectionTool::Line) {
        painter.drawLine(point(statisticsStart_, true),
                         point(statisticsEnd_, true));
        painter.setBrush(palette().color(QPalette::Highlight));
        painter.drawEllipse(point(statisticsStart_, true), 4.0, 4.0);
        painter.drawEllipse(point(statisticsEnd_, true), 4.0, 4.0);
    } else {
        const int left = std::min(statisticsStart_.x(), statisticsEnd_.x());
        const int top = std::min(statisticsStart_.y(), statisticsEnd_.y());
        const int right = std::max(statisticsStart_.x(), statisticsEnd_.x()) + 1;
        const int bottom = std::max(statisticsStart_.y(), statisticsEnd_.y()) + 1;
        const QRectF rectangle(point(QPoint(left, top), false),
                               point(QPoint(right, bottom), false));
        const QColor accent = palette().color(QPalette::Highlight);
        painter.fillRect(rectangle,
                         QColor(accent.red(), accent.green(), accent.blue(), 45));
        painter.drawRect(rectangle);
    }
    painter.restore();
}

void ImageViewport::drawPixelOverlay(QPainter& painter) {
    const bool hasOverlay = overlayOptions_.enabled ||
        overlayOptions_.showMesh || overlayOptions_.showBayerLabel;
    if (!hasOverlay || !image_ || preview_.isNull() ||
        zoom_ < overlayOptions_.minimumCellPixels ||
        (overlayOptions_.enabled && !image_->pixels)) {
        return;
    }

    const auto imageWidth = static_cast<qint64>(image_->metadata.width);
    const auto imageHeight = static_cast<qint64>(image_->metadata.height);
    const qint64 left = std::clamp<qint64>(
        static_cast<qint64>(std::floor((-offset_.x()) / zoom_)),
        0,
        imageWidth);
    const qint64 top = std::clamp<qint64>(
        static_cast<qint64>(std::floor((-offset_.y()) / zoom_)),
        0,
        imageHeight);
    const qint64 right = std::clamp<qint64>(
        static_cast<qint64>(std::ceil((width() - offset_.x()) / zoom_)),
        0,
        imageWidth);
    const qint64 bottom = std::clamp<qint64>(
        static_cast<qint64>(std::ceil((height() - offset_.y()) / zoom_)),
        0,
        imageHeight);
    if (right <= left || bottom <= top) {
        return;
    }

    painter.save();
    painter.setClipRect(rect());

    const auto textColorAt = [this, imageWidth, imageHeight](qint64 x, qint64 y) {
        const int previewX = std::clamp(
            static_cast<int>((x * preview_.width()) / imageWidth),
            0,
            preview_.width() - 1);
        const int previewY = std::clamp(
            static_cast<int>((y * preview_.height()) / imageHeight),
            0,
            preview_.height() - 1);
        const QColor background = preview_.pixelColor(previewX, previewY);
        const int luminance = qGray(background.rgb());
        return luminance >= 140 ? QColor(Qt::black) : QColor(Qt::white);
    };
    const auto meshColor = [](domain::BayerChannel channel) {
        switch (channel) {
        case domain::BayerChannel::R: return QColor(255, 55, 55, 64);
        case domain::BayerChannel::Gr: return QColor(80, 235, 105, 64);
        case domain::BayerChannel::Gb: return QColor(25, 165, 70, 64);
        case domain::BayerChannel::B: return QColor(55, 95, 255, 64);
        case domain::BayerChannel::None: return QColor(Qt::transparent);
        }
        return QColor(Qt::transparent);
    };

    QFont valueFont = painter.font();
    valueFont.setPixelSize(
        std::max(1, static_cast<int>(std::lround(zoom_ / 6.0))));
    QFont patternFont = painter.font();
    patternFont.setPixelSize(
        std::max(1, static_cast<int>(std::lround(zoom_ / 7.0))));
    const double patternMargin = std::max(1.0, zoom_ / 28.0);
    for (qint64 y = top; y < bottom; ++y) {
        for (qint64 x = left; x < right; ++x) {
            const auto channel = domain::bayerChannelAt(
                image_->metadata.bayerPattern,
                static_cast<std::uint64_t>(x),
                static_cast<std::uint64_t>(y));
            const QRectF cell(offset_.x() + x * zoom_,
                              offset_.y() + y * zoom_,
                              zoom_,
                              zoom_);
            if (overlayOptions_.showMesh &&
                channel != domain::BayerChannel::None) {
                painter.fillRect(cell, meshColor(channel));
            }

            if (overlayOptions_.enabled) {
                QString text;
                if (image_->metadata.kind == application::ImageKind::Standard) {
                    const auto info = application::queryPixelInfo(
                        *image_, displayMapping_,
                        static_cast<std::uint64_t>(x),
                        static_cast<std::uint64_t>(y));
                    if (info.valid && info.rgbValid) {
                        text = QStringLiteral("%1,%2,%3")
                            .arg(info.red).arg(info.green).arg(info.blue);
                    } else if (info.valid) {
                        text = QString::number(info.originalValue, 'g', 8);
                    }
                } else {
                    const auto sample = image_->pixels->sample(
                        static_cast<std::uint64_t>(x),
                        static_cast<std::uint64_t>(y));
                    if (sample.valid) {
                        text = QString::number(sample.value, 'g', 8);
                    }
                }
                if (!text.isEmpty()) {
                    painter.setFont(valueFont);
                    painter.setPen(textColorAt(x, y));
                    painter.drawText(cell,
                                     Qt::AlignCenter | Qt::TextDontClip,
                                     text);
                }
            }

            if (overlayOptions_.showBayerLabel &&
                channel != domain::BayerChannel::None) {
                painter.setFont(patternFont);
                painter.setPen(textColorAt(x, y));
                painter.drawText(
                    cell.adjusted(patternMargin, patternMargin,
                                  -patternMargin, -patternMargin),
                    Qt::AlignRight | Qt::AlignBottom | Qt::TextDontClip,
                    QString::fromLatin1(domain::toString(channel)));
            }
        }
    }
    painter.restore();
}

void ImageViewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    loadingOverlay_->setGeometry(rect());
    if (fitMode_) {
        fitToWindow();
    }
}

void ImageViewport::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && image_) {
        if (statisticsTool_ != StatisticsSelectionTool::None) {
            const QPoint selected = boundedImagePixel(event->position());
            if (selected.x() < 0 || selected.y() < 0) {
                return;
            }
            if (!statisticsSelectionStarted_) {
                statisticsStart_ = selected;
                statisticsEnd_ = selected;
                statisticsSelectionStarted_ = true;
                statisticsSelectionVisible_ = true;
            } else {
                statisticsEnd_ = selected;
                statisticsSelectionStarted_ = false;
                emit statisticsSelectionCompleted(
                    statisticsStart_.x(),
                    statisticsStart_.y(),
                    statisticsEnd_.x(),
                    statisticsEnd_.y(),
                    statisticsTool_ == StatisticsSelectionTool::Line);
            }
            update();
            event->accept();
            return;
        }
        dragging_ = true;
        fitMode_ = false;
        lastMouse_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void ImageViewport::mouseMoveEvent(QMouseEvent* event) {
    if (statisticsSelectionStarted_) {
        const QPoint selected = boundedImagePixel(event->position());
        if (selected.x() >= 0 && selected.y() >= 0) {
            statisticsEnd_ = selected;
            update();
        }
    }
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
    zoom_ = std::clamp(zoom_ * factor, 0.0001, 256.0);
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

void ImageViewport::setStatisticsSelectionTool(
    StatisticsSelectionTool tool) {
    statisticsTool_ = tool;
    statisticsSelectionStarted_ = false;
    statisticsSelectionVisible_ = false;
    dragging_ = false;
    if (statisticsTool_ == StatisticsSelectionTool::None) {
        unsetCursor();
    } else {
        setCursor(Qt::CrossCursor);
    }
    update();
}

void ImageViewport::clearStatisticsSelection() {
    statisticsSelectionStarted_ = false;
    statisticsSelectionVisible_ = false;
    update();
}

QPoint ImageViewport::boundedImagePixel(const QPointF& widgetPoint) const {
    if (!image_ || image_->metadata.width == 0 || image_->metadata.height == 0) {
        return {-1, -1};
    }
    const QPointF source = imageCoordinate(widgetPoint);
    if (source.x() < 0.0 || source.y() < 0.0 ||
        source.x() >= static_cast<double>(image_->metadata.width) ||
        source.y() >= static_cast<double>(image_->metadata.height)) {
        return {-1, -1};
    }
    const auto maximumX = static_cast<qint64>(image_->metadata.width - 1);
    const auto maximumY = static_cast<qint64>(image_->metadata.height - 1);
    return {
        static_cast<int>(std::clamp<qint64>(
            static_cast<qint64>(std::floor(source.x())), 0, maximumX)),
        static_cast<int>(std::clamp<qint64>(
            static_cast<qint64>(std::floor(source.y())), 0, maximumY))
    };
}

} // namespace rawviewer::presentation
