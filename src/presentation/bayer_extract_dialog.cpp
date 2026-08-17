#include "presentation/bayer_extract_dialog.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace rawviewer::presentation {
namespace {

constexpr int maximumEditorDimension = 16;
constexpr auto settingsKey = "bayerExtract/customPatterns";

QFrame* card(QWidget* parent) {
    auto* result = new QFrame(parent);
    result->setObjectName(QStringLiteral("bayerCard"));
    return result;
}

int matrixCellExtent(int columns, int rows) noexcept {
    const int maximumDimension = std::max(columns, rows);
    if (maximumDimension <= 2) return 48;
    if (maximumDimension <= 4) return 36;
    if (maximumDimension <= 8) return 24;
    return 12;
}

} // namespace

BayerExtractDialog::BayerExtractDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Bayer Channel Extract"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(350, 330);
    setStyleSheet(QStringLiteral(R"(
        QDialog { background: #f4f6f9; color: #202631; }
        QFrame#bayerCard { background: white; border: 1px solid #dfe4ec;
                           border-radius: 8px; }
        QLabel#sectionTitle { font-size: 14px; font-weight: 650; color: #172033; }
        QLabel#axisLabel { color: #7a8496; font-size: 10px; }
        QLabel#resultStatus { color: #667085; }
        QToolButton#maskCell { border-radius: 0px; border: 1px solid #cfd5df;
                              background: #e3e7ed; }
        QToolButton#maskCell:hover { border: 1px solid #7891f5; background: #edf1f7; }
        QToolButton#maskCell:checked { border: 1px solid #4f6bed;
                                      background: #5b72e8; color: white; }
        QLineEdit, QComboBox, QSpinBox { min-height: 28px; max-height: 28px;
                                        border: 1px solid #cfd5df;
                                        border-radius: 5px; padding: 0 6px;
                                        background: white; color: #202631; }
        QComboBox QAbstractItemView { background: white; color: #202631;
                                      selection-background-color: #4f6bed;
                                      selection-color: white; outline: 0; }
        QPushButton { min-height: 30px; max-height: 30px;
                      border-radius: 5px; padding: 0 10px;
                      border: 1px solid #c9d0dc; background: #ffffff; }
        QToolButton#packingHelpButton { border: none; padding: 0;
                                        font-size: 13px; font-weight: 700;
                                        color: #4f6bed; background: transparent; }
        QToolButton#packingHelpButton:hover { border-radius: 3px;
                                              background: #edf1ff; }
        QPushButton:hover { background: #f1f4fa; }
        QPushButton#primaryAction { color: white; background: #4f6bed; border-color: #4f6bed; }
        QPushButton#primaryAction:hover { background: #4059d6; }
    )"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 8);
    root->setSpacing(6);

    auto* patternCard = card(this);
    patternCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* patternLayout = new QVBoxLayout(patternCard);
    patternLayout->setContentsMargins(9, 6, 9, 7);
    patternLayout->setSpacing(4);
    auto* title = new QLabel(tr("Pattern"), patternCard);
    title->setObjectName(QStringLiteral("sectionTitle"));
    title->ensurePolished();
    title->setFixedHeight((title->fontMetrics().height() * 3 + 1) / 2);
    patternLayout->addWidget(title);

    auto* selectorRow = new QHBoxLayout();
    patternCombo_ = new QComboBox(patternCard);
    patternCombo_->setObjectName(QStringLiteral("patternCombo"));
    orderCombo_ = new QComboBox(patternCard);
    orderCombo_->setObjectName(QStringLiteral("packingOrderCombo"));
    orderCombo_->addItem(tr("Row-major"));
    orderCombo_->addItem(tr("Column-major"));
    patternCombo_->setFixedWidth(88);
    orderCombo_->setFixedWidth(126);
    packingHelpButton_ = new QToolButton(patternCard);
    packingHelpButton_->setObjectName(QStringLiteral("packingHelpButton"));
    packingHelpButton_->setText(QStringLiteral("?"));
    packingHelpButton_->setAutoRaise(true);
    packingHelpButton_->setFixedSize(18, 18);
    packingHelpButton_->setToolTip(tr("Explain packing order"));
    selectorRow->addWidget(patternCombo_);
    selectorRow->addWidget(orderCombo_);
    selectorRow->addWidget(packingHelpButton_);
    selectorRow->addStretch();
    patternLayout->addLayout(selectorRow);

    customEditor_ = new QWidget(patternCard);
    customEditor_->setObjectName(QStringLiteral("customPatternEditor"));
    auto* editorRow = new QHBoxLayout(customEditor_);
    editorRow->setContentsMargins(0, 0, 0, 0);
    editorRow->setSpacing(4);
    nameEdit_ = new QLineEdit(customEditor_);
    nameEdit_->setObjectName(QStringLiteral("patternNameEdit"));
    nameEdit_->setPlaceholderText(tr("Pattern name"));
    columnsSpin_ = new QSpinBox(customEditor_);
    rowsSpin_ = new QSpinBox(customEditor_);
    columnsSpin_->setObjectName(QStringLiteral("patternColumnsSpin"));
    rowsSpin_->setObjectName(QStringLiteral("patternRowsSpin"));
    columnsSpin_->setRange(1, maximumEditorDimension);
    rowsSpin_->setRange(1, maximumEditorDimension);
    columnsSpin_->setPrefix(tr("W "));
    rowsSpin_->setPrefix(tr("H "));
    columnsSpin_->setFixedWidth(58);
    rowsSpin_->setFixedWidth(58);
    saveButton_ = new QPushButton(tr("Save"), customEditor_);
    deleteButton_ = new QPushButton(tr("Delete"), customEditor_);
    editorRow->addWidget(nameEdit_, 1);
    editorRow->addWidget(columnsSpin_);
    editorRow->addWidget(rowsSpin_);
    editorRow->addWidget(saveButton_);
    editorRow->addWidget(deleteButton_);
    patternLayout->addWidget(customEditor_);
    root->addWidget(patternCard);

    auto* matrixCard = card(this);
    auto* matrixCardLayout = new QVBoxLayout(matrixCard);
    matrixCardLayout->setContentsMargins(8, 7, 8, 8);
    matrixCardLayout->setSpacing(4);
    auto* matrixToolbar = new QHBoxLayout();
    matrixToolbar->setSpacing(4);
    selectAllButton_ = new QPushButton(tr("Select all"), matrixCard);
    clearButton_ = new QPushButton(tr("Clear"), matrixCard);
    invertButton_ = new QPushButton(tr("Invert"), matrixCard);
    matrixToolbar->addWidget(selectAllButton_);
    matrixToolbar->addWidget(clearButton_);
    matrixToolbar->addWidget(invertButton_);
    matrixToolbar->addStretch();
    matrixCardLayout->addLayout(matrixToolbar);

    matrixScroll_ = new QScrollArea(matrixCard);
    matrixScroll_->setObjectName(QStringLiteral("patternMatrixScroll"));
    matrixScroll_->setWidgetResizable(true);
    matrixScroll_->setFrameShape(QFrame::NoFrame);
    matrixScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    matrixScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    matrixWidget_ = new QWidget(matrixScroll_);
    matrixWidget_->setObjectName(QStringLiteral("patternMatrix"));
    matrixLayout_ = new QGridLayout(matrixWidget_);
    matrixLayout_->setContentsMargins(3, 3, 3, 3);
    matrixLayout_->setSpacing(3);
    matrixLayout_->setAlignment(Qt::AlignCenter);
    matrixScroll_->setWidget(matrixWidget_);
    matrixCardLayout->addWidget(matrixScroll_, 1);
    root->addWidget(matrixCard, 1);

    resultLabel_ = new QLabel(tr("Ready · always extracts from the original RAW"), this);
    resultLabel_->setObjectName(QStringLiteral("resultStatus"));
    resultLabel_->setWordWrap(true);
    root->addWidget(resultLabel_);

    auto* actions = new QHBoxLayout();
    extractButton_ = new QPushButton(tr("Extract"), this);
    extractButton_->setObjectName(QStringLiteral("primaryAction"));
    originalButton_ = new QPushButton(tr("Show original"), this);
    actions->addWidget(extractButton_);
    actions->addWidget(originalButton_);
    actions->addStretch();
    auto* closeButton = new QPushButton(tr("Close"), this);
    actions->addWidget(closeButton);
    root->addLayout(actions);

    addPreset(2);
    addPreset(4);
    addPreset(8);
    loadPatterns();
    for (const auto& entry : patterns_) {
        patternCombo_->addItem(QString::fromStdString(entry.pattern.name));
    }
    patternCombo_->addItem(tr("Custom"));
    applyPattern(0);

    connect(patternCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &BayerExtractDialog::applyPattern);
    connect(columnsSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this] { if (!applyingPattern_) rebuildMatrix(true); });
    connect(rowsSpin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this] { if (!applyingPattern_) rebuildMatrix(true); });
    connect(saveButton_, &QPushButton::clicked,
            this, &BayerExtractDialog::savePattern);
    connect(deleteButton_, &QPushButton::clicked,
            this, &BayerExtractDialog::deletePattern);
    connect(selectAllButton_, &QPushButton::clicked, this, [this] {
        for (auto* cell : cells_) cell->setChecked(true);
        updateSelectionSummary();
    });
    connect(clearButton_, &QPushButton::clicked, this, [this] {
        for (auto* cell : cells_) cell->setChecked(false);
        updateSelectionSummary();
    });
    connect(invertButton_, &QPushButton::clicked, this, [this] {
        for (auto* cell : cells_) cell->setChecked(!cell->isChecked());
        updateSelectionSummary();
    });
    connect(packingHelpButton_, &QToolButton::clicked, this, [this] {
        QMessageBox::information(this, tr("Packing order"),
            tr("Row-major packs selected pattern positions from left to right, "
               "then top to bottom.\n\n"
               "Column-major packs them from top to bottom, then left to right."));
    });
    connect(extractButton_, &QPushButton::clicked,
            this, &BayerExtractDialog::confirmAndExtract);
    connect(originalButton_, &QPushButton::clicked,
            this, &BayerExtractDialog::showOriginalRequested);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::hide);
    syncActionState();
    adjustDialogSize();
}

void BayerExtractDialog::addPreset(std::uint32_t size) {
    application::BayerMaskPattern pattern;
    pattern.name = std::to_string(size) + "x" + std::to_string(size);
    pattern.columns = size;
    pattern.rows = size;
    pattern.selected.assign(static_cast<std::size_t>(size) * size, 0);
    pattern.selected.front() = 1;
    patterns_.push_back({std::move(pattern), true});
}

void BayerExtractDialog::loadPatterns() {
    const auto bytes = QSettings().value(settingsKey).toByteArray();
    const auto document = QJsonDocument::fromJson(bytes);
    for (const auto value : document.array()) {
        const auto object = value.toObject();
        application::BayerMaskPattern pattern;
        pattern.name = object.value("name").toString().toStdString();
        pattern.columns = static_cast<std::uint32_t>(object.value("columns").toInt());
        pattern.rows = static_cast<std::uint32_t>(object.value("rows").toInt());
        pattern.selected.clear();
        for (const auto selected : object.value("selected").toArray()) {
            pattern.selected.push_back(selected.toBool() ? 1 : 0);
        }
        if (pattern.isValid() && pattern.columns <= maximumEditorDimension &&
            pattern.rows <= maximumEditorDimension && !pattern.name.empty()) {
            patterns_.push_back({std::move(pattern), false});
        }
    }
}

void BayerExtractDialog::persistCustomPatterns() const {
    QJsonArray patterns;
    for (const auto& entry : patterns_) {
        if (entry.preset) continue;
        QJsonObject object;
        object.insert("name", QString::fromStdString(entry.pattern.name));
        object.insert("columns", static_cast<int>(entry.pattern.columns));
        object.insert("rows", static_cast<int>(entry.pattern.rows));
        QJsonArray selected;
        for (const auto value : entry.pattern.selected) selected.append(value != 0);
        object.insert("selected", selected);
        patterns.append(object);
    }
    QSettings().setValue(settingsKey, QJsonDocument(patterns).toJson(QJsonDocument::Compact));
}

void BayerExtractDialog::applyPattern(int index) {
    if (index == static_cast<int>(patterns_.size())) {
        applyingPattern_ = true;
        nameEdit_->clear();
        applyingPattern_ = false;
        customEditor_->show();
        deleteButton_->setEnabled(false);
        adjustDialogSize();
        syncActionState();
        return;
    }
    if (index < 0 || index >= static_cast<int>(patterns_.size())) return;
    applyingPattern_ = true;
    const auto& entry = patterns_[static_cast<std::size_t>(index)];
    customEditor_->setVisible(!entry.preset);
    nameEdit_->setText(QString::fromStdString(entry.pattern.name));
    columnsSpin_->setValue(static_cast<int>(entry.pattern.columns));
    rowsSpin_->setValue(static_cast<int>(entry.pattern.rows));
    applyingPattern_ = false;
    rebuildMatrix(false);
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        cells_[i]->setChecked(entry.pattern.selected[i] != 0);
    }
    deleteButton_->setEnabled(!entry.preset && !busy_);
    updateSelectionSummary();
}

void BayerExtractDialog::rebuildMatrix(bool preserveSelection) {
    std::vector<std::uint8_t> previous;
    const auto oldColumns = matrixWidget_->property("columns").toInt();
    if (preserveSelection) {
        previous.reserve(cells_.size());
        for (const auto* cell : cells_) previous.push_back(cell->isChecked() ? 1 : 0);
    }
    while (auto* item = matrixLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    cells_.clear();
    const int columns = columnsSpin_->value();
    const int rows = rowsSpin_->value();
    const int cellExtent = matrixCellExtent(columns, rows);
    matrixWidget_->setProperty("columns", columns);
    cells_.reserve(static_cast<std::size_t>(columns) * rows);
    for (int x = 0; x < columns; ++x) {
        auto* label = new QLabel(QString::number(x), matrixWidget_);
        label->setObjectName(QStringLiteral("axisLabel"));
        label->setAlignment(Qt::AlignCenter);
        matrixLayout_->addWidget(label, 0, x + 1);
    }
    for (int y = 0; y < rows; ++y) {
        auto* label = new QLabel(QString::number(y), matrixWidget_);
        label->setObjectName(QStringLiteral("axisLabel"));
        label->setAlignment(Qt::AlignCenter);
        matrixLayout_->addWidget(label, y + 1, 0);
        for (int x = 0; x < columns; ++x) {
            auto* cell = new QToolButton(matrixWidget_);
            cell->setObjectName(QStringLiteral("maskCell"));
            cell->setFixedSize(cellExtent, cellExtent);
            cell->setCheckable(true);
            if (preserveSelection && x < oldColumns) {
                const auto oldIndex = static_cast<std::size_t>(y * oldColumns + x);
                if (oldIndex < previous.size()) cell->setChecked(previous[oldIndex] != 0);
            }
            cell->setProperty("cellX", x);
            cell->setProperty("cellY", y);
            cell->setText({});
            cell->setToolTip(
                tr("Source offset (%1, %2) · click to retain").arg(x).arg(y));
            connect(cell, &QToolButton::toggled,
                    this, &BayerExtractDialog::updateSelectionSummary);
            matrixLayout_->addWidget(cell, y + 1, x + 1);
            cells_.push_back(cell);
        }
    }
    updateSelectionSummary();
    adjustDialogSize();
}

void BayerExtractDialog::adjustDialogSize() {
    const int columns = columnsSpin_->value();
    const int rows = rowsSpin_->value();
    const int cellExtent = matrixCellExtent(columns, rows);
    const int matrixHeight = std::clamp(
        rows * cellExtent + std::max(0, rows - 1) * 3 + 54, 150, 450);
    const int matrixWidth = std::clamp(
        columns * cellExtent + std::max(0, columns - 1) * 3 + 38, 100, 470);
    matrixScroll_->setFixedHeight(matrixHeight);
    const int dialogWidth = std::clamp(matrixWidth + 38, 340, 520);
    const int fixedContentHeight = customEditor_->isVisible() ? 238 : 205;
    const int dialogHeight = std::clamp(matrixHeight + fixedContentHeight, 355, 680);
    // A fixed content-derived size is deliberate: resize() alone only grows a
    // visible dialog on some Windows styles because the prior layout hint is
    // retained. Resetting both bounds guarantees 8x8 -> 2x2 also shrinks.
    setFixedSize(dialogWidth, dialogHeight);
}

void BayerExtractDialog::updateSelectionSummary() {
    syncActionState();
}

void BayerExtractDialog::setSource(const application::ImageMetadata* metadata) {
    sourceSupported_ = metadata &&
        metadata->kind != application::ImageKind::Standard;
    sourceWidth_ = metadata ? metadata->width : 0;
    sourceHeight_ = metadata ? metadata->height : 0;
    if (!sourceSupported_) {
        syncActionState();
        return;
    }
    resultLabel_->setText(tr("Ready · always extracts from the original RAW"));
    syncActionState();
}

application::BayerMaskPattern BayerExtractDialog::editedPattern() const {
    application::BayerMaskPattern pattern;
    pattern.name = nameEdit_->text().trimmed().toStdString();
    pattern.columns = static_cast<std::uint32_t>(columnsSpin_->value());
    pattern.rows = static_cast<std::uint32_t>(rowsSpin_->value());
    pattern.selected.clear();
    pattern.selected.reserve(cells_.size());
    for (const auto* cell : cells_) pattern.selected.push_back(cell->isChecked() ? 1 : 0);
    return pattern;
}

application::BayerPackingOrder BayerExtractDialog::selectedOrder() const noexcept {
    return orderCombo_->currentIndex() == 0
        ? application::BayerPackingOrder::RowMajor
        : application::BayerPackingOrder::ColumnMajor;
}

application::BayerExtractRequest BayerExtractDialog::request(
    std::shared_ptr<const application::DecodedImage> source,
    std::shared_ptr<std::atomic_bool> cancellation) const {
    application::BayerExtractRequest result;
    result.source = std::move(source);
    result.mask = editedPattern();
    result.packingOrder = selectedOrder();
    result.cancellation = std::move(cancellation);
    return result;
}

bool BayerExtractDialog::needsPartialEdgeConfirmation() const noexcept {
    const auto pattern = editedPattern();
    return pattern.columns != 0 && pattern.rows != 0 &&
        (sourceWidth_ % pattern.columns != 0 ||
         sourceHeight_ % pattern.rows != 0);
}

void BayerExtractDialog::confirmAndExtract() {
    const auto pattern = editedPattern();
    if (!pattern.isValid()) {
        QMessageBox::warning(this, tr("Bayer Extract"),
            tr("Select at least one cell in the pattern."));
        return;
    }
    if (needsPartialEdgeConfirmation()) {
        const auto answer = QMessageBox::question(this, tr("Partial edge units"),
            tr("The %1 × %2 source region is not an integer multiple of the %3 × %4 pattern.\n\n"
               "Continue and retain selected pixels that are covered by the partial right/bottom units?")
                .arg(sourceWidth_).arg(sourceHeight_)
                .arg(pattern.columns).arg(pattern.rows),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) return;
    }
    emit extractRequested();
}

void BayerExtractDialog::savePattern() {
    auto pattern = editedPattern();
    if (pattern.name.empty() || !pattern.isValid()) {
        QMessageBox::warning(this, tr("Save pattern"),
            tr("Enter a name and select at least one cell."));
        return;
    }
    const auto sameName = std::find_if(patterns_.begin(), patterns_.end(),
        [&pattern](const PatternEntry& entry) { return entry.pattern.name == pattern.name; });
    int index = -1;
    if (sameName != patterns_.end()) {
        if (sameName->preset) {
            QMessageBox::warning(this, tr("Save pattern"),
                tr("Preset names are protected. Choose another name."));
            return;
        }
        sameName->pattern = pattern;
        index = static_cast<int>(std::distance(patterns_.begin(), sameName));
        patternCombo_->setItemText(index, QString::fromStdString(pattern.name));
    } else {
        patterns_.push_back({std::move(pattern), false});
        index = static_cast<int>(patterns_.size() - 1);
        patternCombo_->insertItem(index,
            QString::fromStdString(patterns_.back().pattern.name));
    }
    persistCustomPatterns();
    patternCombo_->setCurrentIndex(index);
    deleteButton_->setEnabled(!busy_);
}

void BayerExtractDialog::deletePattern() {
    const int index = patternCombo_->currentIndex();
    if (index < 0 || index >= static_cast<int>(patterns_.size()) ||
        patterns_[static_cast<std::size_t>(index)].preset) return;
    patterns_.erase(patterns_.begin() + index);
    patternCombo_->removeItem(index);
    persistCustomPatterns();
    patternCombo_->setCurrentIndex(0);
}

void BayerExtractDialog::setBusy(bool busy, const QString& message) {
    busy_ = busy;
    const std::array<QWidget*, 10> controls{
        patternCombo_, orderCombo_, nameEdit_, columnsSpin_, rowsSpin_,
        saveButton_, selectAllButton_, clearButton_, invertButton_,
        packingHelpButton_};
    for (auto* widget : controls) {
        widget->setEnabled(!busy);
    }
    for (auto* cell : cells_) cell->setEnabled(!busy);
    syncActionState();
    if (!message.isEmpty()) resultLabel_->setText(message);
}

void BayerExtractDialog::setResult(const application::BayerExtraction* extraction) {
    if (!extraction || !extraction->image) {
        resultLabel_->setText(tr("Ready · always extracts from the original RAW"));
        return;
    }
    const auto& geometry = extraction->geometry;
    resultLabel_->setText(tr("Result  %1 × %2%3")
        .arg(geometry.width).arg(geometry.height)
        .arg(geometry.hasPartialEdgeUnits() ? tr(" · partial edge") : QString()));
}

void BayerExtractDialog::syncActionState() {
    const bool hasSelection = std::any_of(cells_.begin(), cells_.end(),
        [](const QToolButton* cell) { return cell->isChecked(); });
    extractButton_->setEnabled(sourceSupported_ && hasSelection && !busy_);
    originalButton_->setEnabled(sourceSupported_ && !busy_);
    saveButton_->setEnabled(hasSelection && !busy_);
    const int index = patternCombo_->currentIndex();
    deleteButton_->setEnabled(!busy_ && index >= 0 &&
        index < static_cast<int>(patterns_.size()) &&
        !patterns_[static_cast<std::size_t>(index)].preset);
}

} // namespace rawviewer::presentation
