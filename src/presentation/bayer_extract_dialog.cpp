#include "presentation/bayer_extract_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace rawviewer::presentation {
namespace {

int spinMaximum(std::uint64_t value) {
    return static_cast<int>(std::min<std::uint64_t>(
        value, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
}

} // namespace

BayerExtractDialog::BayerExtractDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Bayer Extract"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(430, 340);

    auto* layout = new QVBoxLayout(this);
    sourceLabel_ = new QLabel(tr("尚未打开 Bayer RAW。"), this);
    sourceLabel_->setWordWrap(true);
    layout->addWidget(sourceLabel_);

    auto* form = new QFormLayout();
    channelCombo_ = new QComboBox(this);
    channelCombo_->addItems({"R", "Gr", "Gb", "B"});
    fullImageCheck_ = new QCheckBox(tr("使用完整图像"), this);
    fullImageCheck_->setChecked(true);
    xSpin_ = new QSpinBox(this);
    ySpin_ = new QSpinBox(this);
    widthSpin_ = new QSpinBox(this);
    heightSpin_ = new QSpinBox(this);
    for (auto* spin : {xSpin_, ySpin_, widthSpin_, heightSpin_}) {
        spin->setGroupSeparatorShown(true);
    }
    form->addRow(tr("通道"), channelCombo_);
    form->addRow(tr("源区域"), fullImageCheck_);
    form->addRow(tr("X"), xSpin_);
    form->addRow(tr("Y"), ySpin_);
    form->addRow(tr("Width"), widthSpin_);
    form->addRow(tr("Height"), heightSpin_);
    layout->addLayout(form);

    resultLabel_ = new QLabel(
        tr("提取结果使用 2×2 步长的只读视图，不复制完整通道数据。"),
        this);
    resultLabel_->setWordWrap(true);
    layout->addWidget(resultLabel_);

    auto* actions = new QHBoxLayout();
    extractButton_ = new QPushButton(tr("提取并显示"), this);
    originalButton_ = new QPushButton(tr("显示原图"), this);
    exportButton_ = new QPushButton(tr("导出 CSV..."), this);
    extractButton_->setEnabled(false);
    originalButton_->setEnabled(false);
    exportButton_->setEnabled(false);
    actions->addWidget(extractButton_);
    actions->addWidget(originalButton_);
    actions->addWidget(exportButton_);
    layout->addLayout(actions);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QWidget::hide);
    layout->addWidget(buttons);

    connect(fullImageCheck_, &QCheckBox::toggled,
            this, &BayerExtractDialog::syncRegionControls);
    connect(extractButton_, &QPushButton::clicked,
            this, &BayerExtractDialog::extractRequested);
    connect(originalButton_, &QPushButton::clicked,
            this, &BayerExtractDialog::showOriginalRequested);
    connect(exportButton_, &QPushButton::clicked,
            this, &BayerExtractDialog::exportRequested);
    syncRegionControls();
}

void BayerExtractDialog::setSource(
    const application::ImageMetadata* metadata) {
    sourceSupported_ = metadata &&
        metadata->bayerPattern != domain::BayerPattern::None &&
        metadata->kind != application::ImageKind::Standard;
    sourceWidth_ = metadata ? metadata->width : 0;
    sourceHeight_ = metadata ? metadata->height : 0;
    if (!sourceSupported_) {
        sourceLabel_->setText(metadata
            ? tr("当前图像没有受支持的 Bayer pattern。")
            : tr("尚未打开 Bayer RAW。"));
        extractButton_->setEnabled(false);
        originalButton_->setEnabled(false);
        exportButton_->setEnabled(false);
        return;
    }

    sourceLabel_->setText(
        tr("源图像：%1 × %2，%3")
            .arg(sourceWidth_)
            .arg(sourceHeight_)
            .arg(QString::fromLatin1(
                domain::toString(metadata->bayerPattern))));
    xSpin_->setRange(0, std::max(0, spinMaximum(sourceWidth_) - 1));
    ySpin_->setRange(0, std::max(0, spinMaximum(sourceHeight_) - 1));
    widthSpin_->setRange(1, std::max(1, spinMaximum(sourceWidth_)));
    heightSpin_->setRange(1, std::max(1, spinMaximum(sourceHeight_)));
    xSpin_->setValue(0);
    ySpin_->setValue(0);
    widthSpin_->setValue(spinMaximum(sourceWidth_));
    heightSpin_->setValue(spinMaximum(sourceHeight_));
    extractButton_->setEnabled(true);
    originalButton_->setEnabled(true);
    exportButton_->setEnabled(false);
    resultLabel_->setText(
        tr("提取结果使用 2×2 步长的只读视图，不复制完整通道数据。"));
    syncRegionControls();
}

application::BayerExtractRequest BayerExtractDialog::request(
    std::shared_ptr<const application::DecodedImage> source,
    std::shared_ptr<std::atomic_bool> cancellation) const {
    application::BayerExtractRequest result;
    result.source = std::move(source);
    result.channel = selectedChannel();
    result.cancellation = std::move(cancellation);
    if (!fullImageCheck_->isChecked()) {
        result.sourceRegion = application::PixelRegion{
            static_cast<std::uint64_t>(xSpin_->value()),
            static_cast<std::uint64_t>(ySpin_->value()),
            static_cast<std::uint64_t>(widthSpin_->value()),
            static_cast<std::uint64_t>(heightSpin_->value())
        };
    }
    return result;
}

void BayerExtractDialog::setBusy(bool busy, const QString& message) {
    extractButton_->setEnabled(sourceSupported_ && !busy);
    originalButton_->setEnabled(sourceSupported_ && !busy);
    exportButton_->setEnabled(exportButton_->property("hasResult").toBool() &&
                              !busy);
    channelCombo_->setEnabled(!busy);
    fullImageCheck_->setEnabled(!busy);
    syncRegionControls();
    if (!message.isEmpty()) {
        resultLabel_->setText(message);
    }
}

void BayerExtractDialog::setResult(
    const application::BayerExtraction* extraction) {
    const bool hasResult = extraction && extraction->image;
    exportButton_->setProperty("hasResult", hasResult);
    exportButton_->setEnabled(hasResult);
    if (!hasResult) {
        resultLabel_->setText(
            tr("提取结果使用 2×2 步长的只读视图，不复制完整通道数据。"));
        return;
    }
    const auto& geometry = extraction->geometry;
    resultLabel_->setText(
        tr("%1 通道：%2 × %3；源起点 (%4, %5)，步长 2×2。")
            .arg(QString::fromLatin1(domain::toString(geometry.channel)))
            .arg(geometry.width)
            .arg(geometry.height)
            .arg(geometry.sourceOriginX)
            .arg(geometry.sourceOriginY));
}

void BayerExtractDialog::syncRegionControls() {
    const bool enabled = sourceSupported_ &&
        !fullImageCheck_->isChecked() && fullImageCheck_->isEnabled();
    for (auto* spin : {xSpin_, ySpin_, widthSpin_, heightSpin_}) {
        spin->setEnabled(enabled);
    }
}

domain::BayerChannel BayerExtractDialog::selectedChannel() const noexcept {
    switch (channelCombo_->currentIndex()) {
    case 0: return domain::BayerChannel::R;
    case 1: return domain::BayerChannel::Gr;
    case 2: return domain::BayerChannel::Gb;
    case 3: return domain::BayerChannel::B;
    default: return domain::BayerChannel::None;
    }
}

} // namespace rawviewer::presentation
