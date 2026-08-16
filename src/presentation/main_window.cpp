#include "presentation/main_window.h"

#include "presentation/bayer_extract_dialog.h"
#include "presentation/histogram_widget.h"
#include "presentation/image_viewport.h"
#include "presentation/pixel_info_dialog.h"
#include "presentation/pixel_statistics_dialog.h"
#include "presentation/theme_manager.h"

#include "application/pixel_info.h"
#include "application/preview_renderer.h"

#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolButton>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <filesystem>

namespace rawviewer::presentation {
namespace {

QString metadataSummary(const application::ImageMetadata& metadata) {
    return QStringLiteral("%1 × %2 | %3 | %4")
        .arg(metadata.width)
        .arg(metadata.height)
        .arg(QString::fromLatin1(domain::toString(metadata.scalarType)))
        .arg(QString::fromLatin1(domain::toString(metadata.bayerPattern)));
}

} // namespace

MainWindow::MainWindow(
    std::shared_ptr<const application::OpenImageService> openService,
    std::shared_ptr<const application::IBayerPlaneExporter> bayerExporter,
    QWidget* parent)
    : QMainWindow(parent),
      openService_(std::move(openService)),
      bayerExporter_(std::move(bayerExporter)) {
    setWindowTitle(tr("Raw Viewer"));
    resize(1440, 900);
    pixelInfoDialog_ = new PixelInfoDialog(this);
    bayerExtractDialog_ = new BayerExtractDialog(this);
    pixelStatisticsDialog_ = new PixelStatisticsDialog(this);
    createMenus();
    createStatusBar();

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(createLeftPanel());
    viewport_ = new ImageViewport(splitter);
    splitter->addWidget(viewport_);
    splitter->addWidget(createRightPanel());
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 6);
    splitter->setStretchFactor(2, 2);
    splitter->setSizes({280, 840, 280});
    setCentralWidget(splitter);

    connect(viewport_, &ImageViewport::fileDropped,
            this, &MainWindow::openPath);
    connect(viewport_, &ImageViewport::imageCoordinateChanged,
            this, &MainWindow::updateCoordinate);
    connect(viewport_, &ImageViewport::zoomChanged, this, [this](double zoom) {
        zoomLabel_->setText(tr("缩放 %1%").arg(zoom * 100.0, 0, 'f', 1));
    });
    connect(viewport_,
            &ImageViewport::statisticsSelectionCompleted,
            this,
            [this](qint64 x0, qint64 y0, qint64 x1, qint64 y1, bool) {
        if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0) {
            return;
        }
        beginPixelStatistics({
            static_cast<std::uint64_t>(x0),
            static_cast<std::uint64_t>(y0),
            static_cast<std::uint64_t>(x1),
            static_cast<std::uint64_t>(y1)
        });
    });
    connect(pixelInfoDialog_,
            &PixelInfoDialog::optionsChanged,
            viewport_,
            &ImageViewport::setPixelOverlayOptions);
    connect(bayerExtractDialog_,
            &BayerExtractDialog::extractRequested,
            this,
            &MainWindow::beginBayerExtraction);
    connect(bayerExtractDialog_,
            &BayerExtractDialog::showOriginalRequested,
            this,
            &MainWindow::showOriginalImage);
    connect(bayerExtractDialog_,
            &BayerExtractDialog::exportRequested,
            this,
            &MainWindow::exportBayerCsv);
    connect(pixelStatisticsDialog_,
            &PixelStatisticsDialog::modeChanged,
            this,
            &MainWindow::setStatisticsMode);
    connect(pixelStatisticsDialog_,
            &PixelStatisticsDialog::optionsChanged,
            this,
            [this] {
        if (statisticsSelection_) {
            beginPixelStatistics(*statisticsSelection_);
        }
    });
    connect(pixelStatisticsDialog_,
            &PixelStatisticsDialog::cancelRequested,
            this,
            &MainWindow::cancelPixelStatistics);
    connect(pixelStatisticsDialog_,
            &PixelStatisticsDialog::toolClosed,
            this,
            [this] {
        cancelPixelStatistics();
        viewport_->setStatisticsSelectionTool(StatisticsSelectionTool::None);
    });
    coordinateTimer_ = new QTimer(this);
    coordinateTimer_->setSingleShot(true);
    coordinateTimer_->setInterval(16);
    connect(coordinateTimer_, &QTimer::timeout,
            this, &MainWindow::flushCoordinateUpdate);
    displayRenderTimer_ = new QTimer(this);
    displayRenderTimer_->setSingleShot(true);
    displayRenderTimer_->setInterval(16);
    connect(displayRenderTimer_, &QTimer::timeout, this, [this] {
        renderCurrentDisplay();
    });

    QSettings settings;
    setTheme(settings.value("appearance/theme", "Dark").toString());
}

MainWindow::~MainWindow() {
    if (cancellation_) {
        cancellation_->store(true);
    }
    if (bayerCancellation_) {
        bayerCancellation_->store(true);
    }
    if (exportCancellation_) {
        exportCancellation_->store(true);
    }
    if (statisticsCancellation_) {
        statisticsCancellation_->store(true);
    }
}

void MainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    auto* openAction = fileMenu->addAction(tr("打开(&O)..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this,
            tr("打开图像"),
            pathEdit_ ? pathEdit_->text() : QDir::homePath(),
            tr("图像 (*.raw *.RAW *.bin *.BIN *.jpg *.jpeg *.png *.bmp *.3fr *.dng);;所有文件 (*)"));
        if (!path.isEmpty()) {
            openPath(path);
        }
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出"), QKeySequence::Quit, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    undoAction_ = editMenu->addAction(tr("撤销"));
    redoAction_ = editMenu->addAction(tr("重做"));
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_->setShortcut(QKeySequence::Redo);
    undoAction_->setEnabled(false);
    redoAction_->setEnabled(false);
    connect(undoAction_, &QAction::triggered, this, [this] {
        if (documentSession_ && documentSession_->undo()) {
            displayRenderTimer_->stop();
            syncDisplayControls();
            renderCurrentDisplay();
            updateUndoActions();
        }
    });
    connect(redoAction_, &QAction::triggered, this, [this] {
        if (documentSession_ && documentSession_->redo()) {
            displayRenderTimer_->stop();
            syncDisplayControls();
            renderCurrentDisplay();
            updateUndoActions();
        }
    });

    auto* preferences = menuBar()->addMenu(tr("偏好(&P)"));
    auto* themes = preferences->addMenu(tr("主题"));
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    for (const QString& theme : {"Dark", "Light", "Gray", "Yellow", "Red"}) {
        auto* action = themes->addAction(theme);
        action->setCheckable(true);
        action->setData(theme);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, theme] {
            setTheme(theme);
        });
    }

    auto* toolMenu = menuBar()->addMenu(tr("Tool(&T)"));
    pixelInfoAction_ = toolMenu->addAction(tr("Pixel Info"));
    pixelInfoAction_->setEnabled(false);
    connect(pixelInfoAction_, &QAction::triggered,
            this, &MainWindow::openPixelInfo);
    statisticsAction_ = toolMenu->addAction(tr("Pixel Statistics"));
    statisticsAction_->setEnabled(false);
    connect(statisticsAction_, &QAction::triggered,
            this, &MainWindow::openPixelStatistics);
    bayerExtractAction_ = toolMenu->addAction(tr("Bayer Extract"));
    bayerExtractAction_->setEnabled(false);
    connect(bayerExtractAction_, &QAction::triggered,
            this, &MainWindow::openBayerExtract);

    auto* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("关于 Raw Viewer"), this, [this] {
        QMessageBox::about(
            this,
            tr("关于 Raw Viewer"),
            tr("<b>Raw Viewer 0.3.0-preview.1</b><br>"
               "第二阶段：非破坏显示处理、Pixel Info、Bayer Extract 与 Pixel Statistics。<br>"
               "输入文件和原始像素始终保持只读。"));
    });
}

void MainWindow::createStatusBar() {
    coordinateLabel_ = new QLabel(tr("坐标 —"), this);
    imageLabel_ = new QLabel(tr("未打开图像"), this);
    zoomLabel_ = new QLabel(tr("缩放 —"), this);
    taskLabel_ = new QLabel(tr("就绪"), this);
    statusBar()->addWidget(coordinateLabel_);
    statusBar()->addPermanentWidget(imageLabel_, 1);
    statusBar()->addPermanentWidget(zoomLabel_);
    statusBar()->addPermanentWidget(taskLabel_);
}

QWidget* MainWindow::createLeftPanel() {
    auto* panel = new QWidget(this);
    auto* splitter = new QSplitter(Qt::Vertical, panel);
    splitter->addWidget(createRawParametersPanel());
    splitter->addWidget(createFileBrowserPanel());
    splitter->setSizes({310, 590});
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);
    return panel;
}

QWidget* MainWindow::createRawParametersPanel() {
    auto* group = new QGroupBox(tr("平面 RAW 参数"), this);
    auto* form = new QFormLayout(group);

    auto makeSpin = [group](int maximum) {
        auto* spin = new QSpinBox(group);
        spin->setRange(0, maximum);
        spin->setGroupSeparatorShown(true);
        return spin;
    };
    widthSpin_ = makeSpin(1'000'000);
    heightSpin_ = makeSpin(1'000'000);
    headerSpin_ = makeSpin(2'000'000'000);
    strideSpin_ = makeSpin(2'000'000'000);
    widthSpin_->setValue(11776);
    heightSpin_->setValue(8842);
    strideSpin_->setSpecialValueText(tr("自动"));

    scalarCombo_ = new QComboBox(group);
    scalarCombo_->addItems({"UInt8", "UInt16", "UInt32", "Float32"});
    scalarCombo_->setCurrentText("UInt16");
    endianCombo_ = new QComboBox(group);
    endianCombo_->addItems({"Little endian", "Big endian"});
    bayerCombo_ = new QComboBox(group);
    bayerCombo_->addItems({"RGGB", "BGGR", "GRBG", "GBRG", "None"});
    sensorBlackSpin_ = new QDoubleSpinBox(group);
    sensorBlackSpin_->setDecimals(4);
    sensorBlackSpin_->setRange(-1.0e12, 1.0e12);

    form->addRow(tr("Width"), widthSpin_);
    form->addRow(tr("Height"), heightSpin_);
    form->addRow(tr("Skip bytes"), headerSpin_);
    form->addRow(tr("Row stride"), strideSpin_);
    form->addRow(tr("Scalar"), scalarCombo_);
    form->addRow(tr("Byte order"), endianCombo_);
    form->addRow(tr("Bayer"), bayerCombo_);
    form->addRow(tr("Sensor BLV"), sensorBlackSpin_);

    auto* note = new QLabel(
        tr("仅用于无头 .raw/.bin。相机 RAW 容器参数由 LibRaw 读取。"),
        group);
    note->setWordWrap(true);
    form->addRow(note);
    return group;
}

QWidget* MainWindow::createFileBrowserPanel() {
    auto* group = new QGroupBox(tr("文件浏览"), this);
    auto* layout = new QVBoxLayout(group);
    auto* navigation = new QHBoxLayout();
    pathEdit_ = new QLineEdit(QDir::homePath(), group);
    auto* up = new QToolButton(group);
    up->setText("↑");
    up->setToolTip(tr("上一级"));
    auto* refresh = new QToolButton(group);
    refresh->setText("↻");
    refresh->setToolTip(tr("刷新"));
    navigation->addWidget(pathEdit_, 1);
    navigation->addWidget(up);
    navigation->addWidget(refresh);
    layout->addLayout(navigation);

    fileModel_ = new QFileSystemModel(group);
    fileModel_->setReadOnly(true);
    fileModel_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    fileTree_ = new QTreeView(group);
    fileTree_->setModel(fileModel_);
    fileTree_->setRootIndex(fileModel_->setRootPath(pathEdit_->text()));
    fileTree_->setSortingEnabled(true);
    fileTree_->sortByColumn(0, Qt::AscendingOrder);
    layout->addWidget(fileTree_, 1);

    auto navigate = [this](const QString& path) {
        QFileInfo info(path);
        const QString directory = info.isDir() ? info.absoluteFilePath()
                                               : info.absolutePath();
        if (QDir(directory).exists()) {
            pathEdit_->setText(QDir::toNativeSeparators(directory));
            fileTree_->setRootIndex(fileModel_->setRootPath(directory));
        }
    };
    connect(pathEdit_, &QLineEdit::returnPressed, this, [this, navigate] {
        navigate(pathEdit_->text());
    });
    connect(up, &QToolButton::clicked, this, [this, navigate] {
        navigate(QFileInfo(pathEdit_->text()).dir().absolutePath() + "/..");
    });
    connect(refresh, &QToolButton::clicked, this, [this] {
        const QString path = pathEdit_->text();
        fileModel_->setRootPath({});
        fileTree_->setRootIndex(fileModel_->setRootPath(path));
    });
    connect(fileTree_, &QTreeView::doubleClicked, this, [this, navigate](const QModelIndex& index) {
        const QString path = fileModel_->filePath(index);
        if (fileModel_->isDir(index)) {
            navigate(path);
        } else {
            openPath(path);
        }
    });
    return group;
}

QWidget* MainWindow::createRightPanel() {
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    auto* histogramGroup = new QGroupBox(tr("全图统计 / 直方图"), panel);
    auto* histogramLayout = new QVBoxLayout(histogramGroup);
    histogram_ = new HistogramWidget(histogramGroup);
    histogramLayout->addWidget(histogram_);
    histogramLayout->addWidget(new QLabel(tr("当前显示预览的灰度分布"), histogramGroup));
    layout->addWidget(histogramGroup);

    auto* displayGroup = new QGroupBox(tr("显示控制"), panel);
    auto* displayLayout = new QFormLayout(displayGroup);
    auto makeDisplaySpin = [displayGroup] {
        auto* spin = new QDoubleSpinBox(displayGroup);
        spin->setDecimals(4);
        spin->setRange(-1.0e12, 1.0e12);
        spin->setKeyboardTracking(true);
        spin->setEnabled(false);
        return spin;
    };
    blackPointSpin_ = makeDisplaySpin();
    whitePointSpin_ = makeDisplaySpin();
    gammaSpin_ = makeDisplaySpin();
    gammaSpin_->setRange(0.01, 10.0);
    gammaSpin_->setSingleStep(0.05);
    displayLayout->addRow(tr("Display BLV"), blackPointSpin_);
    displayLayout->addRow(tr("Display WLV"), whitePointSpin_);
    displayLayout->addRow(tr("Gamma"), gammaSpin_);
    resetDisplayButton_ =
        new QPushButton(tr("恢复图像默认值"), displayGroup);
    resetDisplayButton_->setEnabled(false);
    displayLayout->addRow(resetDisplayButton_);
    for (auto* spin : {blackPointSpin_, whitePointSpin_, gammaSpin_}) {
        connect(spin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                [this] { applyDisplayControls(); });
        connect(spin,
                &QDoubleSpinBox::editingFinished,
                this,
                &MainWindow::commitDisplayEdit);
    }
    connect(resetDisplayButton_, &QPushButton::clicked, this, [this] {
        if (!documentSession_) {
            return;
        }
        documentSession_->beginDisplayEdit();
        const auto result = documentSession_->updateDisplayMapping(
            documentSession_->initialDisplayMapping());
        if (result.valid) {
            displayRenderTimer_->stop();
            documentSession_->commitDisplayEdit();
            syncDisplayControls();
            renderCurrentDisplay();
            updateUndoActions();
        }
    });
    layout->addWidget(displayGroup);

    auto* tools = new QGroupBox(tr("工具"), panel);
    auto* toolLayout = new QVBoxLayout(tools);
    pixelInfoButton_ = new QPushButton(tr("Pixel Info"), tools);
    pixelInfoButton_->setEnabled(false);
    connect(pixelInfoButton_, &QPushButton::clicked,
            this, &MainWindow::openPixelInfo);
    toolLayout->addWidget(pixelInfoButton_);
    bayerExtractButton_ = new QPushButton(tr("Bayer Extract"), tools);
    bayerExtractButton_->setEnabled(false);
    connect(bayerExtractButton_, &QPushButton::clicked,
            this, &MainWindow::openBayerExtract);
    toolLayout->addWidget(bayerExtractButton_);
    statisticsButton_ = new QPushButton(tr("Pixel Statistics"), tools);
    statisticsButton_->setEnabled(false);
    connect(statisticsButton_, &QPushButton::clicked,
            this, &MainWindow::openPixelStatistics);
    toolLayout->addWidget(statisticsButton_);
    auto* reserved = new QPushButton(tr("ISP tools — reserved"), tools);
    reserved->setEnabled(false);
    toolLayout->addWidget(reserved);
    toolLayout->addStretch();
    layout->addWidget(tools, 1);
    return panel;
}

domain::RawDescriptor MainWindow::currentRawDescriptor() const {
    domain::RawDescriptor descriptor;
    descriptor.width = static_cast<std::uint64_t>(widthSpin_->value());
    descriptor.height = static_cast<std::uint64_t>(heightSpin_->value());
    descriptor.headerBytes = static_cast<std::uint64_t>(headerSpin_->value());
    descriptor.rowStrideBytes = static_cast<std::uint64_t>(strideSpin_->value());
    switch (scalarCombo_->currentIndex()) {
    case 0: descriptor.scalarType = domain::ScalarType::UInt8; break;
    case 1: descriptor.scalarType = domain::ScalarType::UInt16; break;
    case 2: descriptor.scalarType = domain::ScalarType::UInt32; break;
    case 3: descriptor.scalarType = domain::ScalarType::Float32; break;
    }
    descriptor.byteOrder = endianCombo_->currentIndex() == 0
        ? domain::ByteOrder::LittleEndian
        : domain::ByteOrder::BigEndian;
    switch (bayerCombo_->currentIndex()) {
    case 0: descriptor.bayerPattern = domain::BayerPattern::RGGB; break;
    case 1: descriptor.bayerPattern = domain::BayerPattern::BGGR; break;
    case 2: descriptor.bayerPattern = domain::BayerPattern::GRBG; break;
    case 3: descriptor.bayerPattern = domain::BayerPattern::GBRG; break;
    default: descriptor.bayerPattern = domain::BayerPattern::None; break;
    }
    descriptor.sensorBlackLevel = sensorBlackSpin_->value();
    return descriptor;
}

void MainWindow::openPath(const QString& path) {
    const QFileInfo info(path);
    if (info.isDir()) {
        pathEdit_->setText(info.absoluteFilePath());
        fileTree_->setRootIndex(fileModel_->setRootPath(info.absoluteFilePath()));
        return;
    }
    if (!info.exists()) {
        QMessageBox::warning(this, tr("无法打开"), tr("文件不存在。"));
        return;
    }
    beginOpen(info.absoluteFilePath());
}

void MainWindow::beginOpen(const QString& path) {
    if (cancellation_) {
        cancellation_->store(true);
    }
    if (bayerCancellation_) {
        bayerCancellation_->store(true);
        bayerCancellation_.reset();
    }
    if (exportCancellation_) {
        exportCancellation_->store(true);
        exportCancellation_.reset();
    }
    cancelPixelStatistics();
    statisticsSelection_.reset();
    viewport_->setStatisticsSelectionTool(StatisticsSelectionTool::None);
    ++bayerGeneration_;
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto token = cancellation_;
    const auto generation = ++generation_;
    taskLabel_->setText(tr("正在打开…"));
    viewport_->setLoading(
        true,
        tr("正在加载 %1…").arg(QFileInfo(path).fileName()));
    statusBar()->showMessage(QDir::toNativeSeparators(path));

    application::OpenImageRequest request;
    request.path = std::filesystem::path(path.toStdWString());
    request.flatRawDescriptor = currentRawDescriptor();
    request.cancellation = token;

    auto* watcher = new QFutureWatcher<application::DecodeResult>(this);
    connect(watcher, &QFutureWatcher<application::DecodeResult>::finished,
            this, [this, watcher, generation, path] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != generation_) {
            return;
        }
        viewport_->setLoading(false);
        if (!result.succeeded()) {
            taskLabel_->setText(tr("打开失败"));
            QMessageBox::critical(
                this,
                tr("打开失败"),
                tr("%1\n\n错误码：%2")
                    .arg(QString::fromStdString(result.message),
                         QString::fromStdString(result.errorCode)));
            return;
        }
        const QString directory = QFileInfo(path).absolutePath();
        pathEdit_->setText(directory);
        fileTree_->setRootIndex(fileModel_->setRootPath(directory));
        showDecoded(result.image);
        taskLabel_->setText(tr("就绪"));
        statusBar()->showMessage(tr("已打开 %1").arg(QFileInfo(path).fileName()), 4000);
    });
    const auto service = openService_;
    watcher->setFuture(QtConcurrent::run([service, request] {
        return service->execute(request);
    }));
}

void MainWindow::showDecoded(
    std::shared_ptr<const application::DecodedImage> image) {
    documentSession_ =
        std::make_unique<application::DocumentSession>(std::move(image));
    bayerExtraction_.reset();
    displaySource_ = documentSession_->original();
    bayerExtractDialog_->setResult(nullptr);
    bayerExtractDialog_->setSource(&displaySource_->metadata);
    pixelStatisticsDialog_->setSource(&displaySource_->metadata);
    pendingCoordinateInside_ = false;
    coordinateLabel_->setText(tr("坐标 —"));
    syncDisplayControls();
    renderCurrentDisplay(false);
    updateUndoActions();
    pixelInfoAction_->setEnabled(true);
    pixelInfoButton_->setEnabled(true);
    const bool supportsBayer =
        displaySource_->metadata.kind != application::ImageKind::Standard &&
        displaySource_->metadata.bayerPattern != domain::BayerPattern::None;
    bayerExtractAction_->setEnabled(supportsBayer);
    bayerExtractButton_->setEnabled(supportsBayer);
    statisticsAction_->setEnabled(supportsBayer);
    statisticsButton_->setEnabled(supportsBayer);
    imageLabel_->setText(metadataSummary(currentImage_->metadata));
    setWindowTitle(QStringLiteral("%1 — Raw Viewer")
                       .arg(QString::fromStdString(currentImage_->metadata.camera)
                                .trimmed()
                                .isEmpty()
                            ? QString::fromStdString(currentImage_->metadata.format)
                            : QString::fromStdString(currentImage_->metadata.camera)));
}

void MainWindow::applyDisplayControls() {
    if (!documentSession_) {
        return;
    }
    domain::DisplayMapping mapping;
    mapping.blackPoint = blackPointSpin_->value();
    mapping.whitePoint = whitePointSpin_->value();
    mapping.gamma = gammaSpin_->value();
    const auto result = documentSession_->updateDisplayMapping(mapping);
    if (!result.valid) {
        taskLabel_->setText(QString::fromStdString(result.message));
        return;
    }
    taskLabel_->setText(tr("显示 revision %1")
                            .arg(documentSession_->revision()));
    if (!displayRenderTimer_->isActive()) {
        displayRenderTimer_->start();
    }
}

void MainWindow::commitDisplayEdit() {
    if (!documentSession_) {
        return;
    }
    documentSession_->commitDisplayEdit();
    updateUndoActions();
    taskLabel_->setText(tr("就绪"));
}

void MainWindow::syncDisplayControls() {
    if (!documentSession_) {
        return;
    }
    const QSignalBlocker blockBlack(blackPointSpin_);
    const QSignalBlocker blockWhite(whitePointSpin_);
    const QSignalBlocker blockGamma(gammaSpin_);
    const auto& mapping = documentSession_->displayMapping();
    blackPointSpin_->setValue(mapping.blackPoint);
    whitePointSpin_->setValue(mapping.whitePoint);
    gammaSpin_->setValue(mapping.gamma);
    const bool directGrayscale =
        documentSession_->original()->preview.hasGrayscale16();
    blackPointSpin_->setEnabled(!directGrayscale);
    whitePointSpin_->setEnabled(!directGrayscale);
    gammaSpin_->setEnabled(!directGrayscale);
    resetDisplayButton_->setEnabled(!directGrayscale);
}

void MainWindow::renderCurrentDisplay(bool preserveView) {
    if (!documentSession_) {
        return;
    }
    const auto source = displaySource_
        ? displaySource_
        : documentSession_->original();
    currentImage_ = application::PreviewRenderer::render(
        source,
        documentSession_->displayMapping());
    if (!currentImage_) {
        return;
    }
    viewport_->setDisplayMapping(documentSession_->displayMapping());
    viewport_->setImage(currentImage_, preserveView);
    histogram_->setImage(currentImage_);
    imageLabel_->setText(metadataSummary(source->metadata));
}

void MainWindow::updateUndoActions() {
    const bool hasDocument = static_cast<bool>(documentSession_);
    undoAction_->setEnabled(hasDocument && documentSession_->canUndo());
    redoAction_->setEnabled(hasDocument && documentSession_->canRedo());
}

void MainWindow::setTheme(const QString& name) {
    ThemeManager::apply(*qApp, name);
    QSettings().setValue("appearance/theme", name);
    for (auto* action : menuBar()->findChildren<QAction*>()) {
        if (action->data().toString() == name) {
            action->setChecked(true);
        }
    }
}

void MainWindow::updateCoordinate(qint64 x, qint64 y, bool inside) {
    pendingCoordinateX_ = x;
    pendingCoordinateY_ = y;
    pendingCoordinateInside_ = inside;
    if (!coordinateTimer_->isActive()) {
        coordinateTimer_->start();
    }
}

void MainWindow::flushCoordinateUpdate() {
    if (!pendingCoordinateInside_ || !documentSession_ ||
        !displaySource_ || !displaySource_->pixels) {
        coordinateLabel_->setText(tr("坐标 —"));
        return;
    }
    const auto info = application::queryPixelInfo(
        *displaySource_,
        documentSession_->displayMapping(),
        static_cast<std::uint64_t>(pendingCoordinateX_),
        static_cast<std::uint64_t>(pendingCoordinateY_));
    if (!info.valid) {
        coordinateLabel_->setText(
            tr("坐标 %1, %2 | 值 —")
                .arg(pendingCoordinateX_)
                .arg(pendingCoordinateY_));
        return;
    }
    if (bayerExtraction_ &&
        displaySource_.get() == bayerExtraction_->image.get()) {
        const auto source = bayerExtraction_->geometry.sourceCoordinate(
            static_cast<std::uint64_t>(pendingCoordinateX_),
            static_cast<std::uint64_t>(pendingCoordinateY_));
        if (source) {
            coordinateLabel_->setText(
                tr("通道 %1 (%2, %3) → Source (%4, %5) | Raw %6 | Display %7")
                    .arg(QString::fromLatin1(domain::toString(
                        bayerExtraction_->geometry.channel)))
                    .arg(pendingCoordinateX_)
                    .arg(pendingCoordinateY_)
                    .arg(source->x)
                    .arg(source->y)
                    .arg(info.originalValue, 0, 'g', 10)
                    .arg(info.processedValue, 0, 'f', 4));
            return;
        }
    }
    if (displaySource_->metadata.kind == application::ImageKind::FlatRaw) {
        coordinateLabel_->setText(
            tr("坐标 %1, %2 | Raw %3")
                .arg(pendingCoordinateX_)
                .arg(pendingCoordinateY_)
                .arg(info.originalValue, 0, 'g', 10));
        return;
    }
    QString channel;
    if (info.channel != domain::BayerChannel::None) {
        channel = tr(" | %1").arg(
            QString::fromLatin1(domain::toString(info.channel)));
    }
    coordinateLabel_->setText(
        tr("坐标 %1, %2 | Raw %3 | Display %4 | RGB %5,%6,%7%8")
            .arg(pendingCoordinateX_)
            .arg(pendingCoordinateY_)
            .arg(info.originalValue, 0, 'g', 10)
            .arg(info.processedValue, 0, 'f', 4)
            .arg(info.red)
            .arg(info.green)
            .arg(info.blue)
            .arg(channel));
}

void MainWindow::openPixelInfo() {
    if (!documentSession_) {
        return;
    }
    viewport_->setPixelOverlayOptions(pixelInfoDialog_->options());
    pixelInfoDialog_->show();
    pixelInfoDialog_->raise();
    pixelInfoDialog_->activateWindow();
}

void MainWindow::openBayerExtract() {
    if (!documentSession_ ||
        documentSession_->original()->metadata.bayerPattern ==
            domain::BayerPattern::None) {
        return;
    }
    bayerExtractDialog_->show();
    bayerExtractDialog_->raise();
    bayerExtractDialog_->activateWindow();
}

void MainWindow::openPixelStatistics() {
    if (!documentSession_) {
        return;
    }
    const auto& metadata = documentSession_->original()->metadata;
    if (metadata.kind == application::ImageKind::Standard ||
        metadata.bayerPattern == domain::BayerPattern::None) {
        return;
    }
    if (displaySource_.get() != documentSession_->original().get()) {
        showOriginalImage();
    }
    setStatisticsMode(pixelStatisticsDialog_->mode());
    pixelStatisticsDialog_->show();
    pixelStatisticsDialog_->raise();
    pixelStatisticsDialog_->activateWindow();
}

void MainWindow::setStatisticsMode(
    application::PixelStatisticsMode mode) {
    cancelPixelStatistics();
    statisticsSelection_.reset();
    viewport_->clearStatisticsSelection();
    switch (mode) {
    case application::PixelStatisticsMode::Status:
    case application::PixelStatisticsMode::HorizontalBox:
    case application::PixelStatisticsMode::VerticalBox:
        viewport_->setStatisticsSelectionTool(
            StatisticsSelectionTool::Rectangle);
        break;
    case application::PixelStatisticsMode::Line:
        viewport_->setStatisticsSelectionTool(StatisticsSelectionTool::Line);
        break;
    case application::PixelStatisticsMode::WhiteBalance:
        viewport_->setStatisticsSelectionTool(StatisticsSelectionTool::None);
        break;
    }
}

void MainWindow::beginPixelStatistics(
    const application::StatisticsSelection& selection) {
    if (!documentSession_ ||
        pixelStatisticsDialog_->mode() ==
            application::PixelStatisticsMode::WhiteBalance) {
        return;
    }
    if (statisticsCancellation_) {
        statisticsCancellation_->store(true, std::memory_order_relaxed);
    }
    statisticsCancellation_ = std::make_shared<std::atomic_bool>(false);
    statisticsProgress_ = std::make_shared<std::atomic_uint32_t>(0);
    statisticsSelection_ = selection;
    const auto token = statisticsCancellation_;
    const auto progress = statisticsProgress_;
    const auto generation = ++statisticsGeneration_;

    application::PixelStatisticsRequest request;
    request.source = documentSession_->original();
    request.mode = pixelStatisticsDialog_->mode();
    request.selection = selection;
    request.channel = pixelStatisticsDialog_->channel();
    request.histogramBins = pixelStatisticsDialog_->histogramBins();
    request.cancellation = token;
    request.progressPermille = progress;
    pixelStatisticsDialog_->setSelection(selection);
    pixelStatisticsDialog_->setBusy(true, progress);
    taskLabel_->setText(tr("正在统计原始 Bayer 像素…"));

    auto* watcher =
        new QFutureWatcher<application::PixelStatisticsResult>(this);
    connect(watcher,
            &QFutureWatcher<application::PixelStatisticsResult>::finished,
            this,
            [this, watcher, generation, token] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != statisticsGeneration_ ||
            token != statisticsCancellation_) {
            return;
        }
        pixelStatisticsDialog_->setBusy(false);
        if (!result.succeeded()) {
            if (result.errorCode == "task.cancelled") {
                taskLabel_->setText(tr("像素统计已取消"));
                pixelStatisticsDialog_->clearResult(tr("计算已取消。"));
            } else {
                taskLabel_->setText(tr("像素统计失败"));
                pixelStatisticsDialog_->clearResult(
                    QString::fromStdString(result.message));
            }
            return;
        }
        pixelStatisticsDialog_->setResult(result);
        taskLabel_->setText(
            tr("统计完成：%1 个结果样本").arg(result.summary.count));
    });
    const auto service = pixelStatisticsService_;
    watcher->setFuture(QtConcurrent::run([service, request] {
        return service.execute(request);
    }));
}

void MainWindow::cancelPixelStatistics() {
    if (statisticsCancellation_) {
        statisticsCancellation_->store(true, std::memory_order_relaxed);
        statisticsCancellation_.reset();
    }
    statisticsProgress_.reset();
    ++statisticsGeneration_;
    if (pixelStatisticsDialog_) {
        pixelStatisticsDialog_->setBusy(false);
    }
}

void MainWindow::beginBayerExtraction() {
    if (!documentSession_) {
        return;
    }
    cancelPixelStatistics();
    statisticsSelection_.reset();
    viewport_->setStatisticsSelectionTool(StatisticsSelectionTool::None);
    pixelStatisticsDialog_->hide();
    if (bayerCancellation_) {
        bayerCancellation_->store(true);
    }
    if (exportCancellation_) {
        exportCancellation_->store(true);
        exportCancellation_.reset();
    }
    bayerCancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto token = bayerCancellation_;
    const auto generation = ++bayerGeneration_;
    const auto request = bayerExtractDialog_->request(
        documentSession_->original(), token);
    bayerExtractDialog_->setBusy(true, tr("正在生成单通道预览…"));
    taskLabel_->setText(tr("正在提取 Bayer 通道…"));

    auto* watcher =
        new QFutureWatcher<application::BayerExtractResult>(this);
    connect(watcher,
            &QFutureWatcher<application::BayerExtractResult>::finished,
            this,
            [this, watcher, generation, token] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != bayerGeneration_ || token != bayerCancellation_) {
            return;
        }
        bayerExtractDialog_->setBusy(false);
        if (!result.succeeded()) {
            taskLabel_->setText(tr("Bayer 提取失败"));
            bayerExtractDialog_->setResult(nullptr);
            if (result.errorCode != "task.cancelled") {
                QMessageBox::warning(
                    this,
                    tr("Bayer Extract"),
                    QString::fromStdString(result.message));
            }
            return;
        }
        bayerExtraction_ = result.extraction;
        displaySource_ = bayerExtraction_->image;
        pendingCoordinateInside_ = false;
        coordinateLabel_->setText(tr("坐标 —"));
        bayerExtractDialog_->setResult(bayerExtraction_.get());
        renderCurrentDisplay(false);
        taskLabel_->setText(
            tr("已显示 %1 通道")
                .arg(QString::fromLatin1(domain::toString(
                    bayerExtraction_->geometry.channel))));
    });
    const auto service = bayerExtractService_;
    watcher->setFuture(QtConcurrent::run([service, request] {
        return service.execute(request);
    }));
}

void MainWindow::showOriginalImage() {
    if (!documentSession_) {
        return;
    }
    displaySource_ = documentSession_->original();
    pendingCoordinateInside_ = false;
    coordinateLabel_->setText(tr("坐标 —"));
    renderCurrentDisplay(false);
    taskLabel_->setText(tr("已显示原始 Bayer 图像"));
}

void MainWindow::exportBayerCsv() {
    if (!bayerExtraction_ || !bayerExporter_) {
        return;
    }
    const QString suggested = QStringLiteral("bayer_%1.csv").arg(
        QString::fromLatin1(domain::toString(
            bayerExtraction_->geometry.channel)));
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("导出 Bayer 通道 CSV"),
        QDir(pathEdit_->text()).filePath(suggested),
        tr("CSV 文件 (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    if (exportCancellation_) {
        exportCancellation_->store(true);
    }
    exportCancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto token = exportCancellation_;
    application::BayerExportRequest request;
    request.extraction = bayerExtraction_;
    request.path = std::filesystem::path(path.toStdWString());
    request.cancellation = token;
    bayerExtractDialog_->setBusy(true, tr("正在导出 CSV…"));
    taskLabel_->setText(tr("正在导出 Bayer CSV…"));

    auto* watcher =
        new QFutureWatcher<application::BayerExportResult>(this);
    connect(watcher,
            &QFutureWatcher<application::BayerExportResult>::finished,
            this,
            [this, watcher, token, path] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (token != exportCancellation_) {
            return;
        }
        bayerExtractDialog_->setBusy(false);
        bayerExtractDialog_->setResult(bayerExtraction_.get());
        if (!result.succeeded) {
            taskLabel_->setText(tr("Bayer CSV 导出失败"));
            if (result.errorCode != "task.cancelled") {
                QMessageBox::critical(
                    this,
                    tr("导出失败"),
                    QString::fromStdString(result.message));
            }
            return;
        }
        taskLabel_->setText(
            tr("已导出 %1 个样本").arg(result.exportedSamples));
        QMessageBox::information(
            this,
            tr("导出完成"),
            tr("已导出 %1 个样本到：\n%2")
                .arg(result.exportedSamples)
                .arg(QDir::toNativeSeparators(path)));
    });
    const auto exporter = bayerExporter_;
    watcher->setFuture(QtConcurrent::run([exporter, request] {
        return exporter->exportCsv(request);
    }));
}

} // namespace rawviewer::presentation
