#include "presentation/histogram_widget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace rawviewer::presentation {
namespace {

QColor componentColor(application::GlobalHistogramComponent component) {
    using application::GlobalHistogramComponent;
    switch (component) {
    case GlobalHistogramComponent::Red: return {239, 76, 82};
    case GlobalHistogramComponent::BayerGreenRed: return {113, 224, 105};
    case GlobalHistogramComponent::Green: return {86, 220, 111};
    case GlobalHistogramComponent::BayerGreenBlue: return {54, 205, 181};
    case GlobalHistogramComponent::Blue: return {73, 133, 255};
    case GlobalHistogramComponent::Luminance: return {230, 234, 239};
    case GlobalHistogramComponent::Signal: return {191, 198, 207};
    }
    return {191, 198, 207};
}

QString componentName(application::GlobalHistogramComponent component) {
    using application::GlobalHistogramComponent;
    switch (component) {
    case GlobalHistogramComponent::Red: return QStringLiteral("R");
    case GlobalHistogramComponent::BayerGreenRed: return QStringLiteral("Gr");
    case GlobalHistogramComponent::Green: return QStringLiteral("G");
    case GlobalHistogramComponent::BayerGreenBlue: return QStringLiteral("Gb");
    case GlobalHistogramComponent::Blue: return QStringLiteral("B");
    case GlobalHistogramComponent::Luminance: return QStringLiteral("Y");
    case GlobalHistogramComponent::Signal: return QStringLiteral("Y");
    }
    return {};
}

QString modeName(application::GlobalHistogramMode mode) {
    using application::GlobalHistogramMode;
    switch (mode) {
    case GlobalHistogramMode::BayerChannels: return QStringLiteral("BAYER");
    case GlobalHistogramMode::RgbLuminance: return QStringLiteral("RGB");
    case GlobalHistogramMode::SingleChannel: return QStringLiteral("MONO");
    case GlobalHistogramMode::Unavailable: break;
    }
    return QStringLiteral("HISTOGRAM");
}

QString axisValue(double value, double span) {
    return QString::number(value, 'f', span < 100.0 ? 1 : 0);
}

} // namespace

HistogramWidget::HistogramWidget(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("globalHistogramWidget"));
    setMinimumHeight(190);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeHorCursor);
    result_.rangeMinimum = 0.0;
    result_.rangeMaximum = 255.0;
}

void HistogramWidget::setLoading(bool loading) {
    loading_ = loading;
    if (loading) {
        result_.mode = application::GlobalHistogramMode::Unavailable;
        result_.series.clear();
        invalidateProjection();
    }
    update();
}

void HistogramWidget::setResult(application::GlobalHistogramResult result) {
    loading_ = false;
    result_ = std::move(result);
    invalidateProjection();
    if (result_.rangeMaximum <= result_.rangeMinimum) {
        result_.rangeMinimum = 0.0;
        result_.rangeMaximum = 255.0;
    }
    blackPoint_ = std::clamp(
        blackPoint_, result_.rangeMinimum, result_.rangeMaximum);
    whitePoint_ = std::clamp(
        whitePoint_, result_.rangeMinimum, result_.rangeMaximum);
    if (whitePoint_ <= blackPoint_) {
        blackPoint_ = result_.rangeMinimum;
        whitePoint_ = result_.rangeMaximum;
    }
    if (zoomed_ && (blackPoint_ < zoomMinimum_ ||
                    whitePoint_ > zoomMaximum_)) {
        zoomMinimum_ = blackPoint_;
        zoomMaximum_ = whitePoint_;
        invalidateProjection();
    }
    update();
}

void HistogramWidget::setDisplayWindow(double blackPoint,
                                       double whitePoint) {
    blackPoint_ = std::clamp(
        blackPoint, result_.rangeMinimum, result_.rangeMaximum);
    whitePoint_ = std::clamp(
        whitePoint, result_.rangeMinimum, result_.rangeMaximum);
    if (whitePoint_ <= blackPoint_) {
        whitePoint_ = std::min(result_.rangeMaximum, blackPoint_ + 0.1);
    }
    if (zoomed_ && (blackPoint_ < zoomMinimum_ ||
                    whitePoint_ > zoomMaximum_)) {
        zoomMinimum_ = blackPoint_;
        zoomMaximum_ = whitePoint_;
        invalidateProjection();
    }
    update();
}

void HistogramWidget::setZoomed(bool enabled) {
    zoomed_ = enabled && whitePoint_ > blackPoint_;
    if (zoomed_) {
        zoomMinimum_ = blackPoint_;
        zoomMaximum_ = whitePoint_;
    }
    invalidateProjection();
    update();
}

void HistogramWidget::invalidateProjection() {
    projectedColumns_.clear();
    projectedMaximum_ = 0.0;
    projectedColumnCount_ = 0;
}

void HistogramWidget::ensureProjection(int columnCount) {
    const auto minimum = viewMinimum();
    const auto maximum = viewMaximum();
    if (!projectedColumns_.empty() &&
        projectedColumnCount_ == columnCount &&
        projectedMinimumValue_ == minimum &&
        projectedMaximumValue_ == maximum) {
        return;
    }
    projectedColumnCount_ = std::max(2, columnCount);
    ++projectionBuildCount_;
    projectedMinimumValue_ = minimum;
    projectedMaximumValue_ = maximum;
    projectedMaximum_ = 0.0;
    projectedColumns_.assign(
        result_.series.size(),
        std::vector<double>(projectedColumnCount_, 0.0));
    const double fullSpan =
        result_.rangeMaximum - result_.rangeMinimum;
    if (maximum <= minimum || fullSpan <= 0.0) return;

    for (std::size_t seriesIndex = 0;
         seriesIndex < result_.series.size(); ++seriesIndex) {
        const auto& series = result_.series[seriesIndex];
        if (series.bins.empty()) continue;
        const auto first = static_cast<std::size_t>(std::clamp(
            std::floor((minimum - result_.rangeMinimum) / fullSpan *
                       series.bins.size()),
            0.0, static_cast<double>(series.bins.size() - 1)));
        const auto last = static_cast<std::size_t>(std::clamp(
            std::ceil((maximum - result_.rangeMinimum) / fullSpan *
                      series.bins.size()),
            0.0, static_cast<double>(series.bins.size() - 1)));
        for (std::size_t index = first; index <= last; ++index) {
            const double value = result_.rangeMinimum +
                (static_cast<double>(index) + 0.5) /
                    series.bins.size() * fullSpan;
            if (value < minimum || value > maximum) continue;
            const auto column = static_cast<int>(std::clamp(
                (value - minimum) / (maximum - minimum) *
                    projectedColumnCount_,
                0.0, static_cast<double>(projectedColumnCount_ - 1)));
            projectedColumns_[seriesIndex][column] += series.bins[index];
        }
        for (const auto count : projectedColumns_[seriesIndex]) {
            projectedMaximum_ = std::max(
                projectedMaximum_, std::log1p(count));
        }
    }
}

QRectF HistogramWidget::plotRect() const noexcept {
    return QRectF(rect()).adjusted(9.0, 29.0, -9.0, -35.0);
}

double HistogramWidget::viewMinimum() const noexcept {
    return zoomed_ ? zoomMinimum_ : result_.rangeMinimum;
}

double HistogramWidget::viewMaximum() const noexcept {
    return zoomed_ ? zoomMaximum_ : result_.rangeMaximum;
}

double HistogramWidget::valueToX(double value) const noexcept {
    const auto plot = plotRect();
    const double span = viewMaximum() - viewMinimum();
    if (span <= 0.0) return plot.left();
    return plot.left() + std::clamp(
        (value - viewMinimum()) / span, 0.0, 1.0) * plot.width();
}

double HistogramWidget::xToValue(double x) const noexcept {
    const auto plot = plotRect();
    if (plot.width() <= 0.0) return viewMinimum();
    const double fraction = std::clamp(
        (x - plot.left()) / plot.width(), 0.0, 1.0);
    return viewMinimum() + fraction * (viewMaximum() - viewMinimum());
}

void HistogramWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(25, 27, 30));

    const auto plot = plotRect();
    painter.fillRect(plot, QColor(18, 20, 23));
    painter.setPen(QPen(QColor(68, 72, 78), 1.0));
    painter.drawRect(plot);
    painter.setPen(QPen(QColor(48, 52, 57), 1.0));
    for (int division = 1; division < 4; ++division) {
        const double x = plot.left() + plot.width() * division / 4.0;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    for (int division = 1; division < 3; ++division) {
        const double y = plot.top() + plot.height() * division / 3.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    painter.setFont(QFont(font().family(), 8, QFont::DemiBold));
    painter.setPen(QColor(176, 181, 188));
    painter.drawText(QRectF(10, 5, 80, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     modeName(result_.mode));
    painter.setFont(QFont(font().family(), 7));
    painter.setPen(QColor(116, 122, 130));
    painter.drawText(QRectF(width() - 100, 5, 90, 18),
                     Qt::AlignRight | Qt::AlignVCenter,
                     zoomed_ ? tr("WINDOW DETAIL") : tr("FULL IMAGE"));

    double legendX = 82.0;
    for (const auto& series : result_.series) {
        const auto color = componentColor(series.component);
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(legendX, 14.0), 2.7, 2.7);
        painter.setPen(color);
        painter.drawText(QRectF(legendX + 5.0, 5.0, 23.0, 18.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         componentName(series.component));
        legendX += 31.0;
    }

    if (loading_) {
        painter.setPen(QColor(181, 186, 193));
        painter.drawText(plot, Qt::AlignCenter, tr("计算全图统计…"));
    } else if (result_.series.empty()) {
        painter.setPen(QColor(134, 140, 148));
        painter.drawText(plot, Qt::AlignCenter, tr("暂无全图统计"));
    } else {
        const int columnCount = std::max(2, static_cast<int>(plot.width()));
        ensureProjection(columnCount);
        if (projectedMaximum_ > 0.0) {
            painter.save();
            painter.setClipRect(plot.adjusted(1, 1, -1, -1));
            for (std::size_t seriesIndex = 0;
                 seriesIndex < result_.series.size(); ++seriesIndex) {
                const auto& series = result_.series[seriesIndex];
                QPainterPath line;
                for (int column = 0; column < columnCount; ++column) {
                    const double x = plot.left() +
                        static_cast<double>(column) / (columnCount - 1) *
                            plot.width();
                    const double y = plot.bottom() -
                        std::log1p(projectedColumns_[seriesIndex][column]) /
                            projectedMaximum_ * (plot.height() - 3.0);
                    if (column == 0) line.moveTo(x, y);
                    else line.lineTo(x, y);
                }
                QPainterPath fill = line;
                fill.lineTo(line.currentPosition().x(), plot.bottom());
                fill.lineTo(line.elementAt(0).x, plot.bottom());
                fill.closeSubpath();
                auto color = componentColor(series.component);
                auto fillColor = color;
                fillColor.setAlpha(38);
                painter.fillPath(fill, fillColor);
                painter.setPen(QPen(color, 1.35));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(line);
            }
            painter.restore();
        }
    }

    const double blackX = valueToX(blackPoint_);
    const double whiteX = valueToX(whitePoint_);
    if (!zoomed_) {
        painter.fillRect(QRectF(plot.left(), plot.top(),
                                std::max(0.0, blackX - plot.left()),
                                plot.height()),
                         QColor(0, 0, 0, 42));
        painter.fillRect(QRectF(whiteX, plot.top(),
                                std::max(0.0, plot.right() - whiteX),
                                plot.height()),
                         QColor(255, 255, 255, 20));
    }
    painter.setPen(QPen(QColor(110, 183, 255), 1.2));
    painter.drawLine(QPointF(blackX, plot.top()),
                     QPointF(blackX, plot.bottom() + 7.0));
    painter.setPen(QPen(QColor(245, 247, 250), 1.2));
    painter.drawLine(QPointF(whiteX, plot.top()),
                     QPointF(whiteX, plot.bottom() + 7.0));
    painter.setPen(QPen(QColor(94, 99, 106), 2.0));
    painter.drawLine(QPointF(plot.left(), plot.bottom() + 7.0),
                     QPointF(plot.right(), plot.bottom() + 7.0));

    const auto drawHandle = [&](double x, QColor color) {
        QPainterPath handle;
        handle.moveTo(x - 5.0, plot.bottom() + 8.0);
        handle.lineTo(x + 5.0, plot.bottom() + 8.0);
        handle.lineTo(x, plot.bottom() + 16.0);
        handle.closeSubpath();
        painter.fillPath(handle, color);
    };
    drawHandle(blackX, QColor(110, 183, 255));
    drawHandle(whiteX, QColor(245, 247, 250));

    painter.setFont(QFont(font().family(), 7));
    painter.setPen(QColor(139, 145, 153));
    const double span = viewMaximum() - viewMinimum();
    painter.drawText(QRectF(plot.left(), plot.bottom() + 18.0, 80, 14),
                     Qt::AlignLeft,
                     axisValue(viewMinimum(), span));
    painter.drawText(QRectF(plot.right() - 80, plot.bottom() + 18.0, 80, 14),
                     Qt::AlignRight,
                     axisValue(viewMaximum(), span));
}

void HistogramWidget::mousePressEvent(QMouseEvent* event) {
    const QRectF interactionArea = plotRect().adjusted(0, 0, 0, 18.0);
    if (event->button() != Qt::LeftButton || result_.series.empty() ||
        !interactionArea.contains(event->position())) {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    const double blackDistance = std::abs(event->position().x() -
                                          valueToX(blackPoint_));
    const double whiteDistance = std::abs(event->position().x() -
                                          valueToX(whitePoint_));
    activeHandle_ = blackDistance <= whiteDistance
        ? ActiveHandle::Black : ActiveHandle::White;
    emit windowEditStarted();
    updateHandleFromX(event->position().x());
    event->accept();
}

void HistogramWidget::mouseMoveEvent(QMouseEvent* event) {
    if (activeHandle_ == ActiveHandle::None) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    updateHandleFromX(event->position().x());
    event->accept();
}

void HistogramWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton &&
        activeHandle_ != ActiveHandle::None) {
        updateHandleFromX(event->position().x());
        activeHandle_ = ActiveHandle::None;
        emit windowEditFinished();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void HistogramWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() != Qt::Key_Left && event->key() != Qt::Key_Right) {
        QWidget::keyPressEvent(event);
        return;
    }
    activeHandle_ = event->modifiers().testFlag(Qt::ShiftModifier)
        ? ActiveHandle::White : ActiveHandle::Black;
    const double step = std::max(
        0.1, (viewMaximum() - viewMinimum()) / 500.0);
    emit windowEditStarted();
    changeActiveHandle(event->key() == Qt::Key_Left ? -step : step);
    emit windowEditFinished();
    event->accept();
}

void HistogramWidget::updateHandleFromX(double x) {
    double value = std::round(xToValue(x) * 10.0) / 10.0;
    const double separation = 0.1;
    if (activeHandle_ == ActiveHandle::Black) {
        blackPoint_ = std::clamp(
            value, viewMinimum(), whitePoint_ - separation);
    } else if (activeHandle_ == ActiveHandle::White) {
        whitePoint_ = std::clamp(
            value, blackPoint_ + separation, viewMaximum());
    }
    emit displayWindowChanged(blackPoint_, whitePoint_);
    update();
}

void HistogramWidget::changeActiveHandle(double delta) {
    const double value = activeHandle_ == ActiveHandle::Black
        ? blackPoint_ + delta : whitePoint_ + delta;
    updateHandleFromX(valueToX(value));
}

} // namespace rawviewer::presentation
