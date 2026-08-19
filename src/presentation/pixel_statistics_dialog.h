#pragma once

#include "application/pixel_statistics.h"

#include <QDialog>

#include <array>
#include <memory>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QCloseEvent;
class QLabel;
class QProgressBar;
class QPushButton;
class QShowEvent;
class QStackedWidget;
class QTimer;

namespace rawviewer::presentation {

class StatisticsChartWidget;

class PixelStatisticsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PixelStatisticsDialog(QWidget* parent = nullptr);

    void setSource(const application::ImageMetadata* metadata);
    application::PixelStatisticsMode mode() const noexcept;
    bool channelsEnabled() const noexcept;
    std::uint32_t histogramBins() const noexcept;
    void showPreferences();
    void setSelection(const application::StatisticsSelection& selection);
    void setBusy(bool busy,
                 std::shared_ptr<std::atomic_uint32_t> progress = {});
    void setResult(const application::PixelStatisticsResult& result);
    void setResults(
        const std::vector<application::PixelStatisticsResult>& results);
    void clearResult(const QString& message = {});

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void reject() override;

signals:
    void modeChanged(application::PixelStatisticsMode mode);
    void optionsChanged();
    void cancelRequested();
    void toolClosed();

private:
    void selectMode(application::PixelStatisticsMode mode);
    void refreshProgress();
    void loadPreferences();
    void applyChartPreferences();
    void setMetricLabels(const application::StatisticsSummary* summary);

    QButtonGroup* modeGroup_ = nullptr;
    QCheckBox* channelsCheck_ = nullptr;
    QLabel* selectionLabel_ = nullptr;
    std::array<QLabel*, 5> metricLabels_{};
    QProgressBar* progressBar_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    StatisticsChartWidget* chart_ = nullptr;
    QStackedWidget* resultStack_ = nullptr;
    QWidget* singleResultPage_ = nullptr;
    QWidget* channelResultPage_ = nullptr;
    std::array<StatisticsChartWidget*, 4> channelCharts_{};
    std::array<QLabel*, 4> channelSummaryLabels_{};
    QTimer* progressTimer_ = nullptr;
    std::shared_ptr<std::atomic_uint32_t> progress_;
    application::PixelStatisticsMode mode_ =
        application::PixelStatisticsMode::Status;
    bool sourceSupported_ = false;
    bool channelSupported_ = false;
    std::uint32_t histogramBins_ = 256;
    int lineWidth_ = 1;
    bool showGrid_ = true;
    bool showPoints_ = false;
    bool fillHistogram_ = true;
};

} // namespace rawviewer::presentation
