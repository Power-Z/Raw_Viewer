#include "presentation/histogram_widget.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace rawviewer::presentation {

HistogramWidget::HistogramWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(120);
}

void HistogramWidget::setImage(
    std::shared_ptr<const application::DecodedImage> image) {
    bins_.fill(0);
    maximum_ = 0;
    if (image) {
        const auto& preview = image->preview;
        if (preview.hasGrayscale16()) {
            constexpr std::uint64_t maximumSamples = 4'000'000;
            const auto totalPixels =
                static_cast<std::uint64_t>(preview.width) * preview.height;
            const auto step = std::max<std::uint64_t>(
                1,
                static_cast<std::uint64_t>(std::ceil(
                    std::sqrt(static_cast<double>(totalPixels) /
                              maximumSamples))));
            for (std::uint64_t y = 0;
                 y < static_cast<std::uint64_t>(preview.height);
                 y += step) {
                const auto row = y * preview.grayscale16StrideSamples;
                for (std::uint64_t x = 0;
                    x < static_cast<std::uint64_t>(preview.width);
                     x += step) {
                    const auto gray = static_cast<std::uint8_t>(
                        preview.grayscale16Pixels[row + x] >> 8);
                    ++bins_[gray];
                }
            }
        } else {
            for (std::size_t index = 0;
                 index + 3 < preview.rgba.size();
                 index += 4) {
                const auto gray = static_cast<std::uint8_t>(
                    (static_cast<unsigned>(preview.rgba[index]) +
                     preview.rgba[index + 1] +
                     preview.rgba[index + 2]) / 3);
                ++bins_[gray];
            }
        }
        maximum_ = *std::max_element(bins_.begin(), bins_.end());
    }
    update();
}

void HistogramWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    if (maximum_ == 0) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, tr("预览直方图"));
        return;
    }

    QPainterPath path;
    const double xScale = static_cast<double>(width() - 2) / 255.0;
    const double drawableHeight = height() - 4.0;
    for (std::size_t index = 0; index < bins_.size(); ++index) {
        const double x = 1.0 + index * xScale;
        const double y = height() - 2.0 -
            (static_cast<double>(bins_[index]) / maximum_) * drawableHeight;
        if (index == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
    painter.drawPath(path);
}

} // namespace rawviewer::presentation
