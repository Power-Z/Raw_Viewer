#pragma once

#include "application/image_types.h"
#include "domain/display_mapping.h"
#include "presentation/pixel_overlay_options.h"

#include <QImage>
#include <QPointF>
#include <QWidget>

#include <memory>

class QLabel;
class QPainter;
class QProgressBar;

namespace rawviewer::presentation {

enum class StatisticsSelectionTool {
    None,
    Rectangle,
    Line
};

class ImageViewport final : public QWidget {
    Q_OBJECT

public:
    explicit ImageViewport(QWidget* parent = nullptr);

    void setImage(std::shared_ptr<const application::DecodedImage> image,
                  bool preserveView = false);
    void clearImage();
    void fitToWindow();
    void setDisplayMapping(const domain::DisplayMapping& mapping);
    void setPixelOverlayOptions(const PixelOverlayOptions& options);
    void setLoading(bool loading, const QString& message = {});
    void setStatisticsSelectionTool(StatisticsSelectionTool tool);
    void clearStatisticsSelection();
    double zoom() const noexcept { return zoom_; }

signals:
    void fileDropped(const QString& path);
    void imageCoordinateChanged(qint64 x, qint64 y, bool inside);
    void zoomChanged(double zoom);
    void statisticsSelectionCompleted(qint64 x0,
                                      qint64 y0,
                                      qint64 x1,
                                      qint64 y1,
                                      bool line);

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
    void drawPixelOverlay(QPainter& painter);
    void drawStatisticsSelection(QPainter& painter);
    QPoint boundedImagePixel(const QPointF& widgetPoint) const;

    std::shared_ptr<const application::DecodedImage> image_;
    QImage preview_;
    QWidget* loadingOverlay_ = nullptr;
    QLabel* loadingLabel_ = nullptr;
    QProgressBar* loadingProgress_ = nullptr;
    double zoom_ = 1.0;
    QPointF offset_;
    QPointF lastMouse_;
    domain::DisplayMapping displayMapping_;
    PixelOverlayOptions overlayOptions_;
    bool dragging_ = false;
    bool fitMode_ = true;
    StatisticsSelectionTool statisticsTool_ = StatisticsSelectionTool::None;
    bool statisticsSelectionStarted_ = false;
    bool statisticsSelectionVisible_ = false;
    QPoint statisticsStart_;
    QPoint statisticsEnd_;
};

} // namespace rawviewer::presentation
