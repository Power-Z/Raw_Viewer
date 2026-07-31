#include "presentation/pixel_info_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace rawviewer::presentation {

PixelInfoDialog::PixelInfoDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Pixel Info"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(360, 320);

    auto* layout = new QVBoxLayout(this);
    enabledCheck_ = new QCheckBox(tr("启用像素标注"), this);
    enabledCheck_->setChecked(true);
    layout->addWidget(enabledCheck_);

    auto* form = new QFormLayout();
    originalCheck_ = new QCheckBox(tr("原始信号"), this);
    originalCheck_->setChecked(true);
    processedCheck_ = new QCheckBox(tr("处理后值"), this);
    rgbCheck_ = new QCheckBox(tr("RGB 显示值"), this);
    meshCheck_ = new QCheckBox(tr("Bayer mesh"), this);
    meshCheck_->setChecked(true);
    bayerLabelCheck_ = new QCheckBox(tr("Bayer pattern 英文字标注"), this);
    bayerLabelCheck_->setChecked(true);
    thresholdSpin_ = new QDoubleSpinBox(this);
    thresholdSpin_->setRange(16.0, 256.0);
    thresholdSpin_->setDecimals(0);
    thresholdSpin_->setSuffix(tr(" px/像素"));
    thresholdSpin_->setValue(40.0);
    maximumLabelsSpin_ = new QSpinBox(this);
    maximumLabelsSpin_->setRange(1, 1000);
    maximumLabelsSpin_->setValue(200);

    form->addRow(tr("显示"), originalCheck_);
    form->addRow(QString(), processedCheck_);
    form->addRow(QString(), rgbCheck_);
    form->addRow(tr("网格"), meshCheck_);
    form->addRow(QString(), bayerLabelCheck_);
    form->addRow(tr("标注阈值"), thresholdSpin_);
    form->addRow(tr("标签上限"), maximumLabelsSpin_);
    layout->addLayout(form);

    auto* note = new QLabel(
        tr("只有单个源像素达到标注阈值时才绘制文字；标签数量受上限约束。"),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QWidget::hide);
    layout->addWidget(buttons);

    for (auto* check : {enabledCheck_, originalCheck_, processedCheck_,
                        rgbCheck_, meshCheck_, bayerLabelCheck_}) {
        connect(check, &QCheckBox::toggled,
                this, &PixelInfoDialog::publishOptions);
    }
    connect(thresholdSpin_,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            &PixelInfoDialog::publishOptions);
    connect(maximumLabelsSpin_,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            &PixelInfoDialog::publishOptions);
}

PixelOverlayOptions PixelInfoDialog::options() const {
    PixelOverlayOptions result;
    result.enabled = enabledCheck_->isChecked();
    result.showOriginal = originalCheck_->isChecked();
    result.showProcessed = processedCheck_->isChecked();
    result.showRgb = rgbCheck_->isChecked();
    result.showMesh = meshCheck_->isChecked();
    result.showBayerLabel = bayerLabelCheck_->isChecked();
    result.minimumCellPixels = thresholdSpin_->value();
    result.maximumLabels = maximumLabelsSpin_->value();
    return result;
}

void PixelInfoDialog::publishOptions() {
    emit optionsChanged(options());
}

} // namespace rawviewer::presentation
