#pragma once

#include "application/bayer_extract.h"
#include "application/demosaic.h"
#include "application/document_session.h"
#include "application/filter.h"
#include "application/global_histogram.h"
#include "application/image_transform.h"
#include "application/open_image_service.h"
#include "application/pixel_statistics.h"
#include "application/recent_documents.h"

#include <QMainWindow>

#include <atomic>
#include <array>
#include <memory>
#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QAction;
class QSpinBox;
class QTimer;
class QToolButton;
class QTreeView;

namespace rawviewer::presentation {

class HistogramWidget;
class ImageViewport;
class BayerExtractDialog;
class DemosaicDialog;
class FilterDialog;
class PixelInfoDialog;
class PixelStatisticsDialog;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
                        std::shared_ptr<const application::OpenImageService> openService,
                        std::shared_ptr<const application::IBayerPlaneExporter> bayerExporter,
                        std::shared_ptr<application::IRecentDocumentStore> recentDocuments,
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
    void applyRawDescriptor(const domain::RawDescriptor& descriptor);
    void refreshRecentFilesMenu();
    void openRecentDocument(const application::RecentDocument& document);
    void beginOpen(const QString& path);
    void showDecoded(std::shared_ptr<const application::DecodedImage> image);
    void setTheme(const QString& name);
    void updateCoordinate(qint64 x, qint64 y, bool inside);
    void flushCoordinateUpdate();
    void openPixelInfo();
    void openDemosaic();
    void openFilter();
    void openBayerExtract();
    void openPixelStatistics();
    void setStatisticsMode(application::PixelStatisticsMode mode);
    void beginPixelStatistics(const application::StatisticsSelection& selection);
    void cancelPixelStatistics();
    void beginFilter();
    void cancelFilter();
    void beginDemosaic();
    void cancelDemosaic();
    void restoreDemosaicSource();
    void beginImageTransform(application::ImageTransform transform);
    void cancelImageTransform();
    void beginGlobalHistogram();
    void cancelGlobalHistogram();
    void previewHistogramWindow(double blackPoint, double whitePoint);
    void commitHistogramWindow();
    void beginBayerExtraction();
    void showOriginalImage();
    void exportBayerCsv();
    void applyDisplayControls();
    void commitDisplayEdit();
    void syncDisplayControls();
    void renderCurrentDisplay(bool preserveView = true);
    void refreshFromDocumentState(bool preserveView,
                                  bool refreshStatistics = false);
    void applyHistoryStep(bool redo);
    void cancelPipelineTasks();
    void updateUndoActions();
    void syncToolAvailability();

    std::shared_ptr<const application::OpenImageService> openService_;
    std::shared_ptr<const application::IBayerPlaneExporter> bayerExporter_;
    std::shared_ptr<application::IRecentDocumentStore> recentDocuments_;
    application::BayerExtractService bayerExtractService_;
    application::DemosaicService demosaicService_;
    application::FilterService filterService_;
    application::GlobalHistogramService globalHistogramService_;
    application::ImageTransformService imageTransformService_;
    application::PixelStatisticsService pixelStatisticsService_;
    std::shared_ptr<const application::DecodedImage> currentImage_;
    std::shared_ptr<const application::DecodedImage> displaySource_;
    std::shared_ptr<const application::DecodedImage> preDemosaicSource_;
    std::shared_ptr<const application::BayerExtraction> bayerExtraction_;
    std::unique_ptr<application::DocumentSession> documentSession_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    std::shared_ptr<std::atomic_bool> bayerCancellation_;
    std::shared_ptr<std::atomic_bool> exportCancellation_;
    std::shared_ptr<std::atomic_bool> statisticsCancellation_;
    std::shared_ptr<std::atomic_bool> filterCancellation_;
    std::shared_ptr<std::atomic_bool> demosaicCancellation_;
    std::shared_ptr<std::atomic_bool> imageTransformCancellation_;
    std::shared_ptr<std::atomic_bool> globalHistogramCancellation_;
    std::shared_ptr<std::atomic_uint32_t> statisticsProgress_;
    std::uint64_t generation_ = 0;
    std::uint64_t bayerGeneration_ = 0;
    std::uint64_t statisticsGeneration_ = 0;
    std::uint64_t filterGeneration_ = 0;
    std::uint64_t demosaicGeneration_ = 0;
    std::uint64_t imageTransformGeneration_ = 0;
    std::uint64_t globalHistogramGeneration_ = 0;
    std::shared_ptr<const application::DecodedImage> globalHistogramSource_;
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
    QToolButton* histogramZoomButton_ = nullptr;
    QPushButton* resetDisplayButton_ = nullptr;
    QPushButton* pixelInfoButton_ = nullptr;
    QPushButton* bayerExtractButton_ = nullptr;
    QPushButton* statisticsButton_ = nullptr;
    QPushButton* filterButton_ = nullptr;
    QPushButton* demosaicButton_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    std::array<QAction*, 5> transformActions_{};
    QAction* pixelInfoAction_ = nullptr;
    QAction* bayerExtractAction_ = nullptr;
    QAction* statisticsAction_ = nullptr;
    QAction* filterAction_ = nullptr;
    QAction* demosaicAction_ = nullptr;
    QMenu* recentFilesMenu_ = nullptr;
    BayerExtractDialog* bayerExtractDialog_ = nullptr;
    DemosaicDialog* demosaicDialog_ = nullptr;
    FilterDialog* filterDialog_ = nullptr;
    PixelInfoDialog* pixelInfoDialog_ = nullptr;
    PixelStatisticsDialog* pixelStatisticsDialog_ = nullptr;
    QTimer* coordinateTimer_ = nullptr;
    QTimer* displayRenderTimer_ = nullptr;
    QTimer* displayControlApplyTimer_ = nullptr;
    QLabel* coordinateLabel_ = nullptr;
    QLabel* imageLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QLabel* taskLabel_ = nullptr;
    qint64 pendingCoordinateX_ = 0;
    qint64 pendingCoordinateY_ = 0;
    bool pendingCoordinateInside_ = false;
};

} // namespace rawviewer::presentation
