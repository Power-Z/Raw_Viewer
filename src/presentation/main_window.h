#pragma once

#include "application/open_image_service.h"

#include <QMainWindow>

#include <atomic>
#include <memory>

class QComboBox;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTreeView;

namespace rawviewer::presentation {

class HistogramWidget;
class ImageViewport;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::shared_ptr<const application::OpenImageService> openService,
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

    std::shared_ptr<const application::OpenImageService> openService_;
    std::shared_ptr<const application::DecodedImage> currentImage_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    std::uint64_t generation_ = 0;

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
    QLabel* coordinateLabel_ = nullptr;
    QLabel* imageLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QLabel* taskLabel_ = nullptr;
};

} // namespace rawviewer::presentation
