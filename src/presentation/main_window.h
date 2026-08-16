#pragma once

#include "application/bayer_extract.h"
#include "application/document_session.h"
#include "application/open_image_service.h"
#include "application/pixel_statistics.h"

#include <QMainWindow>

#include <atomic>
#include <memory>
#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QPushButton;
class QAction;
class QSpinBox;
class QTimer;
class QTreeView;

namespace rawviewer::presentation {

class HistogramWidget;
class ImageViewport;
class BayerExtractDialog;
class PixelInfoDialog;
class PixelStatisticsDialog;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
                        std::shared_ptr<const application::OpenImageService> openService,
                        std::shared_ptr<const application::IBayerPlaneExporter> bayerExporter,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

    void openPath(const QString& path);

private:
    QWidget* createLeftPanel();
    QWidget* createRightPanel();
    QWidget* createRawParametersPanel();
    QWidget* createFileBrowserPanel();
    void createMenus();
    void createStatusBar();
    domain::RawDescriptor currentRawDescriptor() const;
    void beginOpen(const QString& path);
    void showDecoded(std::shared_ptr<const application::DecodedImage> image);
    void setTheme(const QString& name);
    void updateCoordinate(qint64 x, qint64 y, bool inside);
    void flushCoordinateUpdate();
    void openPixelInfo();
    void openBayerExtract();
    void openPixelStatistics();
    void setStatisticsMode(application::PixelStatisticsMode mode);
    void beginPixelStatistics(const application::StatisticsSelection& selection);
    void cancelPixelStatistics();
    void beginBayerExtraction();
    void showOriginalImage();
    void exportBayerCsv();
    void applyDisplayControls();
    void commitDisplayEdit();
    void syncDisplayControls();
    void renderCurrentDisplay(bool preserveView = true);
    void updateUndoActions();

    std::shared_ptr<const application::OpenImageService> openService_;
    std::shared_ptr<const application::IBayerPlaneExporter> bayerExporter_;
    application::BayerExtractService bayerExtractService_;
    application::PixelStatisticsService pixelStatisticsService_;
    std::shared_ptr<const application::DecodedImage> currentImage_;
    std::shared_ptr<const application::DecodedImage> displaySource_;
    std::shared_ptr<application::BayerExtraction> bayerExtraction_;
    std::unique_ptr<application::DocumentSession> documentSession_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    std::shared_ptr<std::atomic_bool> bayerCancellation_;
    std::shared_ptr<std::atomic_bool> exportCancellation_;
    std::shared_ptr<std::atomic_bool> statisticsCancellation_;
    std::shared_ptr<std::atomic_uint32_t> statisticsProgress_;
    std::uint64_t generation_ = 0;
    std::uint64_t bayerGeneration_ = 0;
    std::uint64_t statisticsGeneration_ = 0;
    std::optional<application::StatisticsSelection> statisticsSelection_;

    ImageViewport* viewport_ = nullptr;
    HistogramWidget* histogram_ = nullptr;
    QFileSystemModel* fileModel_ = nullptr;
    QTreeView* fileTree_ = nullptr;
    QLineEdit* pathEdit_ = nullptr;
    QSpinBox* widthSpin_ = nullptr;
    QSpinBox* heightSpin_ = nullptr;
    QSpinBox* headerSpin_ = nullptr;
    QSpinBox* strideSpin_ = nullptr;
    QComboBox* scalarCombo_ = nullptr;
    QComboBox* endianCombo_ = nullptr;
    QComboBox* bayerCombo_ = nullptr;
    QDoubleSpinBox* sensorBlackSpin_ = nullptr;
    QDoubleSpinBox* blackPointSpin_ = nullptr;
    QDoubleSpinBox* whitePointSpin_ = nullptr;
    QDoubleSpinBox* gammaSpin_ = nullptr;
    QPushButton* resetDisplayButton_ = nullptr;
    QPushButton* pixelInfoButton_ = nullptr;
    QPushButton* bayerExtractButton_ = nullptr;
    QPushButton* statisticsButton_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* pixelInfoAction_ = nullptr;
    QAction* bayerExtractAction_ = nullptr;
    QAction* statisticsAction_ = nullptr;
    BayerExtractDialog* bayerExtractDialog_ = nullptr;
    PixelInfoDialog* pixelInfoDialog_ = nullptr;
    PixelStatisticsDialog* pixelStatisticsDialog_ = nullptr;
    QTimer* coordinateTimer_ = nullptr;
    QTimer* displayRenderTimer_ = nullptr;
    QLabel* coordinateLabel_ = nullptr;
    QLabel* imageLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QLabel* taskLabel_ = nullptr;
    qint64 pendingCoordinateX_ = 0;
    qint64 pendingCoordinateY_ = 0;
    bool pendingCoordinateInside_ = false;
};

} // namespace rawviewer::presentation
