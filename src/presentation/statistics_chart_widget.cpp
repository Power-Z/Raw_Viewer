#include "presentation/statistics_chart_widget.h"

#include <QFontMetrics>
#include <QLineF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <limits>

namespace rawviewer::presentation {
namespace {

QString tickText(double value) {
    const double magnitude = std::abs(value);
    if ((magnitude >= 100000.0) ||
        (magnitude > 0.0 && magnitude < 0.001)) {
        return QLocale().toString(value, 'e', 2);
    }
    return QLocale().toString(value, 'f', magnitude < 10.0 ? 2 : 0);
}

} // namespace

StatisticsChartWidget::StatisticsChartWidget(QWidget* parent)
    : QWidget(parent),
      emptyMessage_(tr("在图像上完成区域选择后显示结果")) {
    setMinimumSize(240, 120);
    setAutoFillBackground(false);
    setMouseTracking(true);
}

void StatisticsChartWidget::setResult(
    const application::PixelStatisticsResult& result) {
    result_ = result;
    hoverActive_ = false;
    update();
}

void StatisticsChartWidget::clear(const QString& message) {
    result_.reset();
    hoverActive_ = false;
    emptyMessage_ = message.isEmpty()
        ? tr("在图像上完成区域选择后显示结果")
        : message;
    update();
}

void StatisticsChartWidget::setShowGrid(bool enabled) {
    showGrid_ = enabled;
    update();
}

void StatisticsChartWidget::setShowPoints(bool enabled) {
    showPoints_ = enabled;
    update();
}

void StatisticsChartWidget::setFillHistogram(bool enabled) {
    fillHistogram_ = enabled;
    update();
}

void StatisticsChartWidget::setLineWidth(int width) {
    lineWidth_ = std::clamp(width, 1, 5);
    update();
}

void StatisticsChartWidget::mouseMoveEvent(QMouseEvent* event) {
    hoverPosition_ = event->position();
    hoverActive_ = true;
    update();
    QWidget::mouseMoveEvent(event);
}

void StatisticsChartWidget::leaveEvent(QEvent* event) {
    hoverActive_ = false;
    update();
    QWidget::leaveEvent(event);
}

void StatisticsChartWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto background = palette().color(QPalette::Base);
    painter.fillRect(rect(), background);

    if (!result_ || result_->plot.x.empty() || result_->plot.y.empty()) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
                         emptyMessage_);
        return;
    }

    const int leftMargin = 76;
    const int rightMargin = 28;
    const int topMargin = 26;
    const int bottomMargin = 52;
    const QRectF plot(leftMargin,
                      topMargin,
                      std::max(1, width() - leftMargin - rightMargin),
                      std::max(1, height() - topMargin - bottomMargin));
    painter.fillRect(plot, QColor(207, 207, 207));
    const auto& xs = result_->plot.x;
    const auto& ys = result_->plot.y;
    const auto [xMinimumIt, xMaximumIt] =
        std::minmax_element(xs.begin(), xs.end());
    const auto [yMinimumIt, yMaximumIt] =
        std::minmax_element(ys.begin(), ys.end());
    double xMinimum = *xMinimumIt;
    double xMaximum = *xMaximumIt;
    double yMinimum = result_->plot.histogram ? 0.0 : *yMinimumIt;
    double yMaximum = *yMaximumIt;
    if (xMaximum <= xMinimum) {
        xMaximum = xMinimum + 1.0;
    }
    if (yMaximum <= yMinimum) {
        yMaximum = yMinimum + 1.0;
    } else if (!result_->plot.histogram) {
        const double padding = (yMaximum - yMinimum) * 0.06;
        yMinimum -= padding;
        yMaximum += padding;
    }

    const auto mapX = [&](double value) {
        return plot.left() +
            (value - xMinimum) / (xMaximum - xMinimum) * plot.width();
    };
    const auto mapY = [&](double value) {
        return plot.bottom() -
            (value - yMinimum) / (yMaximum - yMinimum) * plot.height();
    };

    painter.save();
    painter.setClipRect(plot.adjusted(-1, -1, 1, 1));
    if (showGrid_) {
        QPen gridPen(QColor(145, 145, 145, 150));
        gridPen.setCosmetic(true);
        gridPen.setWidthF(0.5);
        gridPen.setStyle(Qt::DashLine);
        painter.setPen(gridPen);
        for (int tick = 0; tick <= 5; ++tick) {
            const double fraction = tick / 5.0;
            painter.drawLine(QPointF(plot.left(),
                                     plot.top() + fraction * plot.height()),
                             QPointF(plot.right(),
                                     plot.top() + fraction * plot.height()));
            painter.drawLine(QPointF(plot.left() + fraction * plot.width(),
                                     plot.top()),
                             QPointF(plot.left() + fraction * plot.width(),
                                     plot.bottom()));
        }
    }

    // Thin antialiased black strokes are blended into gray on the fixed plot
    // background. Keep data strokes aliased so the visible center pixel stays
    // pure black even at the compact sub-pixel widths requested by the UI.
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor plotLine(Qt::black);
    if (result_->plot.histogram) {
        const double barWidth = plot.width() /
            static_cast<double>(std::max<std::size_t>(1, ys.size()));
        QPen outline(plotLine);
        outline.setCosmetic(true);
        outline.setWidthF(std::max(1.0, static_cast<double>(lineWidth_) * 0.5));
        painter.setPen(outline);
        painter.setBrush(fillHistogram_
            ? QColor(0, 0, 0, 55)
            : Qt::NoBrush);
        for (std::size_t index = 0; index < ys.size(); ++index) {
            const double left = plot.left() + index * barWidth;
            painter.drawRect(QRectF(left,
                                    mapY(ys[index]),
                                    std::max(1.0, barWidth),
                                    plot.bottom() - mapY(ys[index])));
        }
    } else {
        QPainterPath path;
        const std::size_t maximumPoints = static_cast<std::size_t>(
            std::max(512.0, plot.width() * 2.0));
        const std::size_t step = std::max<std::size_t>(
            1, (ys.size() + maximumPoints - 1) / maximumPoints);
        bool started = false;
        for (std::size_t begin = 0; begin < ys.size(); begin += step) {
            const std::size_t end = std::min(ys.size(), begin + step);
            auto minimum = begin;
            auto maximum = begin;
            for (std::size_t index = begin + 1; index < end; ++index) {
                if (ys[index] < ys[minimum]) minimum = index;
                if (ys[index] > ys[maximum]) maximum = index;
            }
            const std::size_t first = std::min(minimum, maximum);
            const std::size_t second = std::max(minimum, maximum);
            for (const auto index : {first, second}) {
                const QPointF point(mapX(xs[index]), mapY(ys[index]));
                if (!started) {
                    path.moveTo(point);
                    started = true;
                } else {
                    path.lineTo(point);
                }
            }
        }
        QPen linePen(plotLine);
        linePen.setCosmetic(true);
        linePen.setWidthF(std::max(1.0,
                                   static_cast<double>(lineWidth_) * 0.75));
        linePen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(linePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        if (showPoints_ && ys.size() <= 2000) {
            painter.setBrush(plotLine);
            for (std::size_t index = 0; index < ys.size(); ++index) {
                painter.drawEllipse(QPointF(mapX(xs[index]), mapY(ys[index])),
                                    2.5,
                                    2.5);
            }
        }
    }

    const std::size_t sampleCount = std::min(xs.size(), ys.size());
    if (hoverActive_ && plot.contains(hoverPosition_) && sampleCount > 0) {
        const double hoveredX = xMinimum +
            (hoverPosition_.x() - plot.left()) / plot.width() *
                (xMaximum - xMinimum);
        const auto end = xs.begin() + static_cast<std::ptrdiff_t>(sampleCount);
        const auto found = std::lower_bound(xs.begin(), end, hoveredX);
        const auto foundIndex = static_cast<std::size_t>(found - xs.begin());
        QPointF nearestPoint;
        double nearestDistance = std::numeric_limits<double>::max();
        if (!result_->plot.histogram && sampleCount > 1) {
            std::array<std::size_t, 2> segmentStarts{
                foundIndex > 0 ? foundIndex - 1 : sampleCount,
                foundIndex < sampleCount - 1 ? foundIndex : sampleCount};
            for (const auto start : segmentStarts) {
                if (start >= sampleCount - 1) continue;
                const QPointF a(mapX(xs[start]), mapY(ys[start]));
                const QPointF b(mapX(xs[start + 1]), mapY(ys[start + 1]));
                const double dx = b.x() - a.x();
                const double dy = b.y() - a.y();
                const double lengthSquared = dx * dx + dy * dy;
                const double projection = lengthSquared > 0.0
                    ? ((hoverPosition_.x() - a.x()) * dx +
                       (hoverPosition_.y() - a.y()) * dy) / lengthSquared
                    : 0.0;
                const double t = std::clamp(projection, 0.0, 1.0);
                const QPointF point(a.x() + t * dx, a.y() + t * dy);
                const double distance = QLineF(point, hoverPosition_).length();
                if (distance < nearestDistance) {
                    nearestPoint = point;
                    nearestDistance = distance;
                }
            }
        } else {
            const std::array<std::size_t, 2> candidates{
                foundIndex < sampleCount ? foundIndex : sampleCount,
                foundIndex > 0 ? foundIndex - 1 : sampleCount};
            for (const auto index : candidates) {
                if (index >= sampleCount) continue;
                const QPointF point(mapX(xs[index]), mapY(ys[index]));
                const double distance = QLineF(point, hoverPosition_).length();
                if (distance < nearestDistance) {
                    nearestPoint = point;
                    nearestDistance = distance;
                }
            }
        }
        if (nearestDistance <= 14.0) {
            QPen guidePen(Qt::black);
            guidePen.setCosmetic(true);
            guidePen.setWidthF(1.0);
            guidePen.setStyle(Qt::SolidLine);
            painter.setPen(guidePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF(plot.left(), nearestPoint.y()),
                             nearestPoint);
            painter.drawLine(nearestPoint,
                             QPointF(nearestPoint.x(), plot.bottom()));
            painter.setPen(QPen(Qt::black, 0.75));
            painter.setBrush(QColor(207, 207, 207));
            painter.drawEllipse(nearestPoint, 3.0, 3.0);
        }
    }
    painter.restore();

    QPen axisPen(palette().color(QPalette::Text));
    axisPen.setCosmetic(true);
    axisPen.setWidthF(0.5);
    painter.setPen(axisPen);
    painter.drawRect(plot);
    const QFontMetrics metrics(painter.font());
    for (int tick = 0; tick <= 5; ++tick) {
        const double fraction = tick / 5.0;
        const double xValue = xMinimum + fraction * (xMaximum - xMinimum);
        const QString xText = tickText(xValue);
        const double x = plot.left() + fraction * plot.width();
        painter.drawText(QRectF(x - 45,
                                plot.bottom() + 8,
                                90,
                                metrics.height()),
                         Qt::AlignHCenter | Qt::AlignTop,
                         xText);
        const double yValue = yMaximum - fraction * (yMaximum - yMinimum);
        painter.drawText(QRectF(2,
                                plot.top() + fraction * plot.height() -
                                    metrics.height() / 2.0,
                                leftMargin - 10,
                                metrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         tickText(yValue));
    }
    const QString xLabel = result_->plot.histogram
        ? tr("Raw signal value")
        : (result_->mode == application::PixelStatisticsMode::Line
               ? tr("Distance (px)")
               : tr("Position (px)"));
    painter.drawText(QRectF(plot.left(),
                            height() - metrics.height() - 4,
                            plot.width(),
                            metrics.height()),
                     Qt::AlignCenter,
                     xLabel);
    painter.save();
    painter.translate(18, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-plot.height() / 2.0,
                            -metrics.height() / 2.0,
                            plot.height(),
                            metrics.height()),
                     Qt::AlignCenter,
                     result_->plot.histogram ? tr("Pixel count")
                                             : tr("Raw signal"));
    painter.restore();
}

} // namespace rawviewer::presentation
