#include "presentation/histogram_widget.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

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
        for (std::size_t index = 0; index + 3 < preview.rgba.size(); index += 4) {
            const auto gray = static_cast<std::uint8_t>(
                (static_cast<unsigned>(preview.rgba[index]) +
                 preview.rgba[index + 1] +
                 preview.rgba[index + 2]) / 3);
            ++bins_[gray];
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
