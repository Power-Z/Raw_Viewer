#include "presentation/pixel_info_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QVBoxLayout>

namespace rawviewer::presentation {

PixelInfoDialog::PixelInfoDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Pixel Info"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(300, 170);

    auto* layout = new QVBoxLayout(this);
    enabledCheck_ = new QCheckBox(tr("像素值标注"), this);
    enabledCheck_->setObjectName(QStringLiteral("pixelValueOverlayCheck"));
    enabledCheck_->setChecked(true);
    layout->addWidget(enabledCheck_);

    meshCheck_ = new QCheckBox(tr("Bayer mesh"), this);
    meshCheck_->setObjectName(QStringLiteral("bayerMeshCheck"));
    meshCheck_->setChecked(false);
    bayerLabelCheck_ = new QCheckBox(tr("Bayer pattern"), this);
    bayerLabelCheck_->setObjectName(QStringLiteral("bayerPatternCheck"));
    bayerLabelCheck_->setChecked(false);
    layout->addWidget(meshCheck_);
    layout->addWidget(bayerLabelCheck_);
    layout->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QWidget::hide);
    layout->addWidget(buttons);

    for (auto* check : {enabledCheck_, meshCheck_, bayerLabelCheck_}) {
        connect(check, &QCheckBox::toggled,
                this, &PixelInfoDialog::publishOptions);
    }
}

PixelOverlayOptions PixelInfoDialog::options() const {
    PixelOverlayOptions result;
    result.enabled = enabledCheck_->isChecked();
    result.showMesh = meshCheck_->isChecked();
    result.showBayerLabel = bayerLabelCheck_->isChecked();
    return result;
}

void PixelInfoDialog::publishOptions() {
    emit optionsChanged(options());
}

} // namespace rawviewer::presentation
