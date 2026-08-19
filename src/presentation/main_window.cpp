#include "presentation/main_window.h"

#include "presentation/bayer_extract_dialog.h"
#include "presentation/demosaic_dialog.h"
#include "presentation/filter_dialog.h"
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
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
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

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

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
    std::shared_ptr<application::IRecentDocumentStore> recentDocuments,
    QWidget* parent)
    : QMainWindow(parent),
      openService_(std::move(openService)),
      bayerExporter_(std::move(bayerExporter)),
      recentDocuments_(std::move(recentDocuments)) {
    setWindowTitle(tr("Raw Viewer"));
    resize(1440, 900);
    pixelInfoDialog_ = new PixelInfoDialog(this);
    bayerExtractDialog_ = new BayerExtractDialog(this);
    demosaicDialog_ = new DemosaicDialog(this);
    filterDialog_ = new FilterDialog(this);
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
    viewport_->setPixelOverlayOptions(pixelInfoDialog_->options());
    connect(bayerExtractDialog_,
            &BayerExtractDialog::extractRequested,
            this,
            &MainWindow::beginBayerExtraction);
    connect(bayerExtractDialog_,
            &BayerExtractDialog::showOriginalRequested,
            this,
            &MainWindow::showOriginalImage);
    connect(filterDialog_,
            &FilterDialog::applyRequested,
            this,
            &MainWindow::beginFilter);
    connect(demosaicDialog_,
            &DemosaicDialog::applyRequested,
            this,
            &MainWindow::beginDemosaic);
    connect(demosaicDialog_,
            &DemosaicDialog::restoreSourceRequested,
            this,
            &MainWindow::restoreDemosaicSource);
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
    displayControlApplyTimer_ = new QTimer(this);
    displayControlApplyTimer_->setSingleShot(true);
    displayControlApplyTimer_->setInterval(120);
    connect(displayControlApplyTimer_, &QTimer::timeout,
            this, &MainWindow::applyDisplayControls);

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
    if (filterCancellation_) {
        filterCancellation_->store(true);
    }
    if (demosaicCancellation_) {
        demosaicCancellation_->store(true);
    }
    if (imageTransformCancellation_) {
        imageTransformCancellation_->store(true);
    }
    if (globalHistogramCancellation_) {
        globalHistogramCancellation_->store(true);
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
    recentFilesMenu_ = fileMenu->addMenu(tr("Recent Files(&R)"));
    recentFilesMenu_->setObjectName(QStringLiteral("recentFilesMenu"));
    connect(recentFilesMenu_, &QMenu::aboutToShow,
            this, &MainWindow::refreshRecentFilesMenu);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出"), QKeySequence::Quit, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    undoAction_ = editMenu->addAction(tr("撤销"));
    redoAction_ = editMenu->addAction(tr("重做"));
    undoAction_->setObjectName(QStringLiteral("undoAction"));
    redoAction_->setObjectName(QStringLiteral("redoAction"));
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_->setShortcut(QKeySequence::Redo);
    undoAction_->setEnabled(false);
    redoAction_->setEnabled(false);
    connect(undoAction_, &QAction::triggered, this, [this] {
        applyHistoryStep(false);
    });
    connect(redoAction_, &QAction::triggered, this, [this] {
        applyHistoryStep(true);
    });
    editMenu->addSeparator();
    struct TransformActionDefinition {
        QString text;
        QString objectName;
        QKeySequence shortcut;
        application::ImageTransform transform;
    };
    const std::array definitions{
        TransformActionDefinition{tr("Flip（上下翻转）"),
            QStringLiteral("flipVerticalAction"),
            QKeySequence(QStringLiteral("Ctrl+Alt+V")),
            application::ImageTransform::FlipVertical},
        TransformActionDefinition{tr("Mirror（左右翻转）"),
            QStringLiteral("mirrorHorizontalAction"),
            QKeySequence(QStringLiteral("Ctrl+Alt+H")),
            application::ImageTransform::MirrorHorizontal},
        TransformActionDefinition{tr("Rotate Left（向左旋转 90°）"),
            QStringLiteral("rotateLeftAction"),
            QKeySequence(QStringLiteral("Ctrl+Alt+Left")),
            application::ImageTransform::RotateLeft},
        TransformActionDefinition{tr("Rotate Right（向右旋转 90°）"),
            QStringLiteral("rotateRightAction"),
            QKeySequence(QStringLiteral("Ctrl+Alt+Right")),
            application::ImageTransform::RotateRight},
        TransformActionDefinition{tr("Rotate 180（旋转 180°）"),
            QStringLiteral("rotate180Action"),
            QKeySequence(QStringLiteral("Ctrl+Alt+Down")),
            application::ImageTransform::Rotate180}};
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const auto& definition = definitions[index];
        auto* action = editMenu->addAction(definition.text);
        action->setObjectName(definition.objectName);
        action->setShortcut(definition.shortcut);
        action->setEnabled(false);
        transformActions_[index] = action;
        connect(action, &QAction::triggered, this,
                [this, transform = definition.transform] {
            beginImageTransform(transform);
        });
    }

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
    preferences->addSeparator();
    auto* statisticsPreferences =
        preferences->addAction(tr("Pixel Statistics…"));
    statisticsPreferences->setObjectName(
        QStringLiteral("pixelStatisticsPreferencesAction"));
    connect(statisticsPreferences,
            &QAction::triggered,
            pixelStatisticsDialog_,
            &PixelStatisticsDialog::showPreferences);

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
    filterAction_ = toolMenu->addAction(tr("Filter"));
    filterAction_->setEnabled(false);
    connect(filterAction_, &QAction::triggered,
            this, &MainWindow::openFilter);
    demosaicAction_ = toolMenu->addAction(tr("Demosaic"));
    demosaicAction_->setEnabled(false);
    connect(demosaicAction_, &QAction::triggered,
            this, &MainWindow::openDemosaic);

    auto* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("关于 Raw Viewer"), this, [this] {
        QMessageBox::about(
            this,
            tr("关于 Raw Viewer"),
            tr("<b>Raw Viewer 0.3.0-preview.4</b><br>"
               "第二阶段：非破坏显示处理、Pixel Info、Bayer Extract、"
               "Pixel Statistics、Filter 与 Bayer Demosaic。<br>"
               "输入文件和原始像素始终保持只读。"));
    });
}

void MainWindow::createStatusBar() {
    coordinateLabel_ = new QLabel(tr("坐标 —"), this);
    coordinateLabel_->setObjectName(QStringLiteral("coordinateLabel"));
    imageLabel_ = new QLabel(tr("未打开图像"), this);
    zoomLabel_ = new QLabel(tr("缩放 —"), this);
    taskLabel_ = new QLabel(tr("就绪"), this);
    imageLabel_->setObjectName(QStringLiteral("imageStatusLabel"));
    taskLabel_->setObjectName(QStringLiteral("taskStatusLabel"));
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
    auto* histogramTools = new QHBoxLayout();
    auto* histogramHint = new QLabel(
        tr("全分辨率 · 自动通道模式"), histogramGroup);
    histogramHint->setObjectName(QStringLiteral("globalHistogramHint"));
    histogramZoomButton_ = new QToolButton(histogramGroup);
    histogramZoomButton_->setObjectName(
        QStringLiteral("histogramWindowZoomButton"));
    histogramZoomButton_->setText(tr("🔍 BLV–WLV"));
    histogramZoomButton_->setToolTip(
        tr("以当前 BLV/WLV 为横轴区间放大；再次点击返回全范围"));
    histogramZoomButton_->setCheckable(true);
    histogramZoomButton_->setEnabled(false);
    histogramTools->addWidget(histogramHint);
    histogramTools->addStretch();
    histogramTools->addWidget(histogramZoomButton_);
    histogramLayout->addLayout(histogramTools);
    histogram_ = new HistogramWidget(histogramGroup);
    histogramLayout->addWidget(histogram_);
    connect(histogramZoomButton_, &QToolButton::toggled,
            histogram_, &HistogramWidget::setZoomed);
    connect(histogram_, &HistogramWidget::windowEditStarted,
            this, [this] {
        if (documentSession_) documentSession_->beginDisplayEdit();
    });
    connect(histogram_, &HistogramWidget::displayWindowChanged,
            this, &MainWindow::previewHistogramWindow);
    connect(histogram_, &HistogramWidget::windowEditFinished,
            this, &MainWindow::commitHistogramWindow);
    layout->addWidget(histogramGroup);

    auto* displayGroup = new QGroupBox(tr("显示控制"), panel);
    auto* displayLayout = new QFormLayout(displayGroup);
    auto makeDisplaySpin = [displayGroup] {
        auto* spin = new QDoubleSpinBox(displayGroup);
        spin->setDecimals(1);
        spin->setRange(-1.0e12, 1.0e12);
        spin->setKeyboardTracking(false);
        spin->setEnabled(false);
        return spin;
    };
    blackPointSpin_ = makeDisplaySpin();
    whitePointSpin_ = makeDisplaySpin();
    gammaSpin_ = makeDisplaySpin();
    blackPointSpin_->setObjectName(QStringLiteral("displayBlackPointSpin"));
    whitePointSpin_->setObjectName(QStringLiteral("displayWhitePointSpin"));
    gammaSpin_->setObjectName(QStringLiteral("displayGammaSpin"));
    gammaSpin_->setDecimals(2);
    gammaSpin_->setRange(0.01, 10.0);
    gammaSpin_->setSingleStep(0.05);
    displayLayout->addRow(tr("Display BLV"), blackPointSpin_);
    displayLayout->addRow(tr("Display WLV"), whitePointSpin_);
    displayLayout->addRow(tr("Gamma"), gammaSpin_);
    resetDisplayButton_ =
        new QPushButton(tr("恢复图像默认值"), displayGroup);
    resetDisplayButton_->setEnabled(false);
    displayLayout->addRow(resetDisplayButton_);
    for (auto* spin : {blackPointSpin_, whitePointSpin_}) {
        connect(spin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                [this] {
            histogram_->setDisplayWindow(
                blackPointSpin_->value(), whitePointSpin_->value());
            displayControlApplyTimer_->start();
        });
        connect(spin,
                &QDoubleSpinBox::editingFinished,
                this, [this] {
            displayControlApplyTimer_->stop();
            applyDisplayControls();
            commitDisplayEdit();
        });
    }
    connect(gammaSpin_,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] { displayControlApplyTimer_->start(); });
    connect(gammaSpin_, &QDoubleSpinBox::editingFinished,
            this, [this] {
        displayControlApplyTimer_->stop();
        applyDisplayControls();
        commitDisplayEdit();
    });
    connect(resetDisplayButton_, &QPushButton::clicked, this, [this] {
        if (!documentSession_) {
            return;
        }
        displayControlApplyTimer_->stop();
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
    filterButton_ = new QPushButton(tr("Filter"), tools);
    filterButton_->setEnabled(false);
    connect(filterButton_, &QPushButton::clicked,
            this, &MainWindow::openFilter);
    toolLayout->addWidget(filterButton_);
    demosaicButton_ = new QPushButton(tr("Demosaic"), tools);
    demosaicButton_->setEnabled(false);
    connect(demosaicButton_, &QPushButton::clicked,
            this, &MainWindow::openDemosaic);
    toolLayout->addWidget(demosaicButton_);
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

void MainWindow::applyRawDescriptor(
    const domain::RawDescriptor& descriptor) {
    widthSpin_->setValue(static_cast<int>(std::min<std::uint64_t>(
        descriptor.width, static_cast<std::uint64_t>(widthSpin_->maximum()))));
    heightSpin_->setValue(static_cast<int>(std::min<std::uint64_t>(
        descriptor.height, static_cast<std::uint64_t>(heightSpin_->maximum()))));
    headerSpin_->setValue(static_cast<int>(std::min<std::uint64_t>(
        descriptor.headerBytes, static_cast<std::uint64_t>(headerSpin_->maximum()))));
    strideSpin_->setValue(static_cast<int>(std::min<std::uint64_t>(
        descriptor.rowStrideBytes, static_cast<std::uint64_t>(strideSpin_->maximum()))));
    switch (descriptor.scalarType) {
    case domain::ScalarType::UInt8: scalarCombo_->setCurrentIndex(0); break;
    case domain::ScalarType::UInt16: scalarCombo_->setCurrentIndex(1); break;
    case domain::ScalarType::UInt32: scalarCombo_->setCurrentIndex(2); break;
    case domain::ScalarType::Float32: scalarCombo_->setCurrentIndex(3); break;
    }
    endianCombo_->setCurrentIndex(
        descriptor.byteOrder == domain::ByteOrder::LittleEndian ? 0 : 1);
    switch (descriptor.bayerPattern) {
    case domain::BayerPattern::RGGB: bayerCombo_->setCurrentIndex(0); break;
    case domain::BayerPattern::BGGR: bayerCombo_->setCurrentIndex(1); break;
    case domain::BayerPattern::GRBG: bayerCombo_->setCurrentIndex(2); break;
    case domain::BayerPattern::GBRG: bayerCombo_->setCurrentIndex(3); break;
    case domain::BayerPattern::None: bayerCombo_->setCurrentIndex(4); break;
    }
    sensorBlackSpin_->setValue(descriptor.sensorBlackLevel);
}

void MainWindow::refreshRecentFilesMenu() {
    recentFilesMenu_->clear();
    const auto documents = recentDocuments_
        ? recentDocuments_->load()
        : std::vector<application::RecentDocument>{};
    if (documents.empty()) {
        auto* empty = recentFilesMenu_->addAction(tr("暂无最近文档"));
        empty->setEnabled(false);
        return;
    }
    int position = 1;
    for (const auto& document : documents) {
#ifdef _WIN32
        const QString path = QString::fromStdWString(document.path.wstring());
#else
        const QString path = QString::fromStdString(document.path.string());
#endif
        const bool exists = QFileInfo(path).isFile();
        const QString label = QStringLiteral("&%1  %2%3")
            .arg(position++)
            .arg(QDir::toNativeSeparators(path),
                 exists ? QString() : tr("  （文件已删除）"));
        auto* action = recentFilesMenu_->addAction(label);
        action->setEnabled(exists);
        action->setToolTip(tr("%1\n%2 × %3 · %4 · %5 · %6 · Skip %7")
            .arg(QDir::toNativeSeparators(path))
            .arg(document.rawDescriptor.width)
            .arg(document.rawDescriptor.height)
            .arg(QString::fromLatin1(domain::toString(
                document.rawDescriptor.scalarType)))
            .arg(QString::fromLatin1(domain::toString(
                document.rawDescriptor.byteOrder)))
            .arg(QString::fromLatin1(domain::toString(
                document.rawDescriptor.bayerPattern)))
            .arg(document.rawDescriptor.headerBytes));
        if (exists) {
            connect(action, &QAction::triggered, this,
                    [this, document] { openRecentDocument(document); });
        }
    }
    recentFilesMenu_->addSeparator();
    auto* clearAction = recentFilesMenu_->addAction(tr("清除最近文档记录"));
    connect(clearAction, &QAction::triggered, this, [this] {
        if (recentDocuments_) recentDocuments_->clear();
    });
}

void MainWindow::openRecentDocument(
    const application::RecentDocument& document) {
    applyRawDescriptor(document.rawDescriptor);
#ifdef _WIN32
    openPath(QString::fromStdWString(document.path.wstring()));
#else
    openPath(QString::fromStdString(document.path.string()));
#endif
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
    cancelFilter();
    cancelDemosaic();
    cancelImageTransform();
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

    const auto rawDescriptor = currentRawDescriptor();
    application::OpenImageRequest request;
    request.path = std::filesystem::path(path.toStdWString());
    request.flatRawDescriptor = rawDescriptor;
    request.cancellation = token;

    auto* watcher = new QFutureWatcher<application::DecodeResult>(this);
    connect(watcher, &QFutureWatcher<application::DecodeResult>::finished,
            this, [this, watcher, generation, path, rawDescriptor] {
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
        if (recentDocuments_) {
            recentDocuments_->remember({
                std::filesystem::path(path.toStdWString()), rawDescriptor});
        }
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
    // An edit can be launched from the previous document while a new open is
    // still decoding. Invalidate every old-document task again at hand-off so
    // that a late result can never replace the newly decoded display source.
    cancelPipelineTasks();
    documentSession_ =
        std::make_unique<application::DocumentSession>(std::move(image));
    bayerExtractDialog_->setResult(nullptr);
    pendingCoordinateInside_ = false;
    coordinateLabel_->setText(tr("坐标 —"));
    refreshFromDocumentState(false);
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
    const auto previous = documentSession_->displayMapping();
    domain::DisplayMapping mapping;
    mapping.blackPoint = blackPointSpin_->value();
    mapping.whitePoint = whitePointSpin_->value();
    mapping.gamma = gammaSpin_->value();
    const auto result = documentSession_->updateDisplayMapping(mapping);
    if (!result.valid) {
        taskLabel_->setText(QString::fromStdString(result.message));
        return;
    }
    if (mapping != previous) {
        // Demosaic materializes RGB bytes using the mapping captured at task
        // start. Never allow that stale result to replace the newer mapping.
        cancelDemosaic();
    }
    histogram_->setDisplayWindow(mapping.blackPoint, mapping.whitePoint);
    {
        const QSignalBlocker blockBlack(blackPointSpin_);
        const QSignalBlocker blockWhite(whitePointSpin_);
        blackPointSpin_->setMaximum(mapping.whitePoint - 0.1);
        whitePointSpin_->setMinimum(mapping.blackPoint + 0.1);
    }
    taskLabel_->setText(tr("显示 revision %1")
                            .arg(documentSession_->revision()));
    updateUndoActions();
    if (!displayRenderTimer_->isActive()) {
        displayRenderTimer_->start();
    }
}

void MainWindow::previewHistogramWindow(double blackPoint,
                                        double whitePoint) {
    if (!documentSession_) return;
    {
        const QSignalBlocker blockBlack(blackPointSpin_);
        const QSignalBlocker blockWhite(whitePointSpin_);
        blackPointSpin_->setValue(blackPoint);
        whitePointSpin_->setValue(whitePoint);
    }
}

void MainWindow::commitHistogramWindow() {
    if (!documentSession_) return;
    displayControlApplyTimer_->stop();
    applyDisplayControls();
    commitDisplayEdit();
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
    const auto range = application::globalHistogramRange(
        displaySource_ ? displaySource_->metadata
                       : documentSession_->original()->metadata);
    const double singleStep = std::max(
        0.1, (range.second - range.first) / 1000.0);
    blackPointSpin_->setRange(range.first, range.second);
    whitePointSpin_->setRange(range.first, range.second);
    blackPointSpin_->setSingleStep(singleStep);
    whitePointSpin_->setSingleStep(singleStep);
    blackPointSpin_->setValue(mapping.blackPoint);
    whitePointSpin_->setValue(mapping.whitePoint);
    blackPointSpin_->setMaximum(mapping.whitePoint - 0.1);
    whitePointSpin_->setMinimum(mapping.blackPoint + 0.1);
    gammaSpin_->setValue(mapping.gamma);
    const bool controlsUnavailable =
        displaySource_ && displaySource_->displayReadyRgb;
    blackPointSpin_->setEnabled(!controlsUnavailable);
    whitePointSpin_->setEnabled(!controlsUnavailable);
    gammaSpin_->setEnabled(!controlsUnavailable);
    resetDisplayButton_->setEnabled(!controlsUnavailable);
    histogram_->setDisplayWindow(mapping.blackPoint, mapping.whitePoint);
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
    imageLabel_->setText(metadataSummary(source->metadata));
}

void MainWindow::refreshFromDocumentState(bool preserveView,
                                          bool refreshStatistics) {
    if (!documentSession_) {
        return;
    }
    displayControlApplyTimer_->stop();
    displaySource_ = documentSession_->displaySource();
    preDemosaicSource_ = documentSession_->preDemosaicSource();
    bayerExtraction_ = documentSession_->bayerExtraction();
    beginGlobalHistogram();
    const bool statisticsCanRefresh = refreshStatistics && preserveView &&
        statisticsSelection_ && pixelStatisticsDialog_->isVisible() &&
        displaySource_ &&
        displaySource_->metadata.kind != application::ImageKind::Standard &&
        statisticsSelection_->x1 < displaySource_->metadata.width &&
        statisticsSelection_->y1 < displaySource_->metadata.height;

    pendingCoordinateInside_ = false;
    coordinateTimer_->stop();
    coordinateLabel_->setText(tr("坐标 —"));
    bayerExtractDialog_->setSource(&documentSession_->original()->metadata);
    bayerExtractDialog_->setResult(bayerExtraction_.get());
    pixelStatisticsDialog_->setSource(&displaySource_->metadata);
    filterDialog_->setSource(&displaySource_->metadata);
    demosaicDialog_->setSource(&displaySource_->metadata);
    if (preDemosaicSource_) {
        demosaicDialog_->setResult(&displaySource_->metadata);
    }
    if (!preserveView || !statisticsCanRefresh) {
        statisticsSelection_.reset();
        viewport_->clearStatisticsSelection();
    }
    syncDisplayControls();
    syncToolAvailability();
    renderCurrentDisplay(preserveView);
    updateUndoActions();
    if (statisticsCanRefresh) {
        const auto selection = *statisticsSelection_;
        QTimer::singleShot(0, this, [this, selection] {
            if (statisticsSelection_ && *statisticsSelection_ == selection) {
                beginPixelStatistics(selection);
            }
        });
    }
}

void MainWindow::applyHistoryStep(bool redo) {
    if (!documentSession_) {
        return;
    }
    if (displayControlApplyTimer_->isActive()) {
        displayControlApplyTimer_->stop();
        applyDisplayControls();
    }
    displayRenderTimer_->stop();
    const auto before = documentSession_->displaySource();
    cancelPipelineTasks();
    const bool changed = redo
        ? documentSession_->redo() : documentSession_->undo();
    if (!changed) {
        updateUndoActions();
        return;
    }
    const auto after = documentSession_->displaySource();
    const bool sameGeometry = before && after &&
        before->metadata.width == after->metadata.width &&
        before->metadata.height == after->metadata.height;
    refreshFromDocumentState(sameGeometry, true);
    taskLabel_->setText(redo ? tr("已重做编辑") : tr("已撤销编辑"));
}

void MainWindow::cancelPipelineTasks() {
    cancelPixelStatistics();
    cancelFilter();
    cancelDemosaic();
    cancelImageTransform();
    if (bayerCancellation_) {
        bayerCancellation_->store(true, std::memory_order_relaxed);
        bayerCancellation_.reset();
    }
    ++bayerGeneration_;
    if (bayerExtractDialog_) {
        bayerExtractDialog_->setBusy(false);
    }
}

void MainWindow::updateUndoActions() {
    const bool hasDocument = static_cast<bool>(documentSession_);
    undoAction_->setEnabled(hasDocument && documentSession_->canUndo());
    redoAction_->setEnabled(hasDocument && documentSession_->canRedo());
    for (auto* action : transformActions_) {
        action->setEnabled(hasDocument && documentSession_->displaySource() &&
                           documentSession_->displaySource()->pixels);
    }
}

void MainWindow::syncToolAvailability() {
    const bool hasDocument = documentSession_ && displaySource_;
    const bool currentRaw = hasDocument &&
        displaySource_->metadata.kind != application::ImageKind::Standard;
    const bool regularBayer = currentRaw &&
        displaySource_->metadata.bayerPattern != domain::BayerPattern::None;
    const bool originalRaw = documentSession_ &&
        documentSession_->original()->metadata.kind !=
            application::ImageKind::Standard;

    pixelInfoAction_->setEnabled(hasDocument);
    pixelInfoButton_->setEnabled(hasDocument);
    bayerExtractAction_->setEnabled(originalRaw);
    bayerExtractButton_->setEnabled(originalRaw);
    filterAction_->setEnabled(currentRaw);
    filterButton_->setEnabled(currentRaw);
    statisticsAction_->setEnabled(currentRaw);
    statisticsButton_->setEnabled(currentRaw);
    demosaicAction_->setEnabled(regularBayer);
    demosaicButton_->setEnabled(regularBayer);
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
        const QString channel = info.channel == domain::BayerChannel::None
            ? QString()
            : tr(" | %1").arg(
                QString::fromLatin1(domain::toString(info.channel)));
        coordinateLabel_->setText(
            tr("Extract %1 | 坐标 %2, %3 | Raw %4 | Display %5%6")
                .arg(QString::fromStdString(
                    bayerExtraction_->geometry.mask.name))
                .arg(pendingCoordinateX_)
                .arg(pendingCoordinateY_)
                .arg(info.originalValue, 0, 'g', 10)
                .arg(info.processedValue, 0, 'f', 4)
                .arg(channel));
        return;
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

void MainWindow::openFilter() {
    const auto source = displaySource_;
    if (!documentSession_ || !source ||
        source->metadata.kind == application::ImageKind::Standard) {
        return;
    }
    documentSession_->commitDisplayEdit();
    updateUndoActions();
    filterDialog_->setSource(&source->metadata);
    filterDialog_->show();
    filterDialog_->raise();
    filterDialog_->activateWindow();
}

void MainWindow::openDemosaic() {
    const auto source = displaySource_;
    if (!documentSession_ || !source ||
        source->metadata.kind == application::ImageKind::Standard ||
        source->metadata.bayerPattern == domain::BayerPattern::None) {
        return;
    }
    documentSession_->commitDisplayEdit();
    updateUndoActions();
    demosaicDialog_->setSource(&source->metadata);
    demosaicDialog_->show();
    demosaicDialog_->raise();
    demosaicDialog_->activateWindow();
}

void MainWindow::openBayerExtract() {
    if (!documentSession_ ||
        documentSession_->original()->metadata.kind ==
            application::ImageKind::Standard) {
        return;
    }
    bayerExtractDialog_->setSource(&documentSession_->original()->metadata);
    bayerExtractDialog_->show();
    bayerExtractDialog_->raise();
    bayerExtractDialog_->activateWindow();
}

void MainWindow::openPixelStatistics() {
    const auto source = displaySource_;
    if (!documentSession_ || !source) {
        return;
    }
    if (source->metadata.kind == application::ImageKind::Standard) {
        return;
    }
    pixelStatisticsDialog_->setSource(&source->metadata);
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
    const auto source = displaySource_;
    if (!documentSession_ || !source ||
        source->metadata.kind == application::ImageKind::Standard ||
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

    application::PixelStatisticsRequest baseRequest;
    baseRequest.source = source;
    baseRequest.mode = pixelStatisticsDialog_->mode();
    baseRequest.selection = selection;
    baseRequest.histogramBins = pixelStatisticsDialog_->histogramBins();
    baseRequest.cancellation = token;
    baseRequest.progressPermille = progress;
    const bool splitChannels = pixelStatisticsDialog_->channelsEnabled();
    pixelStatisticsDialog_->setSelection(selection);
    pixelStatisticsDialog_->setBusy(true, progress);
    taskLabel_->setText(tr("正在统计当前显示 RAW…"));

    auto* watcher = new QFutureWatcher<
        std::vector<application::PixelStatisticsResult>>(this);
    connect(watcher,
            &QFutureWatcher<
                std::vector<application::PixelStatisticsResult>>::finished,
            this,
            [this, watcher, generation, token] {
        const auto results = watcher->result();
        watcher->deleteLater();
        if (generation != statisticsGeneration_ ||
            token != statisticsCancellation_) {
            return;
        }
        pixelStatisticsDialog_->setBusy(false);
        const auto failed = std::find_if(
            results.begin(), results.end(), [](const auto& result) {
                return !result.succeeded() &&
                    result.errorCode != "statistics.empty_selection";
            });
        if (results.empty() || failed != results.end()) {
            const auto errorCode = results.empty()
                ? std::string("statistics.no_results")
                : failed->errorCode;
            const auto message = results.empty()
                ? std::string("No statistics results were produced.")
                : failed->message;
            if (errorCode == "task.cancelled") {
                taskLabel_->setText(tr("像素统计已取消"));
                pixelStatisticsDialog_->clearResult(tr("计算已取消。"));
            } else {
                taskLabel_->setText(tr("像素统计失败"));
                pixelStatisticsDialog_->clearResult(
                    QString::fromStdString(message));
            }
            return;
        }
        pixelStatisticsDialog_->setResults(results);
        std::uint64_t totalCount = 0;
        for (const auto& result : results) {
            totalCount += result.summary.count;
        }
        taskLabel_->setText(
            tr("统计完成：%1 个结果样本").arg(totalCount));
    });
    const auto service = pixelStatisticsService_;
    watcher->setFuture(QtConcurrent::run(
        [service, baseRequest, splitChannels] {
        if (splitChannels) {
            return service.executeChannels(baseRequest);
        }
        return std::vector<application::PixelStatisticsResult>{
            service.execute(baseRequest)};
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

void MainWindow::beginFilter() {
    const auto source = displaySource_;
    if (!documentSession_ || !source ||
        source->metadata.kind == application::ImageKind::Standard) {
        return;
    }
    documentSession_->commitDisplayEdit();
    updateUndoActions();
    cancelPipelineTasks();
    statisticsSelection_.reset();
    viewport_->clearStatisticsSelection();
    viewport_->setStatisticsSelectionTool(StatisticsSelectionTool::None);
    pixelStatisticsDialog_->hide();
    filterCancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto token = filterCancellation_;
    const auto generation = ++filterGeneration_;
    const auto request = filterDialog_->request(source, token);
    filterDialog_->setBusy(true, tr("Building bounded preview…"));
    taskLabel_->setText(tr("正在滤波当前显示 RAW…"));

    auto* watcher = new QFutureWatcher<application::FilterResult>(this);
    connect(watcher, &QFutureWatcher<application::FilterResult>::finished,
            this, [this, watcher, generation, token] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != filterGeneration_ || token != filterCancellation_) {
            return;
        }
        filterDialog_->setBusy(false);
        if (!result.succeeded()) {
            taskLabel_->setText(result.errorCode == "task.cancelled"
                ? tr("图像滤波已取消")
                : tr("图像滤波失败"));
            if (result.errorCode != "task.cancelled") {
                QMessageBox::warning(this, tr("Image Filter"),
                    QString::fromStdString(result.message));
            }
            return;
        }
        documentSession_->commitPipelineEdit(
            result.image, {}, bayerExtraction_);
        refreshFromDocumentState(true);
        filterDialog_->setResult(&displaySource_->metadata);
        taskLabel_->setText(tr("滤波完成：%1")
            .arg(QString::fromStdString(displaySource_->metadata.format)));
    });
    const auto service = filterService_;
    watcher->setFuture(QtConcurrent::run([service, request] {
        return service.execute(request);
    }));
}

void MainWindow::cancelFilter() {
    if (filterCancellation_) {
        filterCancellation_->store(true, std::memory_order_relaxed);
        filterCancellation_.reset();
    }
    ++filterGeneration_;
    if (filterDialog_) {
        filterDialog_->setBusy(false);
    }
}

void MainWindow::beginDemosaic() {
    const auto source = displaySource_;
    if (!documentSession_ || !source ||
        source->metadata.kind == application::ImageKind::Standard ||
        source->metadata.bayerPattern == domain::BayerPattern::None) {
        return;
    }
    documentSession_->commitDisplayEdit();
    updateUndoActions();
    cancelPipelineTasks();
    statisticsSelection_.reset();
    viewport_->clearStatisticsSelection();
    viewport_->setStatisticsSelectionTool(StatisticsSelectionTool::None);
    pixelStatisticsDialog_->hide();
    demosaicCancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto token = demosaicCancellation_;
    const auto generation = ++demosaicGeneration_;
    const auto request = demosaicDialog_->request(
        source, documentSession_->displayMapping(), token);
    demosaicDialog_->setBusy(true, tr("Building bounded RGB preview…"));
    taskLabel_->setText(tr("正在对当前 Bayer RAW 去马赛克…"));

    auto* watcher = new QFutureWatcher<application::DemosaicResult>(this);
    connect(watcher, &QFutureWatcher<application::DemosaicResult>::finished,
            this, [this, watcher, generation, token, source] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != demosaicGeneration_ ||
            token != demosaicCancellation_) {
            return;
        }
        demosaicDialog_->setBusy(false);
        if (!result.succeeded()) {
            taskLabel_->setText(result.errorCode == "task.cancelled"
                ? tr("Demosaic 已取消") : tr("Demosaic 失败"));
            if (result.errorCode != "task.cancelled") {
                QMessageBox::warning(this, tr("Bayer Demosaic"),
                    QString::fromStdString(result.message));
            }
            return;
        }
        documentSession_->commitPipelineEdit(
            result.image, source, bayerExtraction_);
        refreshFromDocumentState(true);
        demosaicDialog_->setResult(&displaySource_->metadata);
        taskLabel_->setText(tr("Demosaic 完成：%1")
            .arg(QString::fromStdString(displaySource_->metadata.format)));
    });
    const auto service = demosaicService_;
    watcher->setFuture(QtConcurrent::run([service, request] {
        return service.execute(request);
    }));
}

void MainWindow::cancelDemosaic() {
    if (demosaicCancellation_) {
        demosaicCancellation_->store(true, std::memory_order_relaxed);
        demosaicCancellation_.reset();
    }
    ++demosaicGeneration_;
    if (demosaicDialog_) {
        demosaicDialog_->setBusy(false);
    }
}

void MainWindow::restoreDemosaicSource() {
    if (!documentSession_ || !preDemosaicSource_) {
        return;
    }
    documentSession_->commitDisplayEdit();
    updateUndoActions();
    cancelPipelineTasks();
    documentSession_->commitPipelineEdit(
        preDemosaicSource_, {}, bayerExtraction_);
    refreshFromDocumentState(true);
    taskLabel_->setText(tr("已恢复 Demosaic 输入 Bayer RAW"));
}

void MainWindow::beginImageTransform(application::ImageTransform transform) {
    const auto source = displaySource_;
    if (!documentSession_ || !source || !source->pixels) {
        return;
    }
    documentSession_->commitDisplayEdit();
    updateUndoActions();
    cancelPipelineTasks();
    statisticsSelection_.reset();
    viewport_->clearStatisticsSelection();
    viewport_->setStatisticsSelectionTool(StatisticsSelectionTool::None);
    pixelStatisticsDialog_->hide();
    imageTransformCancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto token = imageTransformCancellation_;
    const auto generation = ++imageTransformGeneration_;
    application::ImageTransformRequest request;
    request.source = source;
    request.transform = transform;
    request.cancellation = token;
    taskLabel_->setText(tr("正在执行 %1…")
        .arg(QString::fromLatin1(application::toString(transform))));

    auto* watcher =
        new QFutureWatcher<application::ImageTransformResult>(this);
    connect(watcher,
            &QFutureWatcher<application::ImageTransformResult>::finished,
            this,
            [this, watcher, generation, token, source, transform] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != imageTransformGeneration_ ||
            token != imageTransformCancellation_ || !documentSession_ ||
            source != documentSession_->displaySource()) {
            return;
        }
        imageTransformCancellation_.reset();
        if (!result.succeeded()) {
            taskLabel_->setText(result.errorCode == "task.cancelled"
                ? tr("图像变换已取消") : tr("图像变换失败"));
            if (result.errorCode != "task.cancelled") {
                QMessageBox::warning(this, tr("Image Transform"),
                    QString::fromStdString(result.message));
            }
            return;
        }
        const bool sameGeometry =
            source->metadata.width == result.image->metadata.width &&
            source->metadata.height == result.image->metadata.height;
        documentSession_->commitPipelineEdit(
            result.image, preDemosaicSource_, bayerExtraction_);
        refreshFromDocumentState(sameGeometry);
        taskLabel_->setText(tr("已完成 %1")
            .arg(QString::fromLatin1(application::toString(transform))));
    });
    const auto service = imageTransformService_;
    watcher->setFuture(QtConcurrent::run([service, request] {
        return service.execute(request);
    }));
}

void MainWindow::cancelImageTransform() {
    if (imageTransformCancellation_) {
        imageTransformCancellation_->store(true, std::memory_order_relaxed);
        imageTransformCancellation_.reset();
    }
    ++imageTransformGeneration_;
}

void MainWindow::beginGlobalHistogram() {
    const auto source = displaySource_;
    if (!source || !source->pixels) {
        cancelGlobalHistogram();
        histogram_->setLoading(false);
        histogramZoomButton_->setEnabled(false);
        return;
    }
    if (globalHistogramSource_ == source) return;

    cancelGlobalHistogram();
    globalHistogramSource_ = source;
    {
        const QSignalBlocker blocker(histogramZoomButton_);
        histogramZoomButton_->setChecked(false);
    }
    histogram_->setZoomed(false);
    histogram_->setLoading(true);
    histogramZoomButton_->setEnabled(false);
    globalHistogramCancellation_ =
        std::make_shared<std::atomic_bool>(false);
    const auto token = globalHistogramCancellation_;
    const auto generation = ++globalHistogramGeneration_;
    application::GlobalHistogramRequest request;
    request.source = source;
    request.binCount = source->metadata.kind == application::ImageKind::Standard ||
            source->displayReadyRgb
        ? 256U : 65536U;
    request.cancellation = token;

    auto* watcher =
        new QFutureWatcher<application::GlobalHistogramResult>(this);
    connect(watcher,
            &QFutureWatcher<application::GlobalHistogramResult>::finished,
            this,
            [this, watcher, generation, token, source] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (generation != globalHistogramGeneration_ ||
            token != globalHistogramCancellation_ ||
            source != displaySource_) {
            return;
        }
        globalHistogramCancellation_.reset();
        histogram_->setResult(std::move(result));
        histogram_->setDisplayWindow(
            documentSession_->displayMapping().blackPoint,
            documentSession_->displayMapping().whitePoint);
        histogramZoomButton_->setEnabled(
            histogram_->seriesCount() != 0);
    });
    const auto service = globalHistogramService_;
    watcher->setFuture(QtConcurrent::run([service, request] {
        return service.execute(request);
    }));
}

void MainWindow::cancelGlobalHistogram() {
    if (globalHistogramCancellation_) {
        globalHistogramCancellation_->store(true, std::memory_order_relaxed);
        globalHistogramCancellation_.reset();
    }
    ++globalHistogramGeneration_;
    globalHistogramSource_.reset();
}

void MainWindow::beginBayerExtraction() {
    if (!documentSession_) {
        return;
    }
    documentSession_->commitDisplayEdit();
    updateUndoActions();
    cancelPipelineTasks();
    statisticsSelection_.reset();
    viewport_->setStatisticsSelectionTool(StatisticsSelectionTool::None);
    pixelStatisticsDialog_->hide();
    if (exportCancellation_) {
        exportCancellation_->store(true);
        exportCancellation_.reset();
    }
    bayerCancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto token = bayerCancellation_;
    const auto generation = ++bayerGeneration_;
    const auto request = bayerExtractDialog_->request(
        documentSession_->original(), token);
    bayerExtractDialog_->setBusy(true, tr("正在生成 Bayer 掩码预览…"));
    taskLabel_->setText(tr("正在执行 Bayer pattern 提取…"));

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
        documentSession_->commitPipelineEdit(
            result.extraction->image, {}, result.extraction);
        refreshFromDocumentState(false);
        taskLabel_->setText(tr("已显示 %1 提取结果")
            .arg(QString::fromStdString(bayerExtraction_->geometry.mask.name)));
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
    documentSession_->commitDisplayEdit();
    updateUndoActions();
    cancelPipelineTasks();
    statisticsSelection_.reset();
    viewport_->clearStatisticsSelection();
    documentSession_->commitPipelineEdit(
        documentSession_->original(), {}, bayerExtraction_);
    refreshFromDocumentState(false);
    taskLabel_->setText(tr("已显示原始 Bayer 图像"));
}

void MainWindow::exportBayerCsv() {
    if (!bayerExtraction_ || !bayerExporter_) {
        return;
    }
    QString safeName = QString::fromStdString(
        bayerExtraction_->geometry.mask.name).toLower();
    safeName.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]+")),
                     QStringLiteral("_"));
    const QString suggested = QStringLiteral("bayer_%1.csv").arg(safeName);
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("导出 Bayer pattern CSV"),
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
