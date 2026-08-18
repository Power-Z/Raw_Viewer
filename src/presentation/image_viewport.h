#pragma once

#include "application/image_types.h"
#include "domain/display_mapping.h"
#include "presentation/pixel_overlay_options.h"

#include <QImage>
#include <QPointF>
#include <QStaticText>
#include <QWidget>

#include <memory>
#include <vector>

class QLabel;
class QPainter;
class QProgressBar;
class QScrollBar;

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
    struct CachedOverlayCell {
        qint64 x = 0;
        qint64 y = 0;
        domain::BayerChannel channel = domain::BayerChannel::None;
        bool lightBackground = false;
        QStaticText valueText;
        int preparedValueFontPixels = 0;
    };

    QPointF imageCoordinate(const QPointF& widgetPoint) const;
    QRect canvasRect() const;
    void publishCoordinate(const QPointF& widgetPoint);
    void drawPixelOverlay(QPainter& painter);
    void drawStatisticsSelection(QPainter& painter);
    void drawRulers(QPainter& painter);
    void drawOverview(QPainter& painter);
    QPoint boundedImagePixel(const QPointF& widgetPoint) const;
    void beginPan(const QPointF& position, Qt::MouseButton button);
    void finishPan();
    void updateNavigationGeometry();
    void updateScrollBars();
    void applyHorizontalScroll(int value);
    void applyVerticalScroll(int value);
    void invalidatePixelOverlayCache();
    void ensurePixelOverlayCache(qint64 left,
                                 qint64 top,
                                 qint64 right,
                                 qint64 bottom);
    CachedOverlayCell makeOverlayCell(qint64 x, qint64 y) const;
    CachedOverlayCell* cachedOverlayCell(qint64 x, qint64 y);

    std::shared_ptr<const application::DecodedImage> image_;
    QImage preview_;
    QWidget* loadingOverlay_ = nullptr;
    QLabel* loadingLabel_ = nullptr;
    QProgressBar* loadingProgress_ = nullptr;
    QScrollBar* horizontalScrollBar_ = nullptr;
    QScrollBar* verticalScrollBar_ = nullptr;
    double zoom_ = 1.0;
    QPointF offset_;
    QPointF lastMouse_;
    domain::DisplayMapping displayMapping_;
    PixelOverlayOptions overlayOptions_;
    std::vector<CachedOverlayCell> overlayCellCache_;
    const application::DecodedImage* overlayCacheImage_ = nullptr;
    domain::DisplayMapping overlayCacheMapping_;
    qint64 overlayCacheLeft_ = 0;
    qint64 overlayCacheTop_ = 0;
    qint64 overlayCacheRight_ = 0;
    qint64 overlayCacheBottom_ = 0;
    bool overlayCacheIncludesValues_ = false;
    bool dragging_ = false;
    Qt::MouseButton draggingButton_ = Qt::NoButton;
    bool updatingScrollBars_ = false;
    bool fitMode_ = true;
    StatisticsSelectionTool statisticsTool_ = StatisticsSelectionTool::None;
    bool statisticsSelectionStarted_ = false;
    bool statisticsSelectionVisible_ = false;
    QPoint statisticsStart_;
    QPoint statisticsEnd_;
};

} // namespace rawviewer::presentation
