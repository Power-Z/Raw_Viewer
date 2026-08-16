#pragma once

#include "application/pixel_statistics.h"

#include <QDialog>

#include <memory>

class QButtonGroup;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace rawviewer::presentation {

class StatisticsChartWidget;

class PixelStatisticsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PixelStatisticsDialog(QWidget* parent = nullptr);

    void setSource(const application::ImageMetadata* metadata);
    application::PixelStatisticsMode mode() const noexcept;
    domain::BayerChannel channel() const noexcept;
    std::uint32_t histogramBins() const noexcept;
    void setSelection(const application::StatisticsSelection& selection);
    void setBusy(bool busy,
                 std::shared_ptr<std::atomic_uint32_t> progress = {});
    void setResult(const application::PixelStatisticsResult& result);
    void clearResult(const QString& message = {});

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

signals:
    void modeChanged(application::PixelStatisticsMode mode);
    void optionsChanged();
    void cancelRequested();
    void toolClosed();

private:
    void selectMode(application::PixelStatisticsMode mode);
    void refreshProgress();
    void updateInstructions();

    QButtonGroup* modeGroup_ = nullptr;
    QComboBox* channelCombo_ = nullptr;
    QComboBox* binsCombo_ = nullptr;
    QCheckBox* gridCheck_ = nullptr;
    QCheckBox* pointsCheck_ = nullptr;
    QCheckBox* fillCheck_ = nullptr;
    QComboBox* lineWidthCombo_ = nullptr;
    QLabel* instructionLabel_ = nullptr;
    QLabel* selectionLabel_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    StatisticsChartWidget* chart_ = nullptr;
    QTimer* progressTimer_ = nullptr;
    std::shared_ptr<std::atomic_uint32_t> progress_;
    application::PixelStatisticsMode mode_ =
        application::PixelStatisticsMode::Status;
    bool sourceSupported_ = false;
};

} // namespace rawviewer::presentation
