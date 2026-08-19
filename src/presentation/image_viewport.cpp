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
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rawviewer::presentation {
namespace {

constexpr int rulerExtent = 24;
constexpr int scrollBarExtent = 14;
constexpr int maximumScrollRange = 2'000'000'000;
// Exact source sampling runs on the paint path, so only enable it once a
// source pixel is visibly large and keep the work bounded for 4K viewports.
constexpr double exactPixelMinimumZoom = 16.0;
constexpr qint64 maximumExactPixelCacheSamples = 65'536;

double niceRulerStep(double minimumStep) {
    minimumStep = std::max(1.0, minimumStep);
    const double magnitude = std::pow(10.0, std::floor(std::log10(minimumStep)));
    for (const double factor : {1.0, 2.0, 5.0, 10.0}) {
        const double candidate = factor * magnitude;
        if (candidate >= minimumStep) {
            return std::max(1.0, std::round(candidate));
        }
    }
    return std::max(1.0, std::round(10.0 * magnitude));
}

} // namespace

ImageViewport::ImageViewport(QWidget* parent)
    : QWidget(parent) {
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    horizontalScrollBar_ = new QScrollBar(Qt::Horizontal, this);
    horizontalScrollBar_->setObjectName(
        QStringLiteral("imageHorizontalScrollBar"));
    horizontalScrollBar_->setFixedHeight(scrollBarExtent);
    verticalScrollBar_ = new QScrollBar(Qt::Vertical, this);
    verticalScrollBar_->setObjectName(
        QStringLiteral("imageVerticalScrollBar"));
    verticalScrollBar_->setFixedWidth(scrollBarExtent);
    connect(horizontalScrollBar_, &QScrollBar::valueChanged,
            this, &ImageViewport::applyHorizontalScroll);
    connect(verticalScrollBar_, &QScrollBar::valueChanged,
            this, &ImageViewport::applyVerticalScroll);

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
    updateNavigationGeometry();
    updateScrollBars();
}

void ImageViewport::setImage(
    std::shared_ptr<const application::DecodedImage> image,
    bool preserveView) {
    if (!preserveView) {
        // A queued move/release from the previous document must never apply
        // its drag anchor or selection geometry to the replacement image.
        dragging_ = false;
        draggingButton_ = Qt::NoButton;
        lastMouse_ = {};
        statisticsSelectionStarted_ = false;
        statisticsSelectionVisible_ = false;
        zoom_ = 1.0;
        offset_ = {};
        fitMode_ = true;
        if (statisticsTool_ == StatisticsSelectionTool::None) {
            unsetCursor();
        } else {
            setCursor(Qt::CrossCursor);
        }
        emit imageCoordinateChanged(0, 0, false);
    }
    preview_ = {};
    image_ = std::move(image);
    invalidatePixelOverlayCache();
    invalidateExactPixelCache();
    invalidateMappedGrayscaleCache();
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
        updateScrollBars();
        update();
    } else {
        fitMode_ = true;
        fitToWindow();
    }
}

void ImageViewport::clearImage() {
    preview_ = {};
    image_.reset();
    invalidatePixelOverlayCache();
    invalidateExactPixelCache();
    invalidateMappedGrayscaleCache();
    zoom_ = 1.0;
    offset_ = {};
    emit imageCoordinateChanged(0, 0, false);
    updateScrollBars();
    update();
}

void ImageViewport::fitToWindow() {
    if (!image_ || image_->metadata.width == 0 || image_->metadata.height == 0) {
        return;
    }
    const QRect canvas = canvasRect();
    if (canvas.isEmpty()) {
        return;
    }
    const double xScale =
        static_cast<double>(canvas.width()) / image_->metadata.width;
    const double yScale =
        static_cast<double>(canvas.height()) / image_->metadata.height;
    zoom_ = std::min(xScale, yScale);
    const double displayWidth = image_->metadata.width * zoom_;
    const double displayHeight = image_->metadata.height * zoom_;
    offset_ = QPointF(canvas.left() + (canvas.width() - displayWidth) / 2.0,
                      canvas.top() + (canvas.height() - displayHeight) / 2.0);
    fitMode_ = true;
    updateScrollBars();
    emit zoomChanged(zoom_);
    update();
}

void ImageViewport::setDisplayMapping(
    const domain::DisplayMapping& mapping) {
    if (displayMapping_ == mapping) {
        return;
    }
    displayMapping_ = mapping;
    invalidatePixelOverlayCache();
    invalidateExactPixelCache();
    invalidateMappedGrayscaleCache();
    update();
}

void ImageViewport::setPixelOverlayOptions(
    const PixelOverlayOptions& options) {
    if (overlayOptions_ == options) {
        return;
    }
    if (overlayOptions_.enabled != options.enabled) {
        invalidatePixelOverlayCache();
    }
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
    const QRect canvas = canvasRect();
    if (preview_.isNull() || !image_) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(canvas,
                         Qt::AlignCenter,
                         tr("将图片拖到这里，或从左侧文件树打开"));
        drawRulers(painter);
        return;
    }

    painter.save();
    painter.setClipRect(canvas);
    const bool smoothScale =
        image_->metadata.kind != application::ImageKind::FlatRaw &&
        zoom_ < 1.0;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, smoothScale);
    const QRectF target(offset_.x(),
                        offset_.y(),
                        image_->metadata.width * zoom_,
                        image_->metadata.height * zoom_);
    const bool directGrayscale = image_->preview.hasGrayscale16() &&
        image_->preview.width ==
            static_cast<int>(image_->metadata.width) &&
        image_->preview.height ==
            static_cast<int>(image_->metadata.height);
    if (directGrayscale) {
        drawMappedGrayscale16(painter, canvas, target);
    } else {
        painter.drawImage(target, preview_);
    }
    drawExactPixelLayer(painter);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(target);
    drawPixelOverlay(painter);
    drawStatisticsSelection(painter);
    painter.restore();
    drawRulers(painter);
    drawOverview(painter);
}

QRect ImageViewport::canvasRect() const {
    return QRect(rulerExtent,
                 rulerExtent,
                 std::max(0, width() - rulerExtent - scrollBarExtent),
                 std::max(0, height() - rulerExtent - scrollBarExtent));
}

void ImageViewport::drawRulers(QPainter& painter) {
    const QRect canvas = canvasRect();
    const QRect topRuler(canvas.left(), 0, canvas.width(), rulerExtent);
    const QRect leftRuler(0, canvas.top(), rulerExtent, canvas.height());
    const QColor background = palette().color(QPalette::Window);
    const QColor border = palette().color(QPalette::Mid);
    const QColor tick = palette().color(QPalette::WindowText);
    painter.fillRect(topRuler, background);
    painter.fillRect(leftRuler, background);
    painter.fillRect(QRect(0, 0, rulerExtent, rulerExtent), background);
    painter.fillRect(QRect(canvas.right() + 1, 0,
                           scrollBarExtent, rulerExtent), background);
    painter.fillRect(QRect(0, canvas.bottom() + 1,
                           rulerExtent, scrollBarExtent), background);
    painter.setPen(border);
    painter.drawLine(topRuler.bottomLeft(), topRuler.bottomRight());
    painter.drawLine(leftRuler.topRight(), leftRuler.bottomRight());

    if (!image_ || image_->metadata.width == 0 ||
        image_->metadata.height == 0 || zoom_ <= 0.0) {
        return;
    }
    const double visibleXStart = std::max(
        0.0, (canvas.left() - offset_.x()) / zoom_);
    const double visibleXEnd = std::min(
        static_cast<double>(image_->metadata.width),
        (canvas.right() + 1.0 - offset_.x()) / zoom_);
    const double visibleYStart = std::max(
        0.0, (canvas.top() - offset_.y()) / zoom_);
    const double visibleYEnd = std::min(
        static_cast<double>(image_->metadata.height),
        (canvas.bottom() + 1.0 - offset_.y()) / zoom_);
    if (visibleXEnd < visibleXStart || visibleYEnd < visibleYStart) {
        return;
    }

    const auto majorStep = static_cast<qint64>(
        niceRulerStep(80.0 / zoom_));
    const qint64 minorStep = majorStep >= 5 ? majorStep / 5 : 1;
    const qint64 firstX = std::max<qint64>(
        0, static_cast<qint64>(std::floor(visibleXStart / minorStep)) * minorStep);
    const qint64 lastX = std::min<qint64>(
        static_cast<qint64>(image_->metadata.width),
        static_cast<qint64>(std::ceil(visibleXEnd)));
    const qint64 firstY = std::max<qint64>(
        0, static_cast<qint64>(std::floor(visibleYStart / minorStep)) * minorStep);
    const qint64 lastY = std::min<qint64>(
        static_cast<qint64>(image_->metadata.height),
        static_cast<qint64>(std::ceil(visibleYEnd)));

    painter.save();
    painter.setPen(tick);
    QFont rulerFont = painter.font();
    rulerFont.setPixelSize(9);
    painter.setFont(rulerFont);
    painter.setClipRect(topRuler);
    for (qint64 value = firstX; value <= lastX;) {
        const double x = offset_.x() + static_cast<double>(value) * zoom_;
        const bool major = value % majorStep == 0;
        const int length = major ? 10 : 5;
        painter.drawLine(QPointF(x, topRuler.bottom()),
                         QPointF(x, topRuler.bottom() - length));
        if (major) {
            painter.drawText(QPointF(x + 3.0, 10.0), QString::number(value));
        }
        if (value > std::numeric_limits<qint64>::max() - minorStep) {
            break;
        }
        value += minorStep;
    }
    painter.setClipRect(leftRuler);
    for (qint64 value = firstY; value <= lastY;) {
        const double y = offset_.y() + static_cast<double>(value) * zoom_;
        const bool major = value % majorStep == 0;
        const int length = major ? 10 : 5;
        painter.drawLine(QPointF(leftRuler.right(), y),
                         QPointF(leftRuler.right() - length, y));
        if (major) {
            const QString label = QString::number(value);
            painter.save();
            painter.translate(9.0, y - 3.0);
            painter.rotate(-90.0);
            painter.drawText(QPointF(0.0, 0.0), label);
            painter.restore();
        }
        if (value > std::numeric_limits<qint64>::max() - minorStep) {
            break;
        }
        value += minorStep;
    }
    painter.restore();
}

void ImageViewport::drawExactPixelLayer(QPainter& painter) {
    if (!image_ || !image_->pixels || preview_.isNull() ||
        zoom_ < exactPixelMinimumZoom ||
        (preview_.width() == static_cast<qint64>(image_->metadata.width) &&
         preview_.height() == static_cast<qint64>(image_->metadata.height))) {
        return;
    }
    const QRect canvas = canvasRect();
    const qint64 left = std::clamp<qint64>(
        static_cast<qint64>(std::floor((canvas.left() - offset_.x()) / zoom_)),
        0, static_cast<qint64>(image_->metadata.width));
    const qint64 top = std::clamp<qint64>(
        static_cast<qint64>(std::floor((canvas.top() - offset_.y()) / zoom_)),
        0, static_cast<qint64>(image_->metadata.height));
    const qint64 right = std::clamp<qint64>(
        static_cast<qint64>(std::ceil(
            (canvas.right() + 1.0 - offset_.x()) / zoom_)),
        0, static_cast<qint64>(image_->metadata.width));
    const qint64 bottom = std::clamp<qint64>(
        static_cast<qint64>(std::ceil(
            (canvas.bottom() + 1.0 - offset_.y()) / zoom_)),
        0, static_cast<qint64>(image_->metadata.height));
    const QRect requested(static_cast<int>(left), static_cast<int>(top),
                          static_cast<int>(right - left),
                          static_cast<int>(bottom - top));
    if (requested.isEmpty() || !ensureExactPixelCache(requested)) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const QRectF target(
        offset_.x() + exactPixelCacheRegion_.x() * zoom_,
        offset_.y() + exactPixelCacheRegion_.y() * zoom_,
        exactPixelCacheRegion_.width() * zoom_,
        exactPixelCacheRegion_.height() * zoom_);
    painter.drawImage(target, exactPixelCache_);
    painter.restore();
}

void ImageViewport::invalidateExactPixelCache() {
    exactPixelCache_ = {};
    exactPixelCacheRegion_ = {};
    exactPixelCacheImage_ = nullptr;
}

bool ImageViewport::ensureExactPixelCache(const QRect& sourceRegion) {
    const bool compatible = exactPixelCacheImage_ == image_.get() &&
        exactPixelCacheMapping_ == displayMapping_;
    if (compatible && exactPixelCacheRegion_.contains(sourceRegion)) {
        return true;
    }
    const auto imageWidth = static_cast<int>(std::min<std::uint64_t>(
        image_->metadata.width, std::numeric_limits<int>::max()));
    const auto imageHeight = static_cast<int>(std::min<std::uint64_t>(
        image_->metadata.height, std::numeric_limits<int>::max()));
    constexpr int margin = 2;
    const QRect expanded = sourceRegion.adjusted(-margin, -margin,
                                                  margin, margin)
        .intersected(QRect(0, 0, imageWidth, imageHeight));
    const qint64 sampleCount = static_cast<qint64>(expanded.width()) *
        expanded.height();
    if (expanded.isEmpty() || sampleCount > maximumExactPixelCacheSamples) {
        return false;
    }

    QImage next(expanded.size(), QImage::Format_ARGB32);
    for (int y = 0; y < expanded.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(next.scanLine(y));
        for (int x = 0; x < expanded.width(); ++x) {
            const auto sample = image_->pixels->sample(
                static_cast<std::uint64_t>(expanded.x() + x),
                static_cast<std::uint64_t>(expanded.y() + y));
            if (!sample.valid) {
                row[x] = qRgba(0, 0, 0, 255);
                continue;
            }
            if (sample.rgbValid) {
                row[x] = qRgba(sample.red, sample.green, sample.blue, 255);
                continue;
            }
            const auto gray = static_cast<int>(std::round(
                domain::mapDisplayValue(sample.value, displayMapping_) * 255.0));
            row[x] = qRgba(gray, gray, gray, 255);
        }
    }
    exactPixelCache_ = std::move(next);
    exactPixelCacheRegion_ = expanded;
    exactPixelCacheImage_ = image_.get();
    exactPixelCacheMapping_ = displayMapping_;
    return true;
}

void ImageViewport::ensureGrayscaleDisplayLut() {
    if (grayscaleDisplayLutValid_ &&
        grayscaleDisplayLutMapping_ == displayMapping_) {
        return;
    }
    if (!domain::validateDisplayMapping(displayMapping_).valid) {
        grayscaleDisplayLut_.fill(0);
        grayscaleDisplayLutMapping_ = displayMapping_;
        grayscaleDisplayLutValid_ = true;
        return;
    }
    const double range =
        displayMapping_.whitePoint - displayMapping_.blackPoint;
    const double inverseGamma = 1.0 / displayMapping_.gamma;
    const bool linear = displayMapping_.gamma == 1.0;
    for (std::size_t index = 0; index < grayscaleDisplayLut_.size(); ++index) {
        const double normalized = std::clamp(
            (static_cast<double>(index) - displayMapping_.blackPoint) / range,
            0.0, 1.0);
        const double mapped = linear
            ? normalized : std::pow(normalized, inverseGamma);
        grayscaleDisplayLut_[index] = static_cast<std::uint8_t>(std::clamp(
            std::lround(mapped * 255.0),
            0L, 255L));
    }
    grayscaleDisplayLutMapping_ = displayMapping_;
    grayscaleDisplayLutValid_ = true;
}

void ImageViewport::invalidateMappedGrayscaleCache() {
    mappedGrayscaleCache_ = {};
    mappedGrayscaleTarget_ = {};
    mappedGrayscaleImageTarget_ = {};
    mappedGrayscaleImage_ = nullptr;
    grayscaleDisplayLutValid_ = false;
}

void ImageViewport::drawMappedGrayscale16(QPainter& painter,
                                          const QRect& canvas,
                                          const QRectF& target) {
    const QRect visible = target.toAlignedRect().intersected(canvas);
    if (visible.isEmpty() || !image_ || !image_->preview.hasGrayscale16()) {
        return;
    }
    if (mappedGrayscaleCache_.isNull() ||
        mappedGrayscaleTarget_ != visible ||
        mappedGrayscaleImageTarget_ != target ||
        mappedGrayscaleImage_ != image_.get() ||
        mappedGrayscaleMapping_ != displayMapping_) {
        ensureGrayscaleDisplayLut();
        mappedGrayscaleCache_ = QImage(
            visible.size(), QImage::Format_Grayscale8);
        const auto& source = image_->preview;
        std::vector<int> sourceColumns(
            static_cast<std::size_t>(visible.width()));
        for (int outputX = 0; outputX < visible.width(); ++outputX) {
            const double fraction =
                (visible.left() + outputX + 0.5 - target.left()) /
                target.width();
            sourceColumns[static_cast<std::size_t>(outputX)] = std::clamp(
                static_cast<int>(fraction * source.width),
                0, source.width - 1);
        }
        for (int outputY = 0; outputY < visible.height(); ++outputY) {
            const double fraction =
                (visible.top() + outputY + 0.5 - target.top()) /
                target.height();
            const int sourceY = std::clamp(
                static_cast<int>(fraction * source.height),
                0, source.height - 1);
            const auto* sourceRow = source.grayscale16Pixels +
                static_cast<std::size_t>(sourceY) *
                    source.grayscale16StrideSamples;
            auto* outputRow = mappedGrayscaleCache_.scanLine(outputY);
            for (int outputX = 0; outputX < visible.width(); ++outputX) {
                outputRow[outputX] = grayscaleDisplayLut_[sourceRow[
                    sourceColumns[static_cast<std::size_t>(outputX)]]];
            }
        }
        mappedGrayscaleTarget_ = visible;
        mappedGrayscaleImageTarget_ = target;
        mappedGrayscaleImage_ = image_.get();
        mappedGrayscaleMapping_ = displayMapping_;
    }
    painter.drawImage(visible.topLeft(), mappedGrayscaleCache_);
}

QImage ImageViewport::mappedGrayscaleOverview(const QSize& size) {
    if (!image_ || !image_->preview.hasGrayscale16() || size.isEmpty()) {
        return {};
    }
    ensureGrayscaleDisplayLut();
    QImage result(size, QImage::Format_Grayscale8);
    const auto& source = image_->preview;
    for (int y = 0; y < size.height(); ++y) {
        const int sourceY = std::min(
            source.height - 1,
            static_cast<int>(static_cast<long double>(y) /
                size.height() * source.height));
        const auto* sourceRow = source.grayscale16Pixels +
            static_cast<std::size_t>(sourceY) *
                source.grayscale16StrideSamples;
        auto* outputRow = result.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            const int sourceX = std::min(
                source.width - 1,
                static_cast<int>(static_cast<long double>(x) /
                    size.width() * source.width));
            outputRow[x] = grayscaleDisplayLut_[sourceRow[sourceX]];
        }
    }
    return result;
}

void ImageViewport::drawOverview(QPainter& painter) {
    if (!image_ || preview_.isNull() || image_->metadata.width == 0 ||
        image_->metadata.height == 0) {
        return;
    }
    const QRect canvas = canvasRect();
    const int maximumWidth = std::clamp(canvas.width() / 4, 90, 180);
    constexpr int maximumHeight = 120;
    constexpr int padding = 5;
    const double scale = std::min(
        static_cast<double>(maximumWidth - padding * 2) /
            image_->metadata.width,
        static_cast<double>(maximumHeight - padding * 2) /
            image_->metadata.height);
    const QSizeF overviewSize(
        std::max(1.0, image_->metadata.width * scale),
        std::max(1.0, image_->metadata.height * scale));
    const QRectF imageRect(
        canvas.right() - 10.0 - padding - overviewSize.width(),
        canvas.top() + 10.0 + padding,
        overviewSize.width(),
        overviewSize.height());
    const QRectF panel = imageRect.adjusted(-padding, -padding,
                                             padding, padding);

    painter.save();
    painter.setClipRect(canvas);
    painter.fillRect(panel, QColor(15, 18, 23, 190));
    painter.setOpacity(0.78);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const bool directGrayscale = image_->preview.hasGrayscale16() &&
        image_->preview.width ==
            static_cast<int>(image_->metadata.width) &&
        image_->preview.height ==
            static_cast<int>(image_->metadata.height);
    if (directGrayscale) {
        painter.drawImage(
            imageRect,
            mappedGrayscaleOverview(overviewSize.toSize()));
    } else {
        painter.drawImage(imageRect, preview_);
    }
    painter.setOpacity(1.0);
    painter.setPen(QPen(QColor(225, 231, 240, 190), 1.0));
    painter.drawRect(panel);

    const QRectF fullSource(0.0, 0.0,
                            static_cast<double>(image_->metadata.width),
                            static_cast<double>(image_->metadata.height));
    QRectF visibleSource(
        imageCoordinate(QPointF(canvas.left(), canvas.top())),
        imageCoordinate(QPointF(canvas.right() + 1.0,
                                canvas.bottom() + 1.0)));
    visibleSource = visibleSource.normalized().intersected(fullSource);
    if (!visibleSource.isEmpty()) {
        const auto mapX = [&imageRect, this](double sourceX) {
            return imageRect.left() + sourceX /
                static_cast<double>(image_->metadata.width) * imageRect.width();
        };
        const auto mapY = [&imageRect, this](double sourceY) {
            return imageRect.top() + sourceY /
                static_cast<double>(image_->metadata.height) * imageRect.height();
        };
        const QRectF viewportIndicator(
            QPointF(mapX(visibleSource.left()), mapY(visibleSource.top())),
            QPointF(mapX(visibleSource.right()), mapY(visibleSource.bottom())));
        QPen indicator(QColor(255, 184, 45));
        indicator.setWidth(2);
        indicator.setCosmetic(true);
        painter.setPen(indicator);
        painter.setBrush(QColor(255, 184, 45, 28));
        painter.drawRect(viewportIndicator);
    }
    painter.restore();
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
    const QRect canvas = canvasRect();
    const qint64 left = std::clamp<qint64>(
        static_cast<qint64>(std::floor(
            (canvas.left() - offset_.x()) / zoom_)),
        0,
        imageWidth);
    const qint64 top = std::clamp<qint64>(
        static_cast<qint64>(std::floor(
            (canvas.top() - offset_.y()) / zoom_)),
        0,
        imageHeight);
    const qint64 right = std::clamp<qint64>(
        static_cast<qint64>(std::ceil(
            (canvas.right() + 1.0 - offset_.x()) / zoom_)),
        0,
        imageWidth);
    const qint64 bottom = std::clamp<qint64>(
        static_cast<qint64>(std::ceil(
            (canvas.bottom() + 1.0 - offset_.y()) / zoom_)),
        0,
        imageHeight);
    if (right <= left || bottom <= top) {
        return;
    }

    ensurePixelOverlayCache(left, top, right, bottom);

    painter.save();
    painter.setClipRect(rect());
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
    const auto patternTextColor = [](domain::BayerChannel channel,
                                     bool lightBackground) {
        switch (channel) {
        case domain::BayerChannel::R:
            return lightBackground ? QColor(175, 0, 25) : QColor(255, 95, 105);
        case domain::BayerChannel::Gr:
            return lightBackground ? QColor(0, 120, 35) : QColor(105, 255, 120);
        case domain::BayerChannel::Gb:
            return lightBackground ? QColor(0, 95, 115) : QColor(55, 225, 215);
        case domain::BayerChannel::B:
            return lightBackground ? QColor(20, 55, 185) : QColor(115, 155, 255);
        case domain::BayerChannel::None:
            return QColor(Qt::transparent);
        }
        return QColor(Qt::transparent);
    };

    QFont valueFont = painter.font();
    valueFont.setPixelSize(
        std::max(1, static_cast<int>(std::lround(zoom_ / 6.0))));
    QFont rgbFont = painter.font();
    rgbFont.setPixelSize(
        std::max(1, static_cast<int>(std::lround(zoom_ / 10.0))));
    QFont patternFont = painter.font();
    patternFont.setPixelSize(
        std::max(1, static_cast<int>(std::lround(zoom_ / 7.0))));
    const double patternMargin = std::max(1.0, zoom_ / 28.0);
    const double valueMargin = std::max(1.0, zoom_ / 28.0);

    if (overlayOptions_.showMesh) {
        for (qint64 y = top; y < bottom; ++y) {
            for (qint64 x = left; x < right; ++x) {
                auto* cached = cachedOverlayCell(x, y);
                if (!cached || cached->channel == domain::BayerChannel::None) {
                    continue;
                }
                const QRectF cell(offset_.x() + x * zoom_,
                                  offset_.y() + y * zoom_,
                                  zoom_,
                                  zoom_);
                painter.fillRect(cell, meshColor(cached->channel));
            }
        }
    }

    if (overlayOptions_.enabled) {
        for (const bool lightBackground : {false, true}) {
            painter.setPen(lightBackground ? QColor(Qt::black)
                                           : QColor(Qt::white));
            for (qint64 y = top; y < bottom; ++y) {
                for (qint64 x = left; x < right; ++x) {
                    auto* cached = cachedOverlayCell(x, y);
                    if (!cached || cached->lightBackground != lightBackground ||
                        (!cached->rgbValue &&
                         cached->valueText.text().isEmpty())) {
                        continue;
                    }
                    const QFont& overlayFont = cached->rgbValue
                        ? rgbFont : valueFont;
                    painter.setFont(overlayFont);
                    if (cached->preparedValueFontPixels !=
                        overlayFont.pixelSize()) {
                        if (cached->rgbValue) {
                            for (auto& line : cached->rgbTexts) {
                                line.prepare(QTransform(), overlayFont);
                            }
                        } else {
                            cached->valueText.prepare(QTransform(), overlayFont);
                        }
                        cached->preparedValueFontPixels = overlayFont.pixelSize();
                    }
                    const QRectF cell(offset_.x() + x * zoom_,
                                      offset_.y() + y * zoom_,
                                      zoom_,
                                      zoom_);
                    if (cached->rgbValue) {
                        double totalHeight = 0.0;
                        for (const auto& line : cached->rgbTexts) {
                            totalHeight += line.size().height();
                        }
                        double textY = cell.bottom() - valueMargin - totalHeight;
                        const std::array<QColor, 3> channelColors{
                            lightBackground ? QColor(175, 0, 25)
                                            : QColor(255, 95, 105),
                            lightBackground ? QColor(0, 115, 35)
                                            : QColor(105, 255, 120),
                            lightBackground ? QColor(20, 55, 185)
                                            : QColor(115, 155, 255)};
                        for (std::size_t index = 0;
                             index < cached->rgbTexts.size(); ++index) {
                            const auto& line = cached->rgbTexts[index];
                            painter.setPen(channelColors[index]);
                            painter.drawStaticText(
                                QPointF(cell.left() + valueMargin, textY), line);
                            textY += line.size().height();
                        }
                    } else {
                        const QSizeF textSize = cached->valueText.size();
                        painter.drawStaticText(
                            QPointF(cell.center().x() - textSize.width() / 2.0,
                                    cell.center().y() - textSize.height() / 2.0),
                            cached->valueText);
                    }
                }
            }
        }
    }

    if (overlayOptions_.showBayerLabel) {
        painter.setFont(patternFont);
        QStaticText patternTexts[4] = {
            QStaticText(QStringLiteral("R")),
            QStaticText(QStringLiteral("Gr")),
            QStaticText(QStringLiteral("Gb")),
            QStaticText(QStringLiteral("B"))
        };
        for (auto& text : patternTexts) {
            text.setPerformanceHint(QStaticText::AggressiveCaching);
            text.prepare(QTransform(), patternFont);
        }
        const domain::BayerChannel channels[4] = {
            domain::BayerChannel::R,
            domain::BayerChannel::Gr,
            domain::BayerChannel::Gb,
            domain::BayerChannel::B
        };
        for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
            const auto channel = channels[channelIndex];
            for (const bool lightBackground : {false, true}) {
                painter.setPen(patternTextColor(channel, lightBackground));
                for (qint64 y = top; y < bottom; ++y) {
                    for (qint64 x = left; x < right; ++x) {
                        auto* cached = cachedOverlayCell(x, y);
                        if (!cached || cached->channel != channel ||
                            cached->lightBackground != lightBackground) {
                            continue;
                        }
                        const QRectF cell(offset_.x() + x * zoom_,
                                          offset_.y() + y * zoom_,
                                          zoom_,
                                          zoom_);
                        const QSizeF textSize = patternTexts[channelIndex].size();
                        painter.drawStaticText(
                            QPointF(cell.right() - patternMargin - textSize.width(),
                                    cell.bottom() - patternMargin - textSize.height()),
                            patternTexts[channelIndex]);
                    }
                }
            }
        }
    }
    painter.restore();
}

void ImageViewport::invalidatePixelOverlayCache() {
    overlayCellCache_.clear();
    overlayCacheImage_ = nullptr;
    overlayCacheLeft_ = 0;
    overlayCacheTop_ = 0;
    overlayCacheRight_ = 0;
    overlayCacheBottom_ = 0;
    overlayCacheIncludesValues_ = false;
}

ImageViewport::CachedOverlayCell ImageViewport::makeOverlayCell(
    qint64 x,
    qint64 y) const {
    CachedOverlayCell result;
    result.x = x;
    result.y = y;
    if (image_->pixels) {
        result.channel = image_->pixels->bayerChannel(
            static_cast<std::uint64_t>(x),
            static_cast<std::uint64_t>(y));
    }
    if (result.channel == domain::BayerChannel::None) {
        result.channel = domain::bayerChannelAt(
            image_->metadata.bayerPattern,
            static_cast<std::uint64_t>(x),
            static_cast<std::uint64_t>(y));
    }

    const auto imageWidth = static_cast<qint64>(image_->metadata.width);
    const auto imageHeight = static_cast<qint64>(image_->metadata.height);
    const int previewX = std::clamp(
        static_cast<int>((x * preview_.width()) / imageWidth),
        0,
        preview_.width() - 1);
    const int previewY = std::clamp(
        static_cast<int>((y * preview_.height()) / imageHeight),
        0,
        preview_.height() - 1);
    result.lightBackground =
        qGray(preview_.pixelColor(previewX, previewY).rgb()) >= 140;

    if (!overlayOptions_.enabled || !image_->pixels) {
        return result;
    }

    QString text;
    if (image_->metadata.kind == application::ImageKind::Standard) {
        const auto info = application::queryPixelInfo(
            *image_, displayMapping_,
            static_cast<std::uint64_t>(x),
            static_cast<std::uint64_t>(y));
        if (info.valid && info.rgbValid) {
            result.rgbValue = true;
            result.rgbTexts[0].setText(
                QStringLiteral("R %1").arg(info.red));
            result.rgbTexts[1].setText(
                QStringLiteral("G %1").arg(info.green));
            result.rgbTexts[2].setText(
                QStringLiteral("B %1").arg(info.blue));
            for (auto& line : result.rgbTexts) {
                line.setPerformanceHint(QStaticText::AggressiveCaching);
            }
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
    result.valueText.setText(text);
    result.valueText.setPerformanceHint(QStaticText::AggressiveCaching);
    return result;
}

void ImageViewport::ensurePixelOverlayCache(qint64 left,
                                            qint64 top,
                                            qint64 right,
                                            qint64 bottom) {
    const bool compatible = overlayCacheImage_ == image_.get() &&
        overlayCacheMapping_ == displayMapping_ &&
        overlayCacheIncludesValues_ == overlayOptions_.enabled;
    if (compatible && left >= overlayCacheLeft_ && top >= overlayCacheTop_ &&
        right <= overlayCacheRight_ && bottom <= overlayCacheBottom_) {
        return;
    }

    const qint64 imageWidth = static_cast<qint64>(image_->metadata.width);
    const qint64 imageHeight = static_cast<qint64>(image_->metadata.height);
    constexpr qint64 margin = 2;
    const qint64 nextLeft = std::max<qint64>(0, left - margin);
    const qint64 nextTop = std::max<qint64>(0, top - margin);
    const qint64 nextRight = std::min(imageWidth, right + margin);
    const qint64 nextBottom = std::min(imageHeight, bottom + margin);
    const qint64 nextWidth = nextRight - nextLeft;
    const qint64 nextHeight = nextBottom - nextTop;

    std::vector<CachedOverlayCell> nextCache;
    nextCache.reserve(static_cast<std::size_t>(nextWidth * nextHeight));
    for (qint64 y = nextTop; y < nextBottom; ++y) {
        for (qint64 x = nextLeft; x < nextRight; ++x) {
            if (compatible && x >= overlayCacheLeft_ && x < overlayCacheRight_ &&
                y >= overlayCacheTop_ && y < overlayCacheBottom_) {
                const auto oldWidth = overlayCacheRight_ - overlayCacheLeft_;
                const auto oldIndex = static_cast<std::size_t>(
                    (y - overlayCacheTop_) * oldWidth + (x - overlayCacheLeft_));
                nextCache.push_back(std::move(overlayCellCache_[oldIndex]));
            } else {
                nextCache.push_back(makeOverlayCell(x, y));
            }
        }
    }

    overlayCellCache_ = std::move(nextCache);
    overlayCacheImage_ = image_.get();
    overlayCacheMapping_ = displayMapping_;
    overlayCacheLeft_ = nextLeft;
    overlayCacheTop_ = nextTop;
    overlayCacheRight_ = nextRight;
    overlayCacheBottom_ = nextBottom;
    overlayCacheIncludesValues_ = overlayOptions_.enabled;
}

ImageViewport::CachedOverlayCell* ImageViewport::cachedOverlayCell(
    qint64 x,
    qint64 y) {
    if (x < overlayCacheLeft_ || x >= overlayCacheRight_ ||
        y < overlayCacheTop_ || y >= overlayCacheBottom_) {
        return nullptr;
    }
    const auto cacheWidth = overlayCacheRight_ - overlayCacheLeft_;
    const auto index = static_cast<std::size_t>(
        (y - overlayCacheTop_) * cacheWidth + (x - overlayCacheLeft_));
    return &overlayCellCache_[index];
}

void ImageViewport::updateNavigationGeometry() {
    const QRect canvas = canvasRect();
    horizontalScrollBar_->setGeometry(
        canvas.left(), height() - scrollBarExtent,
        canvas.width(), scrollBarExtent);
    verticalScrollBar_->setGeometry(
        canvas.right() + 1, canvas.top(),
        scrollBarExtent, canvas.height());
    horizontalScrollBar_->raise();
    verticalScrollBar_->raise();
}

void ImageViewport::updateScrollBars() {
    if (!horizontalScrollBar_ || !verticalScrollBar_) {
        return;
    }
    const QSignalBlocker horizontalBlock(horizontalScrollBar_);
    const QSignalBlocker verticalBlock(verticalScrollBar_);
    updatingScrollBars_ = true;
    const QRect canvas = canvasRect();
    const bool valid = image_ && !canvas.isEmpty() && zoom_ > 0.0;
    const double displayWidth = valid
        ? static_cast<double>(image_->metadata.width) * zoom_ : 0.0;
    const double displayHeight = valid
        ? static_cast<double>(image_->metadata.height) * zoom_ : 0.0;

    const auto configure = [](QScrollBar* scrollBar,
                              double contentExtent,
                              int viewportExtent,
                              double leadingOffset,
                              int canvasLeading) {
        const double overflow = std::max(
            0.0, contentExtent - static_cast<double>(viewportExtent));
        if (overflow <= 0.5) {
            scrollBar->setRange(0, 0);
            scrollBar->setPageStep(1);
            scrollBar->setEnabled(false);
            return;
        }
        const int range = static_cast<int>(std::min(
            static_cast<double>(maximumScrollRange),
            std::max(1.0, std::ceil(overflow))));
        const double ratio = std::clamp(
            (static_cast<double>(canvasLeading) - leadingOffset) / overflow,
            0.0, 1.0);
        scrollBar->setRange(0, range);
        scrollBar->setPageStep(std::max(
            1, static_cast<int>(std::round(
                range * static_cast<double>(viewportExtent) / contentExtent))));
        scrollBar->setSingleStep(std::max(
            1, static_cast<int>(std::round(range / overflow))));
        scrollBar->setValue(static_cast<int>(std::round(ratio * range)));
        scrollBar->setEnabled(true);
    };

    configure(horizontalScrollBar_, displayWidth, canvas.width(),
              offset_.x(), canvas.left());
    configure(verticalScrollBar_, displayHeight, canvas.height(),
              offset_.y(), canvas.top());
    updatingScrollBars_ = false;
}

void ImageViewport::applyHorizontalScroll(int value) {
    if (updatingScrollBars_ || !image_ || horizontalScrollBar_->maximum() <= 0) {
        return;
    }
    const QRect canvas = canvasRect();
    const double overflow = std::max(
        0.0, static_cast<double>(image_->metadata.width) * zoom_ -
            canvas.width());
    offset_.setX(canvas.left() - overflow * value /
        horizontalScrollBar_->maximum());
    fitMode_ = false;
    update();
}

void ImageViewport::applyVerticalScroll(int value) {
    if (updatingScrollBars_ || !image_ || verticalScrollBar_->maximum() <= 0) {
        return;
    }
    const QRect canvas = canvasRect();
    const double overflow = std::max(
        0.0, static_cast<double>(image_->metadata.height) * zoom_ -
            canvas.height());
    offset_.setY(canvas.top() - overflow * value /
        verticalScrollBar_->maximum());
    fitMode_ = false;
    update();
}

void ImageViewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    loadingOverlay_->setGeometry(rect());
    updateNavigationGeometry();
    if (fitMode_) {
        fitToWindow();
    } else {
        updateScrollBars();
    }
    if (loadingOverlay_->isVisible()) {
        loadingOverlay_->raise();
    }
}

void ImageViewport::mousePressEvent(QMouseEvent* event) {
    if (!image_ || !canvasRect().contains(event->position().toPoint())) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        beginPan(event->position(), event->button());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (statisticsTool_ != StatisticsSelectionTool::None) {
            const QPoint selected = boundedImagePixel(event->position());
            if (selected.x() < 0 || selected.y() < 0) {
                return;
            }
            statisticsStart_ = selected;
            statisticsEnd_ = selected;
            statisticsSelectionStarted_ = true;
            statisticsSelectionVisible_ = true;
            update();
            event->accept();
            return;
        }
        beginPan(event->position(), event->button());
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
        const QRect canvas = canvasRect();
        const double displayWidth = image_->metadata.width * zoom_;
        const double displayHeight = image_->metadata.height * zoom_;
        if (displayWidth > canvas.width()) {
            offset_.setX(std::clamp(
                offset_.x(), canvas.right() + 1.0 - displayWidth,
                static_cast<double>(canvas.left())));
        }
        if (displayHeight > canvas.height()) {
            offset_.setY(std::clamp(
                offset_.y(), canvas.bottom() + 1.0 - displayHeight,
                static_cast<double>(canvas.top())));
        }
        updateScrollBars();
        update();
    }
    publishCoordinate(event->position());
}

void ImageViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (statisticsSelectionStarted_ &&
            statisticsTool_ != StatisticsSelectionTool::None) {
            const QPoint selected = boundedImagePixel(event->position());
            if (selected.x() >= 0 && selected.y() >= 0) {
                statisticsEnd_ = selected;
            }
            statisticsSelectionStarted_ = false;
            emit statisticsSelectionCompleted(
                statisticsStart_.x(),
                statisticsStart_.y(),
                statisticsEnd_.x(),
                statisticsEnd_.y(),
                statisticsTool_ == StatisticsSelectionTool::Line);
            update();
            event->accept();
            return;
        }
    }
    if (dragging_ && event->button() == draggingButton_) {
        finishPan();
        event->accept();
    }
}

void ImageViewport::beginPan(const QPointF& position,
                             Qt::MouseButton button) {
    dragging_ = true;
    draggingButton_ = button;
    fitMode_ = false;
    lastMouse_ = position;
    setCursor(Qt::ClosedHandCursor);
}

void ImageViewport::finishPan() {
    dragging_ = false;
    draggingButton_ = Qt::NoButton;
    if (statisticsTool_ == StatisticsSelectionTool::None) {
        unsetCursor();
    } else {
        setCursor(Qt::CrossCursor);
    }
}

void ImageViewport::wheelEvent(QWheelEvent* event) {
    if (!image_ || !canvasRect().contains(event->position().toPoint())) {
        QWidget::wheelEvent(event);
        return;
    }
    const QPointF anchor = event->position();
    const QPointF sourceAnchor = imageCoordinate(anchor);
    const double factor = std::pow(1.0015, event->angleDelta().y());
    zoom_ = std::clamp(zoom_ * factor, 0.0001, 256.0);
    offset_ = anchor - sourceAnchor * zoom_;
    fitMode_ = false;
    updateScrollBars();
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
    if (!image_ || !canvasRect().contains(widgetPoint.toPoint())) {
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
    draggingButton_ = Qt::NoButton;
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
    if (!image_ || image_->metadata.width == 0 || image_->metadata.height == 0 ||
        !canvasRect().contains(widgetPoint.toPoint())) {
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
