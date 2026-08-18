#include "presentation/filter_dialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
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

FilterDialog::FilterDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Image Filter"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setSizeGripEnabled(false);
    setFixedWidth(390);
    setStyleSheet(QStringLiteral(R"(
        QDialog { background: #20242b; color: #e6e9ee; }
        QFrame#filterParametersPane, QFrame#filterExecutionPane {
            background: #272c34; border: 1px solid #414854; border-radius: 2px;
        }
        QLabel#filterSectionTitle { font-weight: 650; color: #f2f4f7; }
        QLabel#filterSourceLabel, QLabel#filterStatusLabel { color: #aeb6c3; }
        QComboBox, QDoubleSpinBox {
            min-height: 27px; border: 1px solid #515967; border-radius: 2px;
            padding: 0 7px; background: #1d2127; color: #f2f4f7;
        }
        QComboBox QAbstractItemView { background: #242930; color: #f2f4f7;
            selection-background-color: #3367d6; }
        QPushButton { min-height: 28px; padding: 0 13px; border-radius: 2px;
            border: 1px solid #515967; background: #2c323b; color: #edf0f4; }
        QPushButton:hover { background: #373e49; }
        QPushButton#filterApplyButton { background: #3367d6; border-color: #4478e5; }
        QPushButton#filterApplyButton:hover { background: #3d73e4; }
        QPushButton:disabled { color: #777f8a; background: #292e35; }
    )"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 10);
    root->setSpacing(8);

    auto* parameterPane = pane(QStringLiteral("filterParametersPane"), this);
    auto* parameterLayout = new QVBoxLayout(parameterPane);
    parameterLayout->setContentsMargins(11, 9, 11, 10);
    parameterLayout->setSpacing(7);
    auto* parameterTitle = new QLabel(tr("FILTER PARAMETERS"), parameterPane);
    parameterTitle->setObjectName(QStringLiteral("filterSectionTitle"));
    parameterLayout->addWidget(parameterTitle);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(7);
    typeCombo_ = new QComboBox(parameterPane);
    typeCombo_->setObjectName(QStringLiteral("filterTypeCombo"));
    typeCombo_->addItem(tr("Gaussian"),
        static_cast<int>(application::FilterType::Gaussian));
    typeCombo_->addItem(tr("Median"),
        static_cast<int>(application::FilterType::Median));
    typeCombo_->addItem(tr("Mean"),
        static_cast<int>(application::FilterType::Mean));
    kernelCombo_ = new QComboBox(parameterPane);
    kernelCombo_->setObjectName(QStringLiteral("filterKernelCombo"));
    for (const int size : {3, 5, 7}) {
        kernelCombo_->addItem(tr("%1 × %1").arg(size), size);
    }
    sigmaSpin_ = new QDoubleSpinBox(parameterPane);
    sigmaSpin_->setObjectName(QStringLiteral("filterSigmaSpin"));
    sigmaSpin_->setRange(0.10, 10.0);
    sigmaSpin_->setDecimals(2);
    sigmaSpin_->setSingleStep(0.10);
    sigmaSpin_->setValue(1.0);
    sigmaSpin_->setToolTip(tr("Standard deviation for Gaussian weights"));
    form->addRow(tr("Type"), typeCombo_);
    form->addRow(tr("Kernel"), kernelCombo_);
    form->addRow(tr("Sigma"), sigmaSpin_);
    parameterLayout->addLayout(form);
    root->addWidget(parameterPane);

    auto* executionPane = pane(QStringLiteral("filterExecutionPane"), this);
    auto* executionLayout = new QVBoxLayout(executionPane);
    executionLayout->setContentsMargins(11, 9, 11, 10);
    executionLayout->setSpacing(5);
    auto* executionTitle = new QLabel(tr("CURRENT DISPLAY PIPELINE"), executionPane);
    executionTitle->setObjectName(QStringLiteral("filterSectionTitle"));
    sourceLabel_ = new QLabel(tr("No RAW source"), executionPane);
    sourceLabel_->setObjectName(QStringLiteral("filterSourceLabel"));
    sourceLabel_->setWordWrap(true);
    statusLabel_ = new QLabel(tr("Ready"), executionPane);
    statusLabel_->setObjectName(QStringLiteral("filterStatusLabel"));
    statusLabel_->setWordWrap(true);
    executionLayout->addWidget(executionTitle);
    executionLayout->addWidget(sourceLabel_);
    executionLayout->addWidget(statusLabel_);
    root->addWidget(executionPane);

    auto* actions = new QHBoxLayout();
    actions->addStretch();
    auto* closeButton = new QPushButton(tr("Close"), this);
    applyButton_ = new QPushButton(tr("Apply"), this);
    applyButton_->setObjectName(QStringLiteral("filterApplyButton"));
    applyButton_->setDefault(true);
    applyButton_->setEnabled(false);
    actions->addWidget(closeButton);
    actions->addWidget(applyButton_);
    root->addLayout(actions);

    connect(typeCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &FilterDialog::syncParameterState);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::hide);
    connect(applyButton_, &QPushButton::clicked,
            this, &FilterDialog::applyRequested);
    syncParameterState();
    adjustSize();
}

void FilterDialog::setSource(const application::ImageMetadata* metadata) {
    sourceSupported_ = metadata &&
        metadata->kind != application::ImageKind::Standard;
    if (!metadata) {
        sourceLabel_->setText(tr("No RAW source"));
    } else {
        sourceLabel_->setText(tr("%1 × %2 · %3 · %4")
            .arg(metadata->width)
            .arg(metadata->height)
            .arg(QString::fromStdString(metadata->format))
            .arg(QString::fromLatin1(domain::toString(metadata->scalarType))));
    }
    statusLabel_->setText(sourceSupported_
        ? tr("Ready · output is chained from the current displayed RAW")
        : tr("Filtering requires a scalar RAW display source"));
    syncParameterState();
}

application::FilterRequest FilterDialog::request(
    std::shared_ptr<const application::DecodedImage> source,
    std::shared_ptr<std::atomic_bool> cancellation) const {
    application::FilterRequest result;
    result.source = std::move(source);
    result.cancellation = std::move(cancellation);
    result.parameters.type = static_cast<application::FilterType>(
        typeCombo_->currentData().toInt());
    result.parameters.kernelSize = static_cast<std::uint32_t>(
        kernelCombo_->currentData().toInt());
    result.parameters.sigma = sigmaSpin_->value();
    return result;
}

void FilterDialog::setBusy(bool busy, const QString& message) {
    busy_ = busy;
    typeCombo_->setEnabled(!busy);
    kernelCombo_->setEnabled(!busy);
    statusLabel_->setText(message.isEmpty()
        ? (busy ? tr("Filtering…") : tr("Ready"))
        : message);
    syncParameterState();
}

void FilterDialog::setResult(const application::ImageMetadata* metadata) {
    if (!metadata) {
        return;
    }
    statusLabel_->setText(tr("Applied · %1").arg(
        QString::fromStdString(metadata->format)));
}

void FilterDialog::syncParameterState() {
    const auto type = static_cast<application::FilterType>(
        typeCombo_->currentData().toInt());
    sigmaSpin_->setEnabled(!busy_ &&
        type == application::FilterType::Gaussian);
    applyButton_->setEnabled(!busy_ && sourceSupported_);
}

} // namespace rawviewer::presentation
