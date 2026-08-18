#include "presentation/pixel_statistics_dialog.h"

#include "presentation/statistics_chart_widget.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
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

QString channelName(domain::BayerChannel channel) {
    return QString::fromLatin1(domain::toString(channel));
}

QString compactSummary(const application::StatisticsSummary& value) {
    return QObject::tr("N %1   μ %2   min %3   max %4   σ %5")
        .arg(QLocale().toString(static_cast<qulonglong>(value.count)))
        .arg(number(value.mean))
        .arg(number(value.minimum))
        .arg(number(value.maximum))
        .arg(number(value.standardDeviation));
}

} // namespace

PixelStatisticsDialog::PixelStatisticsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Pixel Statistics"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(740, 620);
    setMinimumSize(640, 500);
    setObjectName(QStringLiteral("pixelStatisticsDialog"));
    setStyleSheet(QStringLiteral(
        "#pixelStatisticsDialog QFrame[statsPane=\"true\"] {"
        " border: 1px solid palette(mid); background: palette(base); }"
        "#pixelStatisticsDialog QLabel[role=\"section\"] {"
        " color: palette(mid); font-size: 10px; font-weight: 700;"
        " letter-spacing: 1px; }"
        "#pixelStatisticsDialog QToolButton[modeButton=\"true\"] {"
        " border: 0; border-bottom: 2px solid transparent;"
        " padding: 4px 9px; min-height: 22px; font-weight: 600; }"
        "#pixelStatisticsDialog QToolButton:checked {"
        " border-bottom-color: palette(highlight);"
        " background: palette(alternate-base); color: palette(text); }"
        "#pixelStatisticsDialog QFrame[metricCell=\"true\"] {"
        " border-left: 1px solid palette(mid); }"
        "#pixelStatisticsDialog QFrame[channelCard=\"true\"] {"
        " border: 1px solid palette(mid); background: palette(base); }"
        "#pixelStatisticsDialog QLabel[role=\"channelTitle\"] {"
        " font-weight: 700; }"
        "#pixelStatisticsDialog QLabel[role=\"channelSummary\"] {"
        " color: palette(mid); font-size: 10px; }"
        "#pixelStatisticsDialog QLabel[role=\"metricCaption\"] {"
        " color: palette(mid); font-size: 10px; }"
        "#pixelStatisticsDialog QLabel[role=\"metricValue\"] {"
        " font-family: monospace; font-size: 14px; font-weight: 600; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* top = new QFrame(this);
    top->setObjectName(QStringLiteral("statisticsModePane"));
    top->setProperty("statsPane", true);
    auto* topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(8, 4, 6, 4);
    topLayout->setSpacing(2);
    auto* modeTitle = new QLabel(tr("MODE"), top);
    modeTitle->setProperty("role", "section");
    topLayout->addWidget(modeTitle);
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
        button->setProperty("modeButton", true);
        button->setProperty("mode", static_cast<int>(mode));
        modeGroup_->addButton(button, static_cast<int>(mode));
        topLayout->addWidget(button);
    }
    modeGroup_->button(static_cast<int>(mode_))->setChecked(true);
    topLayout->addStretch();
    auto* close = new QToolButton(top);
    close->setText(QStringLiteral("×"));
    close->setToolTip(tr("关闭"));
    close->setAutoRaise(true);
    close->setFixedSize(22, 22);
    topLayout->addWidget(close);
    root->addWidget(top, 1);

    auto* controls = new QFrame(this);
    controls->setObjectName(QStringLiteral("statisticsAnalysisPane"));
    controls->setProperty("statsPane", true);
    auto* controlLayout = new QGridLayout(controls);
    controlLayout->setContentsMargins(8, 6, 8, 6);
    controlLayout->setHorizontalSpacing(7);
    controlLayout->setVerticalSpacing(4);
    auto* analysisTitle = new QLabel(tr("ANALYSIS"), controls);
    analysisTitle->setProperty("role", "section");
    controlLayout->addWidget(analysisTitle, 0, 0, 1, 8);

    channelsCheck_ = new QCheckBox(tr("Bayer channels"), controls);
    channelsCheck_->setObjectName(QStringLiteral("statisticsChannelsCheck"));
    channelsCheck_->setToolTip(
        tr("Show separate R, Gr, Gb and B results"));
    controlLayout->addWidget(channelsCheck_, 1, 0, 1, 2);
    selectionLabel_ = new QLabel(tr("Selection —"), controls);
    selectionLabel_->setObjectName(QStringLiteral("statisticsSelectionLabel"));
    controlLayout->addWidget(selectionLabel_, 1, 2, 1, 6);

    auto* metrics = new QFrame(controls);
    metrics->setObjectName(QStringLiteral("statisticsMetrics"));
    auto* metricsLayout = new QHBoxLayout(metrics);
    metricsLayout->setContentsMargins(0, 0, 0, 0);
    metricsLayout->setSpacing(0);
    const std::array metricNames{tr("COUNT"), tr("MEAN"), tr("MIN"),
                                 tr("MAX"), tr("STD")};
    for (std::size_t index = 0; index < metricNames.size(); ++index) {
        auto* cell = new QFrame(metrics);
        cell->setProperty("metricCell", index != 0);
        auto* cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(8, 2, 8, 3);
        cellLayout->setSpacing(0);
        auto* caption = new QLabel(metricNames[index], cell);
        caption->setProperty("role", "metricCaption");
        metricLabels_[index] = new QLabel(QStringLiteral("—"), cell);
        metricLabels_[index]->setProperty("role", "metricValue");
        cellLayout->addWidget(caption);
        cellLayout->addWidget(metricLabels_[index]);
        metricsLayout->addWidget(cell, 1);
    }
    controlLayout->addWidget(metrics, 2, 0, 1, 8);

    summaryLabel_ = new QLabel(tr("Drag on the displayed RAW"), controls);
    summaryLabel_->setObjectName(QStringLiteral("statisticsStateLabel"));
    summaryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    controlLayout->addWidget(summaryLabel_, 3, 0, 1, 8);
    progressBar_ = new QProgressBar(controls);
    progressBar_->setRange(0, 1000);
    progressBar_->setValue(0);
    progressBar_->setFormat(tr("Ready"));
    cancelButton_ = new QPushButton(tr("Cancel"), controls);
    cancelButton_->setEnabled(false);
    controlLayout->addWidget(progressBar_, 4, 0, 1, 7);
    controlLayout->addWidget(cancelButton_, 4, 7);
    root->addWidget(controls, 2);

    auto* bottom = new QFrame(this);
    bottom->setObjectName(QStringLiteral("statisticsPlotPane"));
    bottom->setProperty("statsPane", true);
    auto* bottomLayout = new QVBoxLayout(bottom);
    bottomLayout->setContentsMargins(8, 6, 8, 8);
    bottomLayout->setSpacing(3);
    auto* plotTitle = new QLabel(tr("PLOT"), bottom);
    plotTitle->setProperty("role", "section");
    bottomLayout->addWidget(plotTitle);
    resultStack_ = new QStackedWidget(bottom);
    resultStack_->setObjectName(QStringLiteral("statisticsResultStack"));
    singleResultPage_ = new QWidget(resultStack_);
    auto* singleLayout = new QVBoxLayout(singleResultPage_);
    singleLayout->setContentsMargins(0, 0, 0, 0);
    chart_ = new StatisticsChartWidget(singleResultPage_);
    singleLayout->addWidget(chart_);
    resultStack_->addWidget(singleResultPage_);

    channelResultPage_ = new QWidget(resultStack_);
    channelResultPage_->setObjectName(QStringLiteral("statisticsChannelResults"));
    auto* channelGrid = new QGridLayout(channelResultPage_);
    channelGrid->setContentsMargins(0, 0, 0, 0);
    channelGrid->setSpacing(4);
    const std::array channels{domain::BayerChannel::R,
                              domain::BayerChannel::Gr,
                              domain::BayerChannel::Gb,
                              domain::BayerChannel::B};
    for (std::size_t index = 0; index < channels.size(); ++index) {
        auto* card = new QFrame(channelResultPage_);
        card->setObjectName(QStringLiteral("statisticsChannelCard%1")
                                .arg(channelName(channels[index])));
        card->setProperty("channelCard", true);
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(4, 3, 4, 4);
        cardLayout->setSpacing(1);
        auto* title = new QLabel(channelName(channels[index]), card);
        title->setProperty("role", "channelTitle");
        channelSummaryLabels_[index] = new QLabel(QStringLiteral("—"), card);
        channelSummaryLabels_[index]->setProperty("role", "channelSummary");
        channelCharts_[index] = new StatisticsChartWidget(card);
        cardLayout->addWidget(title);
        cardLayout->addWidget(channelSummaryLabels_[index]);
        cardLayout->addWidget(channelCharts_[index], 1);
        channelGrid->addWidget(card,
                               static_cast<int>(index / 2),
                               static_cast<int>(index % 2));
    }
    resultStack_->addWidget(channelResultPage_);
    bottomLayout->addWidget(resultStack_);
    root->addWidget(bottom, 5);

    loadPreferences();
    applyChartPreferences();
    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(80);
    connect(progressTimer_, &QTimer::timeout,
            this, &PixelStatisticsDialog::refreshProgress);
    connect(modeGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        selectMode(static_cast<application::PixelStatisticsMode>(id));
    });
    connect(channelsCheck_, &QCheckBox::toggled,
            this, &PixelStatisticsDialog::optionsChanged);
    connect(cancelButton_, &QPushButton::clicked,
            this, &PixelStatisticsDialog::cancelRequested);
    connect(close, &QToolButton::clicked, this, [this] {
        emit toolClosed();
        hide();
    });
}

void PixelStatisticsDialog::setSource(
    const application::ImageMetadata* metadata) {
    sourceSupported_ = metadata &&
        metadata->kind != application::ImageKind::Standard;
    channelSupported_ = sourceSupported_ &&
        metadata->bayerPattern != domain::BayerPattern::None;
    if (!channelSupported_) {
        const QSignalBlocker blocker(channelsCheck_);
        channelsCheck_->setChecked(false);
    }
    channelsCheck_->setEnabled(channelSupported_);
    for (auto* button : modeGroup_->buttons()) {
        button->setEnabled(sourceSupported_);
    }
    clearResult(sourceSupported_
        ? tr("Drag on the displayed RAW")
        : tr("RAW source required"));
    selectionLabel_->setText(tr("Selection —"));
}

application::PixelStatisticsMode PixelStatisticsDialog::mode() const noexcept {
    return mode_;
}

bool PixelStatisticsDialog::channelsEnabled() const noexcept {
    return channelSupported_ && channelsCheck_->isChecked();
}

std::uint32_t PixelStatisticsDialog::histogramBins() const noexcept {
    return histogramBins_;
}

void PixelStatisticsDialog::showPreferences() {
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("pixelStatisticsPreferencesDialog"));
    dialog.setWindowTitle(tr("Pixel Statistics Preferences"));
    dialog.setModal(true);
    dialog.setMinimumWidth(330);
    auto* layout = new QFormLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* lineWidth = new QSpinBox(&dialog);
    lineWidth->setObjectName(QStringLiteral("statisticsPreferenceLineWidth"));
    lineWidth->setRange(1, 5);
    lineWidth->setSuffix(tr(" px"));
    lineWidth->setValue(lineWidth_);
    auto* bins = new QSpinBox(&dialog);
    bins->setObjectName(QStringLiteral("statisticsPreferenceHistogramBins"));
    bins->setRange(16, 4096);
    bins->setSingleStep(16);
    bins->setValue(static_cast<int>(histogramBins_));
    auto* dataPoints = new QCheckBox(tr("Show data points"), &dialog);
    dataPoints->setObjectName(QStringLiteral("statisticsPreferenceDataPoints"));
    dataPoints->setChecked(showPoints_);
    auto* grid = new QCheckBox(tr("Show grid"), &dialog);
    grid->setObjectName(QStringLiteral("statisticsPreferenceGrid"));
    grid->setChecked(showGrid_);
    auto* fill = new QCheckBox(tr("Fill histogram"), &dialog);
    fill->setObjectName(QStringLiteral("statisticsPreferenceHistogramFill"));
    fill->setChecked(fillHistogram_);
    layout->addRow(tr("Line width"), lineWidth);
    layout->addRow(tr("Histogram bins"), bins);
    layout->addRow(QString(), dataPoints);
    layout->addRow(QString(), grid);
    layout->addRow(QString(), fill);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto oldBins = histogramBins_;
    lineWidth_ = lineWidth->value();
    histogramBins_ = static_cast<std::uint32_t>(bins->value());
    showPoints_ = dataPoints->isChecked();
    showGrid_ = grid->isChecked();
    fillHistogram_ = fill->isChecked();
    QSettings settings;
    settings.setValue(QStringLiteral("pixelStatistics/lineWidth"), lineWidth_);
    settings.setValue(QStringLiteral("pixelStatistics/histogramBins"),
                      histogramBins_);
    settings.setValue(QStringLiteral("pixelStatistics/showDataPoints"),
                      showPoints_);
    settings.setValue(QStringLiteral("pixelStatistics/showGrid"), showGrid_);
    settings.setValue(QStringLiteral("pixelStatistics/fillHistogram"),
                      fillHistogram_);
    applyChartPreferences();
    if (oldBins != histogramBins_) {
        emit optionsChanged();
    }
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
    channelsCheck_->setEnabled(channelSupported_ && !busy);
    if (busy) {
        progressBar_->setValue(0);
        progressBar_->setFormat(tr("Calculating %p%"));
        progressTimer_->start();
        summaryLabel_->setText(tr("Calculating displayed RAW…"));
    } else {
        progressTimer_->stop();
        progress_.reset();
    }
}

void PixelStatisticsDialog::setResult(
    const application::PixelStatisticsResult& result) {
    setResults({result});
}

void PixelStatisticsDialog::setResults(
    const std::vector<application::PixelStatisticsResult>& results) {
    if (results.empty()) {
        clearResult(tr("No results"));
        return;
    }
    setBusy(false);
    if (results.size() == channelCharts_.size()) {
        setMetricLabels(nullptr);
        summaryLabel_->setText(tr("Complete · 4 Bayer channels"));
        for (std::size_t index = 0; index < channelCharts_.size(); ++index) {
            if (results[index].succeeded()) {
                channelSummaryLabels_[index]->setText(
                    compactSummary(results[index].summary));
                channelCharts_[index]->setResult(results[index]);
            } else {
                const auto message = QString::fromStdString(
                    results[index].message);
                channelSummaryLabels_[index]->setText(tr("No samples"));
                channelCharts_[index]->clear(message);
            }
        }
        resultStack_->setCurrentWidget(channelResultPage_);
    } else {
        setMetricLabels(&results.front().summary);
        summaryLabel_->setText(tr("Complete"));
        chart_->setResult(results.front());
        resultStack_->setCurrentWidget(singleResultPage_);
    }
    progressBar_->setValue(1000);
    progressBar_->setFormat(tr("Complete"));
}

void PixelStatisticsDialog::clearResult(const QString& message) {
    setBusy(false);
    setMetricLabels(nullptr);
    summaryLabel_->setText(
        message.isEmpty() ? tr("Drag on the displayed RAW") : message);
    progressBar_->setValue(0);
    progressBar_->setFormat(tr("Ready"));
    chart_->clear(message);
    for (std::size_t index = 0; index < channelCharts_.size(); ++index) {
        channelSummaryLabels_[index]->setText(QStringLiteral("—"));
        channelCharts_[index]->clear(message);
    }
    resultStack_->setCurrentWidget(singleResultPage_);
}

void PixelStatisticsDialog::selectMode(
    application::PixelStatisticsMode mode) {
    mode_ = mode;
    clearResult(mode == application::PixelStatisticsMode::WhiteBalance
        ? tr("WB reserved")
        : tr("Drag on the displayed RAW"));
    emit modeChanged(mode_);
}

void PixelStatisticsDialog::refreshProgress() {
    if (!progress_) {
        return;
    }
    progressBar_->setValue(static_cast<int>(
        progress_->load(std::memory_order_relaxed)));
}

void PixelStatisticsDialog::loadPreferences() {
    const QSettings settings;
    lineWidth_ = std::clamp(
        settings.value(QStringLiteral("pixelStatistics/lineWidth"), 1).toInt(),
        1,
        5);
    histogramBins_ = static_cast<std::uint32_t>(std::clamp(
        settings.value(QStringLiteral("pixelStatistics/histogramBins"), 256)
            .toInt(),
        16,
        4096));
    showPoints_ = settings.value(
        QStringLiteral("pixelStatistics/showDataPoints"), false).toBool();
    showGrid_ = settings.value(
        QStringLiteral("pixelStatistics/showGrid"), true).toBool();
    fillHistogram_ = settings.value(
        QStringLiteral("pixelStatistics/fillHistogram"), true).toBool();
}

void PixelStatisticsDialog::applyChartPreferences() {
    const auto apply = [this](StatisticsChartWidget* chart) {
        chart->setLineWidth(lineWidth_);
        chart->setShowGrid(showGrid_);
        chart->setShowPoints(showPoints_);
        chart->setFillHistogram(fillHistogram_);
    };
    apply(chart_);
    for (auto* channelChart : channelCharts_) {
        apply(channelChart);
    }
}

void PixelStatisticsDialog::setMetricLabels(
    const application::StatisticsSummary* summary) {
    if (!summary) {
        for (auto* label : metricLabels_) {
            label->setText(QStringLiteral("—"));
        }
        return;
    }
    metricLabels_[0]->setText(
        QLocale().toString(static_cast<qulonglong>(summary->count)));
    metricLabels_[1]->setText(number(summary->mean));
    metricLabels_[2]->setText(number(summary->minimum));
    metricLabels_[3]->setText(number(summary->maximum));
    metricLabels_[4]->setText(number(summary->standardDeviation));
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
