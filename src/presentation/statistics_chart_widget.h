#pragma once

#include "application/pixel_statistics.h"

#include <QWidget>

#include <optional>

class QMouseEvent;

namespace rawviewer::presentation {

class StatisticsChartWidget final : public QWidget {
    Q_OBJECT

public:
    explicit StatisticsChartWidget(QWidget* parent = nullptr);

    void setResult(const application::PixelStatisticsResult& result);
    void clear(const QString& message = {});
    void setShowGrid(bool enabled);
    void setShowPoints(bool enabled);
    void setFillHistogram(bool enabled);
    void setLineWidth(int width);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    std::optional<application::PixelStatisticsResult> result_;
    QString emptyMessage_;
    bool showGrid_ = true;
    bool showPoints_ = false;
    bool fillHistogram_ = true;
    int lineWidth_ = 1;
    QPointF hoverPosition_;
    bool hoverActive_ = false;
};

} // namespace rawviewer::presentation
