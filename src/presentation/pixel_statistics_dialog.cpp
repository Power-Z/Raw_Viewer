#include "presentation/pixel_statistics_dialog.h"

#include "presentation/statistics_chart_widget.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>

namespace rawviewer::presentation {
namespace {

QString modeName(application::PixelStatisticsMode mode) {
    switch (mode) {
    case application::PixelStatisticsMode::Status: return QObject::tr("Status");
    case application::PixelStatisticsMode::HorizontalBox:
        return QObject::tr("Horizontal Box");
    case application::PixelStatisticsMode::VerticalBox:
        return QObject::tr("Vertical Box");
    case application::PixelStatisticsMode::Line: return QObject::tr("Line");
    case application::PixelStatisticsMode::WhiteBalance: return QObject::tr("WB");
    }
    return {};
}

QString number(double value) {
    return QLocale().toString(value, 'f', 4);
}

} // namespace

PixelStatisticsDialog::PixelStatisticsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Pixel Statistics"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(1120, 780);
    setMinimumSize(860, 620);
    setObjectName(QStringLiteral("pixelStatisticsDialog"));
    setStyleSheet(QStringLiteral(
        "#pixelStatisticsDialog QFrame[panel=\"true\"] {"
        " border: 1px solid palette(mid); border-radius: 10px;"
        " background: palette(base); }"
        "#pixelStatisticsDialog QToolButton { padding: 8px 16px;"
        " border-radius: 7px; font-weight: 600; }"
        "#pixelStatisticsDialog QToolButton:checked {"
        " background: palette(highlight); color: palette(highlighted-text); }"
        "#pixelStatisticsDialog QLabel[role=\"metric\"] {"
        " font-size: 15px; font-weight: 600; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* top = new QFrame(this);
    top->setProperty("panel", true);
    auto* topLayout = new QHBoxLayout(top);
    auto* title = new QLabel(tr("PIXEL STATISTICS"), top);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
    title->setFont(titleFont);
    topLayout->addWidget(title);
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);
    const std::array modes{
        application::PixelStatisticsMode::Status,
        application::PixelStatisticsMode::HorizontalBox,
        application::PixelStatisticsMode::VerticalBox,
        application::PixelStatisticsMode::Line,
        application::PixelStatisticsMode::WhiteBalance
    };
    for (const auto mode : modes) {
        auto* button = new QToolButton(top);
        button->setText(modeName(mode));
        button->setCheckable(true);
        button->setProperty("mode", static_cast<int>(mode));
        modeGroup_->addButton(button, static_cast<int>(mode));
        topLayout->addWidget(button);
    }
    modeGroup_->button(static_cast<int>(mode_))->setChecked(true);
    topLayout->addStretch();
    auto* close = new QToolButton(top);
    close->setText(QStringLiteral("×"));
    close->setToolTip(tr("关闭"));
    topLayout->addWidget(close);
    root->addWidget(top, 1);

    auto* controls = new QFrame(this);
    controls->setProperty("panel", true);
    auto* controlLayout = new QGridLayout(controls);
    controlLayout->setContentsMargins(16, 12, 16, 12);
    instructionLabel_ = new QLabel(controls);
    instructionLabel_->setWordWrap(true);
    instructionLabel_->setProperty("role", "metric");
    controlLayout->addWidget(instructionLabel_, 0, 0, 1, 6);

    channelCombo_ = new QComboBox(controls);
    channelCombo_->addItem(tr("All Bayer samples"),
                           static_cast<int>(domain::BayerChannel::None));
    channelCombo_->addItem(QStringLiteral("R"),
                           static_cast<int>(domain::BayerChannel::R));
    channelCombo_->addItem(QStringLiteral("Gr"),
                           static_cast<int>(domain::BayerChannel::Gr));
    channelCombo_->addItem(QStringLiteral("Gb"),
                           static_cast<int>(domain::BayerChannel::Gb));
    channelCombo_->addItem(QStringLiteral("B"),
                           static_cast<int>(domain::BayerChannel::B));
    binsCombo_ = new QComboBox(controls);
    for (const int bins : {64, 128, 256, 512, 1024}) {
        binsCombo_->addItem(QString::number(bins), bins);
    }
    binsCombo_->setCurrentText(QStringLiteral("256"));
    gridCheck_ = new QCheckBox(tr("Grid"), controls);
    gridCheck_->setChecked(true);
    pointsCheck_ = new QCheckBox(tr("Data points"), controls);
    fillCheck_ = new QCheckBox(tr("Histogram fill"), controls);
    fillCheck_->setChecked(true);
    lineWidthCombo_ = new QComboBox(controls);
    lineWidthCombo_->addItems({"1 px", "2 px", "3 px", "4 px"});
    lineWidthCombo_->setCurrentIndex(1);
    controlLayout->addWidget(new QLabel(tr("Channel"), controls), 1, 0);
    controlLayout->addWidget(channelCombo_, 1, 1);
    controlLayout->addWidget(new QLabel(tr("Histogram bins"), controls), 1, 2);
    controlLayout->addWidget(binsCombo_, 1, 3);
    controlLayout->addWidget(new QLabel(tr("Line width"), controls), 1, 4);
    controlLayout->addWidget(lineWidthCombo_, 1, 5);
    controlLayout->addWidget(gridCheck_, 2, 0);
    controlLayout->addWidget(pointsCheck_, 2, 1);
    controlLayout->addWidget(fillCheck_, 2, 2);

    selectionLabel_ = new QLabel(tr("Selection —"), controls);
    summaryLabel_ = new QLabel(tr("等待选择区域"), controls);
    summaryLabel_->setTextFormat(Qt::RichText);
    summaryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summaryLabel_->setProperty("role", "metric");
    controlLayout->addWidget(selectionLabel_, 3, 0, 1, 2);
    controlLayout->addWidget(summaryLabel_, 3, 2, 1, 4);
    progressBar_ = new QProgressBar(controls);
    progressBar_->setRange(0, 1000);
    progressBar_->setValue(0);
    progressBar_->setFormat(tr("Ready"));
    cancelButton_ = new QPushButton(tr("Cancel"), controls);
    cancelButton_->setEnabled(false);
    controlLayout->addWidget(progressBar_, 4, 0, 1, 5);
    controlLayout->addWidget(cancelButton_, 4, 5);
    root->addWidget(controls, 2);

    auto* bottom = new QFrame(this);
    bottom->setProperty("panel", true);
    auto* bottomLayout = new QVBoxLayout(bottom);
    chart_ = new StatisticsChartWidget(bottom);
    bottomLayout->addWidget(chart_);
    root->addWidget(bottom, 5);

    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(80);
    connect(progressTimer_, &QTimer::timeout,
            this, &PixelStatisticsDialog::refreshProgress);
    connect(modeGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        selectMode(static_cast<application::PixelStatisticsMode>(id));
    });
    connect(channelCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PixelStatisticsDialog::optionsChanged);
    connect(binsCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PixelStatisticsDialog::optionsChanged);
    connect(gridCheck_, &QCheckBox::toggled,
            chart_, &StatisticsChartWidget::setShowGrid);
    connect(pointsCheck_, &QCheckBox::toggled,
            chart_, &StatisticsChartWidget::setShowPoints);
    connect(fillCheck_, &QCheckBox::toggled,
            chart_, &StatisticsChartWidget::setFillHistogram);
    connect(lineWidthCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) { chart_->setLineWidth(index + 1); });
    connect(cancelButton_, &QPushButton::clicked,
            this, &PixelStatisticsDialog::cancelRequested);
    connect(close, &QToolButton::clicked, this, [this] {
        emit toolClosed();
        hide();
    });
    updateInstructions();
}

void PixelStatisticsDialog::setSource(
    const application::ImageMetadata* metadata) {
    sourceSupported_ = metadata && metadata->kind != application::ImageKind::Standard &&
        metadata->bayerPattern != domain::BayerPattern::None;
    channelCombo_->setEnabled(sourceSupported_);
    binsCombo_->setEnabled(
        sourceSupported_ && mode_ == application::PixelStatisticsMode::Status);
    for (auto* button : modeGroup_->buttons()) {
        button->setEnabled(sourceSupported_);
    }
    clearResult(sourceSupported_
        ? tr("选择统计模式，然后在原始 Bayer RAW 上选择区域。")
        : tr("Pixel Statistics 仅支持原始 Bayer RAW。"));
    selectionLabel_->setText(tr("Selection —"));
    updateInstructions();
}

application::PixelStatisticsMode PixelStatisticsDialog::mode() const noexcept {
    return mode_;
}

domain::BayerChannel PixelStatisticsDialog::channel() const noexcept {
    return static_cast<domain::BayerChannel>(channelCombo_->currentData().toInt());
}

std::uint32_t PixelStatisticsDialog::histogramBins() const noexcept {
    return static_cast<std::uint32_t>(binsCombo_->currentData().toUInt());
}

void PixelStatisticsDialog::setSelection(
    const application::StatisticsSelection& selection) {
    selectionLabel_->setText(
        tr("Selection (%1, %2) → (%3, %4)")
            .arg(selection.x0)
            .arg(selection.y0)
            .arg(selection.x1)
            .arg(selection.y1));
}

void PixelStatisticsDialog::setBusy(
    bool busy,
    std::shared_ptr<std::atomic_uint32_t> progress) {
    progress_ = std::move(progress);
    cancelButton_->setEnabled(busy);
    channelCombo_->setEnabled(sourceSupported_ && !busy);
    binsCombo_->setEnabled(
        sourceSupported_ && !busy &&
        mode_ == application::PixelStatisticsMode::Status);
    if (busy) {
        progressBar_->setValue(0);
        progressBar_->setFormat(tr("Calculating %p%"));
        progressTimer_->start();
        summaryLabel_->setText(tr("正在扫描原始像素…"));
    } else {
        progressTimer_->stop();
        progress_.reset();
    }
}

void PixelStatisticsDialog::setResult(
    const application::PixelStatisticsResult& result) {
    setBusy(false);
    const auto& value = result.summary;
    summaryLabel_->setText(
        tr("<b>Count</b> %1 &nbsp;&nbsp; <b>Mean</b> %2 &nbsp;&nbsp; "
           "<b>Min</b> %3 &nbsp;&nbsp; <b>Max</b> %4 &nbsp;&nbsp; "
           "<b>Std</b> %5")
            .arg(QLocale().toString(static_cast<qulonglong>(value.count)))
            .arg(number(value.mean))
            .arg(number(value.minimum))
            .arg(number(value.maximum))
            .arg(number(value.standardDeviation)));
    progressBar_->setValue(1000);
    progressBar_->setFormat(tr("Complete"));
    chart_->setResult(result);
}

void PixelStatisticsDialog::clearResult(const QString& message) {
    setBusy(false);
    summaryLabel_->setText(message.isEmpty() ? tr("等待选择区域") : message);
    progressBar_->setValue(0);
    progressBar_->setFormat(tr("Ready"));
    chart_->clear(message);
}

void PixelStatisticsDialog::selectMode(
    application::PixelStatisticsMode mode) {
    mode_ = mode;
    binsCombo_->setEnabled(sourceSupported_ &&
        mode == application::PixelStatisticsMode::Status);
    fillCheck_->setEnabled(mode == application::PixelStatisticsMode::Status);
    pointsCheck_->setEnabled(mode != application::PixelStatisticsMode::Status);
    clearResult(mode == application::PixelStatisticsMode::WhiteBalance
        ? tr("WB 入口已预留，本版本不执行计算。")
        : tr("请在图像中单击起点，再次单击终点。"));
    updateInstructions();
    emit modeChanged(mode_);
}

void PixelStatisticsDialog::refreshProgress() {
    if (!progress_) {
        return;
    }
    progressBar_->setValue(static_cast<int>(
        progress_->load(std::memory_order_relaxed)));
}

void PixelStatisticsDialog::updateInstructions() {
    if (!sourceSupported_) {
        instructionLabel_->setText(tr("打开带 Bayer pattern 的 RAW 后可用。"));
        return;
    }
    switch (mode_) {
    case application::PixelStatisticsMode::Status:
        instructionLabel_->setText(
            tr("STATUS · 矩形区域原始值统计与灰度直方图"));
        break;
    case application::PixelStatisticsMode::HorizontalBox:
        instructionLabel_->setText(
            tr("HORIZONTAL BOX · 每列沿 Y 方向平均，绘制水平 profile"));
        break;
    case application::PixelStatisticsMode::VerticalBox:
        instructionLabel_->setText(
            tr("VERTICAL BOX · 每行沿 X 方向平均，绘制垂直 profile"));
        break;
    case application::PixelStatisticsMode::Line:
        instructionLabel_->setText(
            tr("LINE · 沿线段最近邻采样，横轴为累计像素距离"));
        break;
    case application::PixelStatisticsMode::WhiteBalance:
        instructionLabel_->setText(tr("WB · Reserved"));
        break;
    }
}

void PixelStatisticsDialog::closeEvent(QCloseEvent* event) {
    emit toolClosed();
    QDialog::closeEvent(event);
}

void PixelStatisticsDialog::reject() {
    emit toolClosed();
    QDialog::reject();
}

} // namespace rawviewer::presentation
