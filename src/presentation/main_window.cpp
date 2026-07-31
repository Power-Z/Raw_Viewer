#include "presentation/main_window.h"

#include "presentation/histogram_widget.h"
#include "presentation/image_viewport.h"
#include "presentation/pixel_info_dialog.h"
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
    QWidget* parent)
    : QMainWindow(parent),
      openService_(std::move(openService)) {
    setWindowTitle(tr("Raw Viewer"));
    resize(1440, 900);
    pixelInfoDialog_ = new PixelInfoDialog(this);
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
    connect(pixelInfoDialog_,
            &PixelInfoDialog::optionsChanged,
            viewport_,
            &ImageViewport::setPixelOverlayOptions);
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
            tr("图像 (*.raw *.RAW *.bin *.jpg *.jpeg *.png *.bmp *.3fr *.dng);;所有文件 (*)"));
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
    auto* statisticsAction = toolMenu->addAction(tr("Pixel Statistics（V0.3）"));
    statisticsAction->setEnabled(false);
    auto* extractAction = toolMenu->addAction(tr("Bayer Extract（V0.3）"));
    extractAction->setEnabled(false);

    auto* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("关于 Raw Viewer"), this, [this] {
        QMessageBox::about(
            this,
            tr("关于 Raw Viewer"),
            tr("<b>Raw Viewer 0.3.0-dev</b><br>"
               "第二阶段：非破坏显示处理、五步撤销与 Pixel Info。<br>"
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
    form->addRow(tr("Header bytes"), headerSpin_);
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
    for (const QString& name : {
             "Pixel Statistics — V0.3",
             "Bayer Extract — V0.3",
             "ISP tools — reserved"}) {
        auto* button = new QPushButton(name, tools);
        button->setEnabled(false);
        toolLayout->addWidget(button);
    }
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
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto token = cancellation_;
    const auto generation = ++generation_;
    taskLabel_->setText(tr("正在打开…"));
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
    syncDisplayControls();
    renderCurrentDisplay(false);
    updateUndoActions();
    pixelInfoAction_->setEnabled(true);
    pixelInfoButton_->setEnabled(true);
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
    blackPointSpin_->setEnabled(true);
    whitePointSpin_->setEnabled(true);
    gammaSpin_->setEnabled(true);
    resetDisplayButton_->setEnabled(true);
}

void MainWindow::renderCurrentDisplay(bool preserveView) {
    if (!documentSession_) {
        return;
    }
    currentImage_ = application::PreviewRenderer::render(
        documentSession_->original(),
        documentSession_->displayMapping());
    if (!currentImage_) {
        return;
    }
    viewport_->setDisplayMapping(documentSession_->displayMapping());
    viewport_->setImage(currentImage_, preserveView);
    histogram_->setImage(currentImage_);
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
        !documentSession_->original()->pixels) {
        coordinateLabel_->setText(tr("坐标 —"));
        return;
    }
    const auto info = application::queryPixelInfo(
        *documentSession_->original(),
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

} // namespace rawviewer::presentation
