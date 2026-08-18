#include "presentation/demosaic_dialog.h"

#include <QComboBox>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace rawviewer::presentation {
namespace {

QFrame* pane(const QString& objectName, QWidget* parent) {
    auto* result = new QFrame(parent);
    result->setObjectName(objectName);
    result->setFrameShape(QFrame::StyledPanel);
    return result;
}

} // namespace

DemosaicDialog::DemosaicDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Bayer Demosaic"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setSizeGripEnabled(false);
    setFixedWidth(410);
    setStyleSheet(QStringLiteral(R"(
        QDialog { background: #20242b; color: #e6e9ee; }
        QFrame#demosaicAlgorithmPane, QFrame#demosaicPipelinePane {
            background: #272c34; border: 1px solid #414854; border-radius: 2px;
        }
        QLabel#demosaicSectionTitle { font-weight: 650; color: #f2f4f7; }
        QLabel#demosaicAlgorithmDescription, QLabel#demosaicSourceLabel,
        QLabel#demosaicStatusLabel { color: #aeb6c3; }
        QComboBox { min-height: 28px; border: 1px solid #515967;
            border-radius: 2px; padding: 0 7px; background: #1d2127;
            color: #f2f4f7; }
        QComboBox QAbstractItemView { background: #242930; color: #f2f4f7;
            selection-background-color: #3367d6; }
        QPushButton { min-height: 28px; padding: 0 13px; border-radius: 2px;
            border: 1px solid #515967; background: #2c323b; color: #edf0f4; }
        QPushButton:hover { background: #373e49; }
        QPushButton#demosaicApplyButton { background: #3367d6;
            border-color: #4478e5; }
        QPushButton#demosaicApplyButton:hover { background: #3d73e4; }
        QPushButton:disabled { color: #777f8a; background: #292e35; }
    )"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 10);
    root->setSpacing(8);

    auto* algorithmPane = pane(QStringLiteral("demosaicAlgorithmPane"), this);
    auto* algorithmLayout = new QVBoxLayout(algorithmPane);
    algorithmLayout->setContentsMargins(11, 9, 11, 10);
    algorithmLayout->setSpacing(7);
    auto* algorithmTitle = new QLabel(tr("DEMOSAIC ALGORITHM"), algorithmPane);
    algorithmTitle->setObjectName(QStringLiteral("demosaicSectionTitle"));
    algorithmLayout->addWidget(algorithmTitle);
    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(14);
    algorithmCombo_ = new QComboBox(algorithmPane);
    algorithmCombo_->setObjectName(QStringLiteral("demosaicAlgorithmCombo"));
    algorithmCombo_->addItem(tr("Malvar-He-Cutler"),
        static_cast<int>(application::DemosaicAlgorithm::MalvarHeCutler));
    algorithmCombo_->addItem(tr("Hamilton-Adams"),
        static_cast<int>(application::DemosaicAlgorithm::HamiltonAdams));
    algorithmCombo_->addItem(tr("Bilinear"),
        static_cast<int>(application::DemosaicAlgorithm::Bilinear));
    form->addRow(tr("Method"), algorithmCombo_);
    algorithmLayout->addLayout(form);
    algorithmDescription_ = new QLabel(algorithmPane);
    algorithmDescription_->setObjectName(
        QStringLiteral("demosaicAlgorithmDescription"));
    algorithmDescription_->setWordWrap(true);
    algorithmLayout->addWidget(algorithmDescription_);
    root->addWidget(algorithmPane);

    auto* pipelinePane = pane(QStringLiteral("demosaicPipelinePane"), this);
    auto* pipelineLayout = new QVBoxLayout(pipelinePane);
    pipelineLayout->setContentsMargins(11, 9, 11, 10);
    pipelineLayout->setSpacing(5);
    auto* pipelineTitle = new QLabel(tr("CURRENT BAYER PIPELINE"), pipelinePane);
    pipelineTitle->setObjectName(QStringLiteral("demosaicSectionTitle"));
    sourceLabel_ = new QLabel(tr("No regular Bayer RAW source"), pipelinePane);
    sourceLabel_->setObjectName(QStringLiteral("demosaicSourceLabel"));
    sourceLabel_->setWordWrap(true);
    statusLabel_ = new QLabel(tr("Ready"), pipelinePane);
    statusLabel_->setObjectName(QStringLiteral("demosaicStatusLabel"));
    statusLabel_->setWordWrap(true);
    pipelineLayout->addWidget(pipelineTitle);
    pipelineLayout->addWidget(sourceLabel_);
    pipelineLayout->addWidget(statusLabel_);
    root->addWidget(pipelinePane);

    auto* note = new QLabel(
        tr("Output is display-mapped RGB. Preview is bounded to 1024 px; "
           "exact pixels remain lazy and tiled."), this);
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("demosaicPerformanceNote"));
    root->addWidget(note);

    auto* actions = new QHBoxLayout();
    restoreButton_ = new QPushButton(tr("Show Bayer Source"), this);
    restoreButton_->setObjectName(QStringLiteral("demosaicRestoreButton"));
    restoreButton_->setEnabled(false);
    actions->addWidget(restoreButton_);
    actions->addStretch();
    auto* closeButton = new QPushButton(tr("Close"), this);
    applyButton_ = new QPushButton(tr("Apply"), this);
    applyButton_->setObjectName(QStringLiteral("demosaicApplyButton"));
    applyButton_->setDefault(true);
    applyButton_->setEnabled(false);
    actions->addWidget(closeButton);
    actions->addWidget(applyButton_);
    root->addLayout(actions);

    connect(algorithmCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &DemosaicDialog::syncAlgorithmDescription);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::hide);
    connect(restoreButton_, &QPushButton::clicked,
            this, &DemosaicDialog::restoreSourceRequested);
    connect(applyButton_, &QPushButton::clicked,
            this, &DemosaicDialog::applyRequested);
    syncAlgorithmDescription();
    adjustSize();
}

void DemosaicDialog::setSource(
    const application::ImageMetadata* metadata) {
    sourceSupported_ = metadata &&
        metadata->kind != application::ImageKind::Standard &&
        metadata->bayerPattern != domain::BayerPattern::None;
    if (!metadata) {
        sourceLabel_->setText(tr("No regular Bayer RAW source"));
    } else {
        sourceLabel_->setText(tr("%1 × %2 · %3 · %4")
            .arg(metadata->width)
            .arg(metadata->height)
            .arg(QString::fromLatin1(
                domain::toString(metadata->bayerPattern)))
            .arg(QString::fromStdString(metadata->format)));
    }
    statusLabel_->setText(sourceSupported_
        ? tr("Ready · input is the currently displayed Bayer RAW")
        : tr("A regular RGGB, BGGR, GRBG, or GBRG source is required"));
    if (sourceSupported_) {
        restoreButton_->setEnabled(false);
    }
    syncActionState();
}

application::DemosaicRequest DemosaicDialog::request(
    std::shared_ptr<const application::DecodedImage> source,
    const domain::DisplayMapping& mapping,
    std::shared_ptr<std::atomic_bool> cancellation) const {
    application::DemosaicRequest result;
    result.source = std::move(source);
    result.algorithm = static_cast<application::DemosaicAlgorithm>(
        algorithmCombo_->currentData().toInt());
    result.displayMapping = mapping;
    result.cancellation = std::move(cancellation);
    return result;
}

void DemosaicDialog::setBusy(bool busy, const QString& message) {
    busy_ = busy;
    algorithmCombo_->setEnabled(!busy);
    statusLabel_->setText(message.isEmpty()
        ? (busy ? tr("Demosaicing…") : tr("Ready")) : message);
    syncActionState();
}

void DemosaicDialog::setResult(
    const application::ImageMetadata* metadata) {
    if (metadata) {
        statusLabel_->setText(tr("Applied · %1").arg(
            QString::fromStdString(metadata->format)));
        restoreButton_->setEnabled(true);
    }
}

void DemosaicDialog::syncAlgorithmDescription() {
    const auto algorithm = static_cast<application::DemosaicAlgorithm>(
        algorithmCombo_->currentData().toInt());
    switch (algorithm) {
    case application::DemosaicAlgorithm::Bilinear:
        algorithmDescription_->setText(
            tr("FAST · Radius 1 · Lowest CPU cost; useful as a baseline."));
        break;
    case application::DemosaicAlgorithm::MalvarHeCutler:
        algorithmDescription_->setText(
            tr("RECOMMENDED · 5×5 linear color correction · strong quality/cost balance."));
        break;
    case application::DemosaicAlgorithm::HamiltonAdams:
        algorithmDescription_->setText(
            tr("EDGE-AWARE · Directional gradients and color differences · best for hard edges."));
        break;
    }
}

void DemosaicDialog::syncActionState() {
    applyButton_->setEnabled(sourceSupported_ && !busy_);
}

} // namespace rawviewer::presentation
