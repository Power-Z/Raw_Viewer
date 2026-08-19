#pragma once

#include "application/global_histogram.h"

#include <QWidget>

namespace rawviewer::presentation {

class HistogramWidget final : public QWidget {
    Q_OBJECT

public:
    explicit HistogramWidget(QWidget* parent = nullptr);

    void setLoading(bool loading);
    void setResult(application::GlobalHistogramResult result);
    void setDisplayWindow(double blackPoint, double whitePoint);
    void setZoomed(bool enabled);

    application::GlobalHistogramMode mode() const noexcept {
        return result_.mode;
    }
    std::size_t seriesCount() const noexcept { return result_.series.size(); }
    double blackPoint() const noexcept { return blackPoint_; }
    double whitePoint() const noexcept { return whitePoint_; }
    bool isZoomed() const noexcept { return zoomed_; }
    std::uint64_t projectionBuildCount() const noexcept {
        return projectionBuildCount_;
    }

signals:
    void windowEditStarted();
    void displayWindowChanged(double blackPoint, double whitePoint);
    void windowEditFinished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class ActiveHandle { None, Black, White };

    QRectF plotRect() const noexcept;
    double viewMinimum() const noexcept;
    double viewMaximum() const noexcept;
    double valueToX(double value) const noexcept;
    double xToValue(double x) const noexcept;
    void updateHandleFromX(double x);
    void changeActiveHandle(double delta);
    void invalidateProjection();
    void ensureProjection(int columnCount);

    application::GlobalHistogramResult result_;
    bool loading_ = false;
    bool zoomed_ = false;
    double blackPoint_ = 0.0;
    double whitePoint_ = 255.0;
    double zoomMinimum_ = 0.0;
    double zoomMaximum_ = 255.0;
    ActiveHandle activeHandle_ = ActiveHandle::None;
    std::vector<std::vector<double>> projectedColumns_;
    double projectedMaximum_ = 0.0;
    double projectedMinimumValue_ = 0.0;
    double projectedMaximumValue_ = 0.0;
    int projectedColumnCount_ = 0;
    std::uint64_t projectionBuildCount_ = 0;
};

} // namespace rawviewer::presentation
