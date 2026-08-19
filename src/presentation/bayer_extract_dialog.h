#pragma once

#include "application/bayer_extract.h"

#include <QDialog>

#include <vector>

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QScrollArea;
class QToolButton;
class QWidget;

namespace rawviewer::presentation {

class BayerExtractDialog final : public QDialog {
    Q_OBJECT

public:
    explicit BayerExtractDialog(QWidget* parent = nullptr);

    void setSource(const application::ImageMetadata* metadata);
    application::BayerExtractRequest request(
        std::shared_ptr<const application::DecodedImage> source,
        std::shared_ptr<std::atomic_bool> cancellation) const;
    void setBusy(bool busy, const QString& message = {});
    void setResult(const application::BayerExtraction* extraction);
    bool needsPartialEdgeConfirmation() const noexcept;

signals:
    void extractRequested();
    void showOriginalRequested();

private:
    struct PatternEntry {
        application::BayerMaskPattern pattern;
        bool preset = false;
    };

    void addPreset(std::uint32_t size);
    void loadPatterns();
    void persistCustomPatterns() const;
    void applyPattern(int index);
    void rebuildMatrix(bool preserveSelection);
    void adjustDialogSize();
    void updateSelectionSummary();
    void syncActionState();
    application::BayerMaskPattern editedPattern() const;
    application::BayerPackingOrder selectedOrder() const noexcept;
    void confirmAndExtract();
    void savePattern();
    void deletePattern();

    QComboBox* patternCombo_ = nullptr;
    QComboBox* orderCombo_ = nullptr;
    QWidget* customEditor_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QSpinBox* columnsSpin_ = nullptr;
    QSpinBox* rowsSpin_ = nullptr;
    QWidget* matrixWidget_ = nullptr;
    QScrollArea* matrixScroll_ = nullptr;
    QGridLayout* matrixLayout_ = nullptr;
    std::vector<QToolButton*> cells_;
    QPushButton* selectAllButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* invertButton_ = nullptr;
    QToolButton* packingHelpButton_ = nullptr;
    QLabel* resultLabel_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QPushButton* extractButton_ = nullptr;
    QPushButton* originalButton_ = nullptr;
    std::vector<PatternEntry> patterns_;
    std::uint64_t sourceWidth_ = 0;
    std::uint64_t sourceHeight_ = 0;
    bool sourceSupported_ = false;
    bool busy_ = false;
    bool applyingPattern_ = false;
};

} // namespace rawviewer::presentation
