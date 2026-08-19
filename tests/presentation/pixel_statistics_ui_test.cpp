#include "presentation/bayer_extract_dialog.h"
#include "presentation/demosaic_dialog.h"
#include "presentation/filter_dialog.h"
#include "presentation/histogram_widget.h"
#include "presentation/image_viewport.h"
#include "presentation/main_window.h"
#include "presentation/pixel_info_dialog.h"
#include "presentation/pixel_statistics_dialog.h"
#include "presentation/statistics_chart_widget.h"

#include "application/preview_renderer.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QSignalSpy>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QTest>
#include <QTemporaryFile>
#include <QToolButton>
#include <QTimer>
#include <QWheelEvent>

#include <array>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace {

class UiPixelSource final : public rawviewer::application::IPixelSource {
public:
    std::uint64_t width() const noexcept override { return 10; }
    std::uint64_t height() const noexcept override { return 10; }
    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (x >= width() || y >= height()) return {};
        return {true, static_cast<double>(y * width() + x)};
    }
};

class ContrastPixelSource final : public rawviewer::application::IPixelSource {
public:
    std::uint64_t width() const noexcept override { return 2; }
    std::uint64_t height() const noexcept override { return 1; }
    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (x >= width() || y >= height()) return {};
        ++sampleCalls;
        return {true, x == 0 ? 0.0 : 65535.0};
    }

    mutable std::uint64_t sampleCalls = 0;
};

class CountingGridPixelSource final
    : public rawviewer::application::IPixelSource {
public:
    CountingGridPixelSource(std::uint64_t width, std::uint64_t height)
        : width_(width), height_(height) {}

    std::uint64_t width() const noexcept override { return width_; }
    std::uint64_t height() const noexcept override { return height_; }
    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (x >= width_ || y >= height_) return {};
        ++sampleCalls;
        return {true, static_cast<double>((y * width_ + x) & 0xffffU)};
    }

    mutable std::uint64_t sampleCalls = 0;

private:
    std::uint64_t width_ = 0;
    std::uint64_t height_ = 0;
};

class RgbPixelSource final : public rawviewer::application::IPixelSource {
public:
    std::uint64_t width() const noexcept override { return 1; }
    std::uint64_t height() const noexcept override { return 1; }
    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (x != 0 || y != 0) return {};
        return {true, 0.0, true, 12, 34, 56};
    }
};

class MemoryRecentDocumentStore final
    : public rawviewer::application::IRecentDocumentStore {
public:
    std::vector<rawviewer::application::RecentDocument> load() const override {
        return documents;
    }
    void remember(const rawviewer::application::RecentDocument& document) override {
        documents.insert(documents.begin(), document);
    }
    void clear() override {
        documents.clear();
        cleared = true;
    }

    std::vector<rawviewer::application::RecentDocument> documents;
    bool cleared = false;
};

std::shared_ptr<rawviewer::application::DecodedImage> makeUiImage();

class StaticRawDecoder final : public rawviewer::application::IImageDecoder {
public:
    explicit StaticRawDecoder(
        std::shared_ptr<rawviewer::application::DecodedImage> image = {})
        : image_(std::move(image)) {}

    rawviewer::application::ProbeStrength probe(
        const std::filesystem::path&,
        std::span<const std::byte>,
        bool) const override {
        return rawviewer::application::ProbeStrength::Definitive;
    }

    rawviewer::application::DecodeResult decode(
        const rawviewer::application::OpenImageRequest&) const override {
        return {image_ ? image_ : makeUiImage(), {}, {}};
    }

private:
    std::shared_ptr<rawviewer::application::DecodedImage> image_;
};

std::shared_ptr<rawviewer::application::DecodedImage> makeUiImage() {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    image->metadata.width = 10;
    image->metadata.height = 10;
    image->metadata.bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    image->preview.width = 10;
    image->preview.height = 10;
    auto pixels = std::make_shared<std::vector<std::uint16_t>>(100);
    for (std::size_t index = 0; index < pixels->size(); ++index) {
        (*pixels)[index] = static_cast<std::uint16_t>(index);
    }
    image->preview.grayscale16Storage = pixels;
    image->preview.grayscale16Pixels = pixels->data();
    image->preview.grayscale16StrideSamples = 10;
    image->pixels = std::make_shared<UiPixelSource>();
    return image;
}

std::shared_ptr<rawviewer::application::DecodedImage> makeRectangularUiImage() {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    image->metadata.width = 3;
    image->metadata.height = 2;
    image->metadata.scalarType = rawviewer::domain::ScalarType::UInt16;
    image->metadata.bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    image->metadata.whiteLevel = 100.0;
    image->metadata.format = "Rectangular RAW";
    image->pixels = std::make_shared<CountingGridPixelSource>(3, 2);
    auto signal = std::make_shared<rawviewer::application::SignalPreview>();
    signal->width = 3;
    signal->height = 2;
    signal->preservesBayerPhase = true;
    signal->bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    signal->values = {0.0F, 1.0F, 2.0F, 10.0F, 11.0F, 12.0F};
    image->signalPreview = std::move(signal);
    return image;
}

std::shared_ptr<rawviewer::application::DecodedImage> makeContrastImage(
    const std::shared_ptr<ContrastPixelSource>& source) {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    image->metadata.width = 2;
    image->metadata.height = 1;
    image->metadata.scalarType = rawviewer::domain::ScalarType::UInt16;
    image->preview.width = 2;
    image->preview.height = 1;
    auto pixels = std::make_shared<std::vector<std::uint16_t>>();
    pixels->assign({0, 65535});
    image->preview.grayscale16Storage = pixels;
    image->preview.grayscale16Pixels = pixels->data();
    image->preview.grayscale16StrideSamples = 2;
    image->pixels = source;
    return image;
}

std::shared_ptr<rawviewer::application::DecodedImage> makeGridImage(
    const std::shared_ptr<CountingGridPixelSource>& source,
    rawviewer::domain::BayerPattern pattern =
        rawviewer::domain::BayerPattern::RGGB) {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    image->metadata.width = source->width();
    image->metadata.height = source->height();
    image->metadata.scalarType = rawviewer::domain::ScalarType::UInt16;
    image->metadata.bayerPattern = pattern;
    image->preview.width = static_cast<int>(source->width());
    image->preview.height = static_cast<int>(source->height());
    auto pixels = std::make_shared<std::vector<std::uint16_t>>(
        static_cast<std::size_t>(source->width() * source->height()), 32768);
    image->preview.grayscale16Storage = pixels;
    image->preview.grayscale16Pixels = pixels->data();
    image->preview.grayscale16StrideSamples = image->preview.width;
    image->pixels = source;
    return image;
}

std::shared_ptr<rawviewer::application::DecodedImage> makeRgbImage() {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.kind = rawviewer::application::ImageKind::Standard;
    image->metadata.width = 1;
    image->metadata.height = 1;
    image->preview.width = 1;
    image->preview.height = 1;
    image->preview.rgba = {255, 255, 255, 255};
    image->pixels = std::make_shared<RgbPixelSource>();
    return image;
}

QRect viewportCanvas(rawviewer::presentation::ImageViewport& viewport) {
    auto* horizontal = viewport.findChild<QScrollBar*>(
        QStringLiteral("imageHorizontalScrollBar"));
    auto* vertical = viewport.findChild<QScrollBar*>(
        QStringLiteral("imageVerticalScrollBar"));
    Q_ASSERT(horizontal);
    Q_ASSERT(vertical);
    constexpr int rulerExtent = 24;
    return QRect(rulerExtent, rulerExtent,
                 vertical->x() - rulerExtent,
                 horizontal->y() - rulerExtent);
}

QRectF fittedImageRect(rawviewer::presentation::ImageViewport& viewport,
                       int imageWidth,
                       int imageHeight) {
    const QRect canvas = viewportCanvas(viewport);
    const double zoom = viewport.zoom();
    const QSizeF size(imageWidth * zoom, imageHeight * zoom);
    return QRectF(canvas.left() + (canvas.width() - size.width()) / 2.0,
                  canvas.top() + (canvas.height() - size.height()) / 2.0,
                  size.width(), size.height());
}

QPoint imagePixelCenter(rawviewer::presentation::ImageViewport& viewport,
                        int imageWidth,
                        int imageHeight,
                        int x,
                        int y) {
    const QRectF image = fittedImageRect(viewport, imageWidth, imageHeight);
    return QPoint(static_cast<int>(std::lround(
                      image.left() + (x + 0.5) * viewport.zoom())),
                  static_cast<int>(std::lround(
                      image.top() + (y + 0.5) * viewport.zoom())));
}

void sendMouseMove(QWidget& widget,
                   const QPoint& position,
                   Qt::MouseButtons buttons = Qt::NoButton) {
    QMouseEvent event(QEvent::MouseMove,
                      QPointF(position),
                      QPointF(widget.mapToGlobal(position)),
                      Qt::NoButton,
                      buttons,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &event);
}

} // namespace

class PixelStatisticsUiTest final : public QObject {
    Q_OBJECT

private slots:
    void rendersGlobalChannelHistogramAndSharedDisplayWindow();
    void completesRectangleAndLineWithDrag();
    void exposesCompactTechnicalPaneLayout();
    void rendersBlackStatisticsPlotAndHoverGuides();
    void rendersFourBayerChannelResultPanes();
    void persistsPixelStatisticsPreferences();
    void editsWithTransformShortcutsAndGlobalUndo();
    void rendersContinuousCompletePixelValues();
    void rendersBayerMaskAndPatternLabels();
    void rendersExtractedPixelAnnotationsFromSourceCoordinates();
    void supportsMiddleButtonPanDuringStatisticsSelection();
    void rendersRulersOverviewAndSynchronizedScrollBars();
    void rendersExactExtractPixelsAtHighZoom();
    void rendersRgbValuesAsThreeLinesAtLowerLeft();
    void resetsPixelOverlayGeometryForSecondImage();
    void previewsPixelInfoOptionsOnRggbGrid();
    void enablesPixelOverlayBeforePixelInfoIsOpened();
    void statisticsComputesFourChannelsFromMainWindow();
    void statisticsUsesDisplayedBayerExtraction();
    void pixelAnnotationUsesSecondIndependentExtraction();
    void editsAndPersistsBayerMaskPatterns();
    void showsRecentFilesAndDisablesMissingDocuments();
    void configuresCompactProfessionalFilterDialog();
    void filtersTheCurrentlyDisplayedBayerExtraction();
    void configuresCompactProfessionalDemosaicDialog();
    void demosaicsTheCurrentlyDisplayedFilteredRaw();
};

void PixelStatisticsUiTest::rendersGlobalChannelHistogramAndSharedDisplayWindow() {
    using rawviewer::application::GlobalHistogramComponent;
    using rawviewer::application::GlobalHistogramMode;
    rawviewer::presentation::HistogramWidget histogram;
    histogram.resize(360, 220);
    rawviewer::application::GlobalHistogramResult result;
    result.mode = GlobalHistogramMode::BayerChannels;
    result.rangeMinimum = 0.0;
    result.rangeMaximum = 100.0;
    for (const auto component : {
             GlobalHistogramComponent::Red,
             GlobalHistogramComponent::BayerGreenRed,
             GlobalHistogramComponent::BayerGreenBlue,
             GlobalHistogramComponent::Blue}) {
        rawviewer::application::GlobalHistogramSeries series;
        series.component = component;
        series.bins.assign(64, 0);
        series.bins[8 + result.series.size() * 12] = 100;
        series.sampleCount = 100;
        result.series.push_back(std::move(series));
    }
    histogram.setResult(std::move(result));
    histogram.setDisplayWindow(10.0, 90.0);
    histogram.show();
    QVERIFY(QTest::qWaitForWindowExposed(&histogram));
    QCOMPARE(histogram.seriesCount(), std::size_t{4});
    QVERIFY(histogram.mode() == GlobalHistogramMode::BayerChannels);

    const auto rendered = histogram.grab().toImage().convertToFormat(
        QImage::Format_ARGB32);
    const auto projectionBuilds = histogram.projectionBuildCount();
    QVERIFY(projectionBuilds > 0);
    const auto hasColorNear = [&rendered](QColor target) {
        for (int y = 0; y < rendered.height(); ++y) {
            for (int x = 0; x < rendered.width(); ++x) {
                const QColor actual = rendered.pixelColor(x, y);
                if (std::abs(actual.red() - target.red()) < 8 &&
                    std::abs(actual.green() - target.green()) < 8 &&
                    std::abs(actual.blue() - target.blue()) < 8) {
                    return true;
                }
            }
        }
        return false;
    };
    QVERIFY(hasColorNear(QColor(239, 76, 82)));
    QVERIFY(hasColorNear(QColor(113, 224, 105)));
    QVERIFY(hasColorNear(QColor(54, 205, 181)));
    QVERIFY(hasColorNear(QColor(73, 133, 255)));

    QSignalSpy changed(
        &histogram,
        &rawviewer::presentation::HistogramWidget::displayWindowChanged);
    QTest::mousePress(&histogram, Qt::LeftButton, Qt::NoModifier,
                      QPoint(44, 110));
    QTest::mouseMove(&histogram, QPoint(100, 110), 10);
    QTest::mouseRelease(&histogram, Qt::LeftButton, Qt::NoModifier,
                        QPoint(100, 110));
    QVERIFY(changed.count() >= 1);
    QVERIFY(histogram.blackPoint() > 20.0);
    histogram.grab();
    QCOMPARE(histogram.projectionBuildCount(), projectionBuilds);
    histogram.setZoomed(true);
    QVERIFY(histogram.isZoomed());

    auto mappedSource = std::make_shared<CountingGridPixelSource>(100, 100);
    rawviewer::presentation::ImageViewport mappedViewport;
    mappedViewport.resize(320, 240);
    mappedViewport.setImage(makeGridImage(mappedSource));
    rawviewer::domain::DisplayMapping mapping;
    mapping.blackPoint = 0.0;
    mapping.whitePoint = 65535.0;
    mapping.gamma = 1.0;
    mappedViewport.setDisplayMapping(mapping);
    QImage wideWindow(mappedViewport.size(),
                      QImage::Format_ARGB32_Premultiplied);
    wideWindow.fill(Qt::transparent);
    mappedViewport.render(&wideWindow);
    mapping.whitePoint = 32768.0;
    mappedViewport.setDisplayMapping(mapping);
    QImage narrowWindow(mappedViewport.size(),
                        QImage::Format_ARGB32_Premultiplied);
    narrowWindow.fill(Qt::transparent);
    mappedViewport.render(&narrowWindow);
    mapping.blackPoint = 32768.0;
    mapping.whitePoint = 65535.0;
    mappedViewport.setDisplayMapping(mapping);
    QImage raisedBlack(mappedViewport.size(),
                       QImage::Format_ARGB32_Premultiplied);
    raisedBlack.fill(Qt::transparent);
    mappedViewport.render(&raisedBlack);
    const QPoint mappedCenter = fittedImageRect(mappedViewport, 100, 100)
        .center().toPoint();
    QVERIFY(qGray(narrowWindow.pixel(mappedCenter)) >
            qGray(wideWindow.pixel(mappedCenter)) + 80);
    QVERIFY(qGray(narrowWindow.pixel(mappedCenter)) > 245);
    QVERIFY(qGray(raisedBlack.pixel(mappedCenter)) < 10);

    QTemporaryFile input;
    QVERIFY(input.open());
    QVERIFY(input.write("raw") == 3);
    const QString path = input.fileName();
    input.close();
    auto integratedSource =
        std::make_shared<CountingGridPixelSource>(100, 100);
    auto integratedImage = makeGridImage(integratedSource);
    integratedImage->metadata.whiteLevel = 65535.0;
    auto decoder = std::make_shared<StaticRawDecoder>(integratedImage);
    auto service = std::make_shared<rawviewer::application::OpenImageService>(
        std::vector<std::shared_ptr<const rawviewer::application::IImageDecoder>>{
            decoder});
    auto store = std::make_shared<MemoryRecentDocumentStore>();
    rawviewer::presentation::MainWindow window(service, {}, store);
    window.resize(1000, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openPath(path);

    auto* integrated = window.findChild<
        rawviewer::presentation::HistogramWidget*>(
            QStringLiteral("globalHistogramWidget"));
    auto* black = window.findChild<QDoubleSpinBox*>(
        QStringLiteral("displayBlackPointSpin"));
    auto* white = window.findChild<QDoubleSpinBox*>(
        QStringLiteral("displayWhitePointSpin"));
    auto* zoom = window.findChild<QToolButton*>(
        QStringLiteral("histogramWindowZoomButton"));
    auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
    QVERIFY(integrated);
    QVERIFY(black);
    QVERIFY(white);
    QVERIFY(zoom);
    QVERIFY(undo);
    QTRY_VERIFY_WITH_TIMEOUT(
        integrated->mode() == GlobalHistogramMode::BayerChannels, 3000);
    QCOMPARE(integrated->seriesCount(), std::size_t{4});
    QCOMPARE(black->decimals(), 1);
    QCOMPARE(white->decimals(), 1);
    QVERIFY(black->isEnabled());
    QVERIFY(white->isEnabled());
    QVERIFY(zoom->isEnabled());
    QCOMPARE(integratedSource->sampleCalls, std::uint64_t{0});
    QVERIFY(!undo->isEnabled());
    const QPoint dragStart(10, integrated->height() / 2);
    const QPoint dragMiddle(integrated->width() / 5,
                            integrated->height() / 2);
    QTest::mousePress(integrated, Qt::LeftButton, Qt::NoModifier, dragStart);
    for (int step = 1; step <= 12; ++step) {
        QTest::mouseMove(
            integrated,
            dragStart + (dragMiddle - dragStart) * step / 12,
            1);
    }
    QVERIFY(black->value() > 0.0);
    QVERIFY(!undo->isEnabled());
    QTest::mouseRelease(integrated, Qt::LeftButton, Qt::NoModifier,
                        dragMiddle);
    QTRY_VERIFY(undo->isEnabled());
    QCOMPARE(integratedSource->sampleCalls, std::uint64_t{0});
    QCOMPARE(integrated->blackPoint(), black->value());
    QTest::mouseClick(zoom, Qt::LeftButton);
    QVERIFY(integrated->isZoomed());
}

void PixelStatisticsUiTest::completesRectangleAndLineWithDrag() {
    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(400, 400);
    viewport.setImage(makeUiImage());
    viewport.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewport));

    QSignalSpy spy(
        &viewport,
        &rawviewer::presentation::ImageViewport::statisticsSelectionCompleted);
    viewport.setStatisticsSelectionTool(
        rawviewer::presentation::StatisticsSelectionTool::Rectangle);
    const QPoint rectangleStart = imagePixelCenter(viewport, 10, 10, 0, 0);
    const QPoint rectangleEnd = imagePixelCenter(viewport, 10, 10, 9, 9);
    QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier,
                      rectangleStart);
    QCOMPARE(spy.count(), 0);
    QTest::mouseMove(&viewport, rectangleEnd);
    QCOMPARE(spy.count(), 0);
    QTest::mouseRelease(&viewport, Qt::LeftButton, Qt::NoModifier,
                        rectangleEnd);
    QCOMPARE(spy.count(), 1);
    auto arguments = spy.takeFirst();
    QCOMPARE(arguments[0].toLongLong(), 0);
    QCOMPARE(arguments[1].toLongLong(), 0);
    QCOMPARE(arguments[2].toLongLong(), 9);
    QCOMPARE(arguments[3].toLongLong(), 9);
    QCOMPARE(arguments[4].toBool(), false);

    viewport.setStatisticsSelectionTool(
        rawviewer::presentation::StatisticsSelectionTool::Line);
    const QPoint lineStart = imagePixelCenter(viewport, 10, 10, 1, 1);
    const QPoint lineEnd = imagePixelCenter(viewport, 10, 10, 8, 7);
    QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier,
                      lineStart);
    QTest::mouseMove(&viewport, lineEnd);
    QTest::mouseRelease(&viewport, Qt::LeftButton, Qt::NoModifier,
                        lineEnd);
    QCOMPARE(spy.count(), 1);
    arguments = spy.takeFirst();
    QCOMPARE(arguments[0].toLongLong(), 1);
    QCOMPARE(arguments[1].toLongLong(), 1);
    QCOMPARE(arguments[2].toLongLong(), 8);
    QCOMPARE(arguments[3].toLongLong(), 7);
    QCOMPARE(arguments[4].toBool(), true);
}

void PixelStatisticsUiTest::exposesCompactTechnicalPaneLayout() {
    rawviewer::presentation::PixelStatisticsDialog dialog;
    rawviewer::application::ImageMetadata metadata;
    metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    metadata.width = 100;
    metadata.height = 80;
    metadata.bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    dialog.setSource(&metadata);

    auto* layout = qobject_cast<QBoxLayout*>(dialog.layout());
    QVERIFY(layout);
    QCOMPARE(layout->stretch(0), 0);
    QCOMPARE(layout->stretch(1), 0);
    QCOMPARE(layout->stretch(2), 1);
    QVERIFY(dialog.width() <= 780);
    QVERIFY(dialog.minimumWidth() <= 640);
    auto* modePane = dialog.findChild<QWidget*>(
        QStringLiteral("statisticsModePane"));
    auto* analysisPane = dialog.findChild<QWidget*>(
        QStringLiteral("statisticsAnalysisPane"));
    QVERIFY(modePane);
    QVERIFY(analysisPane);
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("statisticsPlotPane")));
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("statisticsMetrics")));
    QVERIFY(!dialog.findChild<QWidget*>(
        QStringLiteral("statisticsStateLabel")));
    QVERIFY(dialog.styleSheet().contains(QStringLiteral("border-radius: 0px")));

    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QVERIFY(modePane->height() <= 36);
    QVERIFY(analysisPane->height() <= 120);
    for (auto* label : dialog.findChildren<QLabel*>()) {
        QVERIFY(label->text() != QStringLiteral("MODE"));
        QVERIFY(label->text() != QStringLiteral("ANALYSIS"));
        QVERIFY(label->text() != QStringLiteral("PLOT"));
        if (label->text() == QStringLiteral("COUNT")) {
            QVERIFY(label->font().pixelSize() >= 14);
        }
    }

    int modeButtonCount = 0;
    QToolButton* lineButton = nullptr;
    for (auto* button : dialog.findChildren<QToolButton*>()) {
        if (button->isCheckable()) {
            ++modeButtonCount;
        }
        if (button->text() == QStringLiteral("Line")) {
            lineButton = button;
        }
    }
    QCOMPARE(modeButtonCount, 5);
    QVERIFY(lineButton);
    QVERIFY(lineButton->isEnabled());
    QTest::mouseClick(lineButton, Qt::LeftButton);
    QCOMPARE(dialog.mode(),
             rawviewer::application::PixelStatisticsMode::Line);

    auto* channel = dialog.findChild<QComboBox*>(
        QStringLiteral("statisticsChannelCombo"));
    QVERIFY(!channel);
    auto* channels = dialog.findChild<QCheckBox*>(
        QStringLiteral("statisticsChannelsCheck"));
    QVERIFY(channels);
    QVERIFY(channels->isEnabled());
    QVERIFY(!dialog.findChild<QComboBox*>(
        QStringLiteral("statisticsBinsCombo")));
    QVERIFY(!dialog.findChild<QComboBox*>(
        QStringLiteral("statisticsLineWidthCombo")));

    rawviewer::application::ImageMetadata extracted = metadata;
    extracted.bayerPattern = rawviewer::domain::BayerPattern::None;
    dialog.setSource(&extracted);
    QVERIFY(!channels->isEnabled());
    QVERIFY(!channels->isChecked());
    QVERIFY(lineButton->isEnabled());
}

void PixelStatisticsUiTest::rendersBlackStatisticsPlotAndHoverGuides() {
    rawviewer::presentation::StatisticsChartWidget chart;
    chart.resize(500, 300);
    chart.setShowGrid(false);
    QPalette chartPalette = chart.palette();
    chartPalette.setColor(QPalette::Base, QColor(24, 24, 24));
    chartPalette.setColor(QPalette::Highlight, QColor(170, 170, 170));
    chart.setPalette(chartPalette);

    rawviewer::application::PixelStatisticsResult result;
    result.mode = rawviewer::application::PixelStatisticsMode::Line;
    result.plot.x = {0.0, 1.0, 2.0};
    result.plot.y = {0.0, 1.0, 0.0};
    chart.setResult(result);
    chart.show();
    QVERIFY(QTest::qWaitForWindowExposed(&chart));

    const auto render = [&chart] {
        QImage frame(chart.size(), QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::transparent);
        chart.render(&frame);
        return frame;
    };
    const QImage base = render();
    QCOMPARE(base.pixelColor(450, 100), QColor(207, 207, 207));

    const QRectF plot(76, 26, 396, 222);
    QPoint curvePoint;
    int darkest = 255;
    for (int y = static_cast<int>(plot.top()) + 3;
         y < static_cast<int>(plot.bottom()) - 3; ++y) {
        // Search well inside the segment rather than at a data vertex. This
        // verifies that hover follows the rendered polyline, even for sparse
        // profile data whose vertices are far apart.
        for (int x = 340; x <= 360; ++x) {
            const int gray = qGray(base.pixel(x, y));
            if (gray < darkest) {
                darkest = gray;
                curvePoint = QPoint(x, y);
            }
        }
    }
    QVERIFY2(darkest < 20,
             qPrintable(QStringLiteral("darkest curve pixel=%1 at %2,%3")
                 .arg(darkest).arg(curvePoint.x()).arg(curvePoint.y())));

    sendMouseMove(chart, curvePoint);
    const QImage hovered = render();
    int darkestHorizontalGuide = 255;
    int darkestVerticalGuide = 255;
    for (int y = curvePoint.y() - 2; y <= curvePoint.y() + 2; ++y) {
        for (int x = 100; x <= 120; ++x) {
            darkestHorizontalGuide = std::min(
                darkestHorizontalGuide, qGray(hovered.pixel(x, y)));
        }
    }
    for (int x = curvePoint.x() - 2; x <= curvePoint.x() + 2; ++x) {
        for (int y = 150; y <= 170; ++y) {
            darkestVerticalGuide = std::min(
                darkestVerticalGuide, qGray(hovered.pixel(x, y)));
        }
    }
    QVERIFY2(darkestHorizontalGuide < 80,
             qPrintable(QStringLiteral("horizontal guide=%1")
                 .arg(darkestHorizontalGuide)));
    QVERIFY2(darkestVerticalGuide < 80,
             qPrintable(QStringLiteral("vertical guide=%1")
                 .arg(darkestVerticalGuide)));
}

void PixelStatisticsUiTest::rendersFourBayerChannelResultPanes() {
    rawviewer::presentation::PixelStatisticsDialog dialog;
    rawviewer::application::ImageMetadata metadata;
    metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    metadata.width = 8;
    metadata.height = 8;
    metadata.bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    dialog.setSource(&metadata);

    auto* channels = dialog.findChild<QCheckBox*>(
        QStringLiteral("statisticsChannelsCheck"));
    QVERIFY(channels);
    QTest::mouseClick(channels, Qt::LeftButton);
    QVERIFY(dialog.channelsEnabled());

    const std::array bayerChannels{rawviewer::domain::BayerChannel::R,
                                   rawviewer::domain::BayerChannel::Gr,
                                   rawviewer::domain::BayerChannel::Gb,
                                   rawviewer::domain::BayerChannel::B};
    std::vector<rawviewer::application::PixelStatisticsResult> results;
    for (std::size_t index = 0; index < bayerChannels.size(); ++index) {
        rawviewer::application::PixelStatisticsResult result;
        result.channel = bayerChannels[index];
        result.summary = {4, static_cast<double>(index),
                          static_cast<double>(index + 6),
                          static_cast<double>(index + 3), 1.0};
        result.plot.x = {0.0, 1.0};
        result.plot.y = {static_cast<double>(index),
                         static_cast<double>(index + 1)};
        results.push_back(std::move(result));
    }
    dialog.setResults(results);

    auto* stack = dialog.findChild<QStackedWidget*>(
        QStringLiteral("statisticsResultStack"));
    auto* channelResults = dialog.findChild<QWidget*>(
        QStringLiteral("statisticsChannelResults"));
    QVERIFY(stack);
    QVERIFY(channelResults);
    QCOMPARE(stack->currentWidget(), channelResults);
    for (const QString channelName : {"R", "Gr", "Gb", "B"}) {
        QVERIFY(dialog.findChild<QWidget*>(
            QStringLiteral("statisticsChannelCard%1").arg(channelName)));
    }
}

void PixelStatisticsUiTest::persistsPixelStatisticsPreferences() {
    QCoreApplication::setOrganizationName(QStringLiteral("RawViewerTests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("PixelStatisticsPreferencesTest"));
    QSettings settings;
    settings.remove(QStringLiteral("pixelStatistics"));

    auto store = std::make_shared<MemoryRecentDocumentStore>();
    rawviewer::presentation::MainWindow window({}, {}, store);
    auto* action = window.findChild<QAction*>(
        QStringLiteral("pixelStatisticsPreferencesAction"));
    auto* statisticsDialog =
        window.findChild<rawviewer::presentation::PixelStatisticsDialog*>();
    QVERIFY(action);
    QVERIFY(statisticsDialog);

    int initialLineWidth = 0;
    QTimer::singleShot(0, [&initialLineWidth] {
        auto* preferences = qobject_cast<QDialog*>(
            QApplication::activeModalWidget());
        if (!preferences) {
            return;
        }
        auto* lineWidth = preferences->findChild<QSpinBox*>(
            QStringLiteral("statisticsPreferenceLineWidth"));
        auto* bins = preferences->findChild<QSpinBox*>(
            QStringLiteral("statisticsPreferenceHistogramBins"));
        auto* dataPoints = preferences->findChild<QCheckBox*>(
            QStringLiteral("statisticsPreferenceDataPoints"));
        auto* buttons = preferences->findChild<QDialogButtonBox*>();
        if (!lineWidth || !bins || !dataPoints || !buttons) {
            preferences->reject();
            return;
        }
        initialLineWidth = lineWidth->value();
        lineWidth->setValue(3);
        bins->setValue(512);
        dataPoints->setChecked(true);
        auto* accept = buttons->button(QDialogButtonBox::Ok);
        if (!accept) {
            preferences->reject();
            return;
        }
        QTest::mouseClick(accept, Qt::LeftButton);
    });
    action->trigger();

    QCOMPARE(initialLineWidth, 1);
    QCOMPARE(statisticsDialog->histogramBins(), std::uint32_t{512});
    settings.sync();
    QCOMPARE(settings.value(QStringLiteral("pixelStatistics/lineWidth")).toInt(),
             3);
    QCOMPARE(settings.value(QStringLiteral("pixelStatistics/histogramBins")).toInt(),
             512);
    QVERIFY(settings.value(
        QStringLiteral("pixelStatistics/showDataPoints")).toBool());
    settings.remove(QStringLiteral("pixelStatistics"));
}

void PixelStatisticsUiTest::editsWithTransformShortcutsAndGlobalUndo() {
    QTemporaryFile input;
    QVERIFY(input.open());
    QVERIFY(input.write("raw") == 3);
    const QString path = input.fileName();
    input.close();

    auto decoder = std::make_shared<StaticRawDecoder>(
        makeRectangularUiImage());
    auto service = std::make_shared<rawviewer::application::OpenImageService>(
        std::vector<std::shared_ptr<const rawviewer::application::IImageDecoder>>{
            decoder});
    auto store = std::make_shared<MemoryRecentDocumentStore>();
    rawviewer::presentation::MainWindow window(service, {}, store);
    window.resize(1000, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openPath(path);

    auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
    auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
    auto* mirror = window.findChild<QAction*>(
        QStringLiteral("mirrorHorizontalAction"));
    auto* rotateLeft = window.findChild<QAction*>(
        QStringLiteral("rotateLeftAction"));
    QVERIFY(undo);
    QVERIFY(redo);
    QVERIFY(mirror);
    QVERIFY(rotateLeft);
    QCOMPARE(undo->shortcut(), QKeySequence::Undo);
    QCOMPARE(redo->shortcut(), QKeySequence::Redo);

    const std::array transformNames{
        QStringLiteral("flipVerticalAction"),
        QStringLiteral("mirrorHorizontalAction"),
        QStringLiteral("rotateLeftAction"),
        QStringLiteral("rotateRightAction"),
        QStringLiteral("rotate180Action")};
    QStringList shortcuts;
    for (const auto& name : transformNames) {
        auto* action = window.findChild<QAction*>(name);
        QVERIFY(action);
        QTRY_VERIFY_WITH_TIMEOUT(action->isEnabled(), 3000);
        QVERIFY(!action->shortcut().isEmpty());
        QVERIFY(!shortcuts.contains(action->shortcut().toString()));
        shortcuts.push_back(action->shortcut().toString());
    }

    auto* task = window.findChild<QLabel*>(QStringLiteral("taskStatusLabel"));
    auto* image = window.findChild<QLabel*>(QStringLiteral("imageStatusLabel"));
    auto* coordinate = window.findChild<QLabel*>(
        QStringLiteral("coordinateLabel"));
    auto* viewport =
        window.findChild<rawviewer::presentation::ImageViewport*>();
    QVERIFY(task);
    QVERIFY(image);
    QVERIFY(coordinate);
    QVERIFY(viewport);
    QTRY_VERIFY_WITH_TIMEOUT(image->text().contains(QStringLiteral("3 × 2")),
                             3000);

    mirror->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        task->text().contains(QStringLiteral("已完成 Mirror")), 3000);
    QVERIFY(undo->isEnabled());
    viewport->imageCoordinateChanged(0, 0, true);
    QTRY_VERIFY_WITH_TIMEOUT(
        coordinate->text().contains(QStringLiteral("Raw 2")), 3000);

    undo->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(redo->isEnabled(), 3000);
    viewport->imageCoordinateChanged(0, 0, true);
    QTRY_VERIFY_WITH_TIMEOUT(
        coordinate->text().contains(QStringLiteral("Raw 0")), 3000);

    redo->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(undo->isEnabled(), 3000);
    viewport->imageCoordinateChanged(0, 0, true);
    QTRY_VERIFY_WITH_TIMEOUT(
        coordinate->text().contains(QStringLiteral("Raw 2")), 3000);

    rotateLeft->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(image->text().contains(QStringLiteral("2 × 3")),
                             3000);
    undo->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(image->text().contains(QStringLiteral("3 × 2")),
                             3000);
}

void PixelStatisticsUiTest::rendersContinuousCompletePixelValues() {
    rawviewer::presentation::PixelInfoDialog optionsDialog;
    QVERIFY(!optionsDialog.options().showMesh);

    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(320, 240);
    auto source = std::make_shared<ContrastPixelSource>();
    viewport.setImage(makeContrastImage(source));
    rawviewer::presentation::PixelOverlayOptions options;
    options.enabled = true;
    options.showBayerLabel = false;
    viewport.setPixelOverlayOptions(options);

    const auto render = [&viewport] {
        QImage output(viewport.size(), QImage::Format_ARGB32_Premultiplied);
        output.fill(Qt::transparent);
        viewport.render(&output);
        return output;
    };
    const auto boundsForTone = [](const QImage& image,
                                  const QRect& area,
                                  bool light) {
        QRect bounds;
        int count = 0;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                const int gray = qGray(image.pixel(x, y));
                if ((light && gray >= 220) || (!light && gray <= 35)) {
                    bounds |= QRect(x, y, 1, 1);
                    ++count;
                }
            }
        }
        return std::pair{bounds, count};
    };

    const QImage rendered = render();
    const int cellPixels = static_cast<int>(std::lround(viewport.zoom()));
    QCOMPARE(cellPixels, 141);
    const QRectF image = fittedImageRect(viewport, 2, 1);
    const auto centerArea = [cellPixels](const QPointF& center) {
        const int radius = std::max(12, cellPixels / 5);
        return QRect(static_cast<int>(std::lround(center.x())) - radius,
                     static_cast<int>(std::lround(center.y())) - radius,
                     radius * 2 + 1, radius * 2 + 1);
    };
    const QPointF firstCenter(
        image.left() + viewport.zoom() / 2.0, image.center().y());
    const QPointF secondCenter(
        image.left() + viewport.zoom() * 1.5, image.center().y());
    const auto [lightBounds, lightCount] =
        boundsForTone(rendered, centerArea(firstCenter), true);
    const auto [darkBounds, darkCount] =
        boundsForTone(rendered, centerArea(secondCenter), false);
    QVERIFY(lightCount > 0);
    QVERIFY(darkCount > 0);
    QVERIFY(lightBounds.width() < cellPixels / 4);
    QVERIFY(lightBounds.height() >= cellPixels / 12);
    QVERIFY(lightBounds.height() <= cellPixels / 5);
    QVERIFY(darkCount < 1200);
    QVERIFY(source->sampleCalls > 0);

    const auto samplesBeforePan = source->sampleCalls;
    QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier,
                      firstCenter.toPoint());
    QTest::mouseMove(&viewport, (firstCenter + QPointF(20, 0)).toPoint());
    const auto [pannedBounds, pannedCount] =
        boundsForTone(render(), centerArea(firstCenter + QPointF(20, 0)), true);
    QVERIFY(pannedCount > 0);
    QVERIFY(std::abs(pannedBounds.center().x() -
                     static_cast<int>(std::lround(firstCenter.x() + 20))) < 12);
    QCOMPARE(source->sampleCalls, samplesBeforePan);
    QTest::mouseRelease(&viewport, Qt::LeftButton, Qt::NoModifier,
                        (firstCenter + QPointF(20, 0)).toPoint());

    const auto samplesBeforeZoom = source->sampleCalls;
    QWheelEvent wheelEvent(
        firstCenter, firstCenter, QPoint(), QPoint(0, 120),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&viewport, &wheelEvent);
    const auto [zoomingBounds, zoomingCount] =
        boundsForTone(render(), centerArea(firstCenter + QPointF(20, 0)), true);
    Q_UNUSED(zoomingBounds);
    QVERIFY(zoomingCount > 0);
    QCOMPARE(source->sampleCalls, samplesBeforeZoom);

    rawviewer::presentation::ImageViewport completeViewport;
    completeViewport.resize(1100, 900);
    auto completeSource =
        std::make_shared<CountingGridPixelSource>(25, 20);
    completeViewport.setImage(makeGridImage(completeSource));
    rawviewer::presentation::PixelOverlayOptions completeOptions;
    completeOptions.enabled = true;
    completeOptions.showBayerLabel = false;
    completeViewport.setPixelOverlayOptions(completeOptions);
    QImage completeFrame(
        completeViewport.size(), QImage::Format_ARGB32_Premultiplied);
    completeFrame.fill(Qt::transparent);
    completeViewport.render(&completeFrame);
    QVERIFY(completeViewport.zoom() >= 40.0);
    QCOMPARE(completeSource->sampleCalls, std::uint64_t{500});
    completeViewport.render(&completeFrame);
    QCOMPARE(completeSource->sampleCalls, std::uint64_t{500});
}

void PixelStatisticsUiTest::rendersBayerMaskAndPatternLabels() {
    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(500, 500);
    auto source = std::make_shared<CountingGridPixelSource>(2, 2);
    viewport.setImage(makeGridImage(source));
    rawviewer::presentation::PixelOverlayOptions options;
    options.enabled = false;
    options.showMesh = true;
    options.showBayerLabel = false;
    viewport.setPixelOverlayOptions(options);

    QImage rendered(viewport.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    viewport.render(&rendered);
    QCOMPARE(viewport.zoom(), 231.0);
    QCOMPARE(source->sampleCalls, std::uint64_t{0});

    const QRectF image = fittedImageRect(viewport, 2, 2);
    const double cell = viewport.zoom();
    const auto meshPoint = [&image, cell](int x, int y) {
        return QPoint(static_cast<int>(std::lround(
                          image.left() + x * cell + cell * 0.25)),
                      static_cast<int>(std::lround(
                          image.top() + y * cell + cell * 0.75)));
    };
    const QColor red = rendered.pixelColor(meshPoint(0, 0));
    const QColor gr = rendered.pixelColor(meshPoint(1, 0));
    const QColor gb = rendered.pixelColor(meshPoint(0, 1));
    const QColor blue = rendered.pixelColor(meshPoint(1, 1));
    QVERIFY(red.red() > red.green() + 30);
    QVERIFY(gr.green() > gr.red() + 20);
    QVERIFY(gb.green() > gb.red() + 20);
    QVERIFY(gr != gb);
    QVERIFY(blue.blue() > blue.red() + 30);

    options.showMesh = false;
    options.showBayerLabel = true;
    viewport.setPixelOverlayOptions(options);
    rendered.fill(Qt::transparent);
    viewport.render(&rendered);

    const auto maximumScore = [&rendered](const QRect& area, auto score) {
        int maximum = 0;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                maximum = std::max(maximum, score(rendered.pixelColor(x, y)));
            }
        }
        return maximum;
    };
    const auto labelArea = [&image, cell](int x, int y) {
        const int right = static_cast<int>(std::lround(
            image.left() + (x + 1) * cell));
        const int bottom = static_cast<int>(std::lround(
            image.top() + (y + 1) * cell));
        return QRect(right - 65, bottom - 55, 60, 50);
    };
    const QRect rLabel = labelArea(0, 0);
    const QRect grLabel = labelArea(1, 0);
    const QRect gbLabel = labelArea(0, 1);
    const QRect bLabel = labelArea(1, 1);
    QVERIFY(maximumScore(rLabel, [](const QColor& color) {
        return color.red() - std::max(color.green(), color.blue());
    }) > 60);
    QVERIFY(maximumScore(grLabel, [](const QColor& color) {
        return color.green() - std::max(color.red(), color.blue());
    }) > 60);
    QVERIFY(maximumScore(gbLabel, [](const QColor& color) {
        return std::min(color.green(), color.blue()) - color.red();
    }) > 60);
    QVERIFY(maximumScore(bLabel, [](const QColor& color) {
        return color.blue() - std::max(color.red(), color.green());
    }) > 60);
}

void PixelStatisticsUiTest::rendersExtractedPixelAnnotationsFromSourceCoordinates() {
    auto source = std::make_shared<CountingGridPixelSource>(4, 4);
    rawviewer::application::BayerExtractRequest request;
    request.source = makeGridImage(source);
    request.mask = {"Gr", 2, 2, {0, 1, 0, 0}};
    const auto extracted =
        rawviewer::application::BayerExtractService().execute(request);
    QVERIFY2(extracted.succeeded(), extracted.message.c_str());
    QCOMPARE(extracted.extraction->image->metadata.bayerPattern,
             rawviewer::domain::BayerPattern::None);

    const auto display = rawviewer::application::PreviewRenderer::render(
        extracted.extraction->image, {});
    QVERIFY(display);
    source->sampleCalls = 0;

    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(500, 500);
    viewport.setImage(display);
    rawviewer::presentation::PixelOverlayOptions options;
    options.enabled = true;
    options.showMesh = true;
    options.showBayerLabel = true;
    viewport.setPixelOverlayOptions(options);

    QImage rendered(viewport.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    viewport.render(&rendered);
    QCOMPARE(viewport.zoom(), 231.0);
    QCOMPARE(source->sampleCalls, std::uint64_t{4});

    // Output (0, 0) maps to source (1, 0), which is Gr in RGGB. The
    // extracted image has no regular output pattern, so this green overlay
    // can only come from the packed pixel source's coordinate provenance.
    const QRectF image = fittedImageRect(viewport, 2, 2);
    const QColor mesh = rendered.pixelColor(QPoint(
        static_cast<int>(std::lround(image.left() + viewport.zoom() * 0.25)),
        static_cast<int>(std::lround(image.top() + viewport.zoom() * 0.75))));
    QVERIFY(mesh.green() > mesh.red() + 20);
    QVERIFY(mesh.green() > mesh.blue() + 20);

    int brightValuePixels = 0;
    const QPoint valueCenter(
        static_cast<int>(std::lround(image.left() + viewport.zoom() * 0.5)),
        static_cast<int>(std::lround(image.top() + viewport.zoom() * 0.5)));
    for (int y = valueCenter.y() - 40; y <= valueCenter.y() + 40; ++y) {
        for (int x = valueCenter.x() - 40; x <= valueCenter.x() + 40; ++x) {
            if (qGray(rendered.pixel(x, y)) >= 190) {
                ++brightValuePixels;
            }
        }
    }
    QVERIFY(brightValuePixels > 0);

    viewport.render(&rendered);
    QCOMPARE(source->sampleCalls, std::uint64_t{4});
}

void PixelStatisticsUiTest::supportsMiddleButtonPanDuringStatisticsSelection() {
    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(500, 400);
    viewport.setImage(makeUiImage());
    viewport.setStatisticsSelectionTool(
        rawviewer::presentation::StatisticsSelectionTool::Rectangle);
    viewport.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewport));

    QSignalSpy coordinates(
        &viewport,
        &rawviewer::presentation::ImageViewport::imageCoordinateChanged);
    QSignalSpy selections(
        &viewport,
        &rawviewer::presentation::ImageViewport::statisticsSelectionCompleted);
    const QPoint anchor = imagePixelCenter(viewport, 10, 10, 5, 5);
    sendMouseMove(viewport, anchor);
    QVERIFY(!coordinates.isEmpty());
    const qint64 beforeX = coordinates.last()[0].toLongLong();

    QTest::mousePress(&viewport, Qt::MiddleButton, Qt::NoModifier, anchor);
    sendMouseMove(viewport, anchor + QPoint(35, 20), Qt::MiddleButton);
    QTest::mouseRelease(&viewport, Qt::MiddleButton, Qt::NoModifier,
                        anchor + QPoint(35, 20));
    QCOMPARE(selections.count(), 0);

    coordinates.clear();
    sendMouseMove(viewport, anchor);
    QVERIFY(!coordinates.isEmpty());
    const qint64 afterX = coordinates.last()[0].toLongLong();
    QVERIFY(afterX < beforeX);
    QCOMPARE(viewport.cursor().shape(), Qt::CrossCursor);
}

void PixelStatisticsUiTest::rendersRulersOverviewAndSynchronizedScrollBars() {
    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(500, 360);
    QPalette rulerPalette = viewport.palette();
    const QColor rulerBackground(210, 220, 230);
    const QColor rulerTick(15, 25, 35);
    rulerPalette.setColor(QPalette::Window, rulerBackground);
    rulerPalette.setColor(QPalette::WindowText, rulerTick);
    rulerPalette.setColor(QPalette::Mid, QColor(80, 90, 100));
    viewport.setPalette(rulerPalette);
    auto source = std::make_shared<CountingGridPixelSource>(10, 10);
    viewport.setImage(makeGridImage(source));
    viewport.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewport));

    auto* horizontal = viewport.findChild<QScrollBar*>(
        QStringLiteral("imageHorizontalScrollBar"));
    auto* vertical = viewport.findChild<QScrollBar*>(
        QStringLiteral("imageVerticalScrollBar"));
    QVERIFY(horizontal);
    QVERIFY(vertical);
    QCOMPARE(horizontal->y(), viewport.height() - horizontal->height());
    QCOMPARE(vertical->x(), viewport.width() - vertical->width());
    QVERIFY(!horizontal->isEnabled());
    QVERIFY(!vertical->isEnabled());

    const QRect canvas = viewportCanvas(viewport);
    const QRectF imageRect = fittedImageRect(viewport, 10, 10);
    QImage fitted(viewport.size(), QImage::Format_ARGB32_Premultiplied);
    fitted.fill(Qt::transparent);
    viewport.render(&fitted);

    QCOMPARE(fitted.pixelColor(5, 5), rulerBackground);
    QCOMPARE(fitted.pixelColor(5, canvas.center().y()), rulerBackground);

    // At this zoom the major ruler interval is five source pixels. The top
    // X ruler and left Y ruler must align to the same transformed pixel grid.
    const int tickX = static_cast<int>(std::lround(
        imageRect.left() + 5.0 * viewport.zoom()));
    const int tickY = static_cast<int>(std::lround(
        imageRect.top() + 5.0 * viewport.zoom()));
    bool topTick = false;
    bool leftTick = false;
    for (int delta = -1; delta <= 1; ++delta) {
        for (int y = canvas.top() - 11; y < canvas.top(); ++y) {
            const QColor color = fitted.pixelColor(tickX + delta, y);
            topTick = topTick ||
                (std::abs(color.red() - rulerTick.red()) < 20 &&
                 std::abs(color.green() - rulerTick.green()) < 20 &&
                 std::abs(color.blue() - rulerTick.blue()) < 20);
        }
        for (int x = canvas.left() - 11; x < canvas.left(); ++x) {
            const QColor color = fitted.pixelColor(x, tickY + delta);
            leftTick = leftTick ||
                (std::abs(color.red() - rulerTick.red()) < 20 &&
                 std::abs(color.green() - rulerTick.green()) < 20 &&
                 std::abs(color.blue() - rulerTick.blue()) < 20);
        }
    }
    QVERIFY(topTick);
    QVERIFY(leftTick);

    const auto orangeBounds = [&canvas](const QImage& frame) {
        QRect bounds;
        for (int y = canvas.top(); y <= canvas.bottom(); ++y) {
            for (int x = canvas.center().x(); x <= canvas.right(); ++x) {
                const QColor color = frame.pixelColor(x, y);
                if (color.red() >= 230 && color.green() >= 130 &&
                    color.green() <= 215 && color.blue() <= 90) {
                    bounds |= QRect(x, y, 1, 1);
                }
            }
        }
        return bounds;
    };
    const QRect fittedIndicator = orangeBounds(fitted);
    QVERIFY(!fittedIndicator.isEmpty());

    QWheelEvent zoomEvent(
        QPointF(canvas.center()), QPointF(canvas.center()), QPoint(),
        QPoint(0, 720), Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&viewport, &zoomEvent);
    QVERIFY(horizontal->isEnabled());
    QVERIFY(vertical->isEnabled());
    QVERIFY(horizontal->maximum() > 0);
    QVERIFY(vertical->maximum() > 0);

    QImage zoomed(viewport.size(), QImage::Format_ARGB32_Premultiplied);
    zoomed.fill(Qt::transparent);
    viewport.render(&zoomed);
    const QRect zoomedIndicator = orangeBounds(zoomed);
    QVERIFY(!zoomedIndicator.isEmpty());
    QVERIFY(zoomedIndicator.width() < fittedIndicator.width());
    QVERIFY(zoomedIndicator.height() < fittedIndicator.height());

    QSignalSpy coordinates(
        &viewport,
        &rawviewer::presentation::ImageViewport::imageCoordinateChanged);
    sendMouseMove(viewport, canvas.center());
    QVERIFY(!coordinates.isEmpty());
    const qint64 beforeX = coordinates.last()[0].toLongLong();
    horizontal->setValue(horizontal->maximum());
    vertical->setValue(vertical->maximum());
    coordinates.clear();
    sendMouseMove(viewport, canvas.center() + QPoint(1, 0));
    sendMouseMove(viewport, canvas.center());
    QVERIFY(!coordinates.isEmpty());
    QVERIFY(coordinates.last()[0].toLongLong() > beforeX);
}

void PixelStatisticsUiTest::rendersExactExtractPixelsAtHighZoom() {
    auto source = std::make_shared<ContrastPixelSource>();
    auto image = makeContrastImage(source);
    auto reducedPreview =
        std::make_shared<std::vector<std::uint16_t>>(1, std::uint16_t{0});
    image->preview.width = 1;
    image->preview.height = 1;
    image->preview.grayscale16Storage = reducedPreview;
    image->preview.grayscale16Pixels = reducedPreview->data();
    image->preview.grayscale16StrideSamples = 1;

    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(500, 240);
    viewport.setImage(image);
    QImage first(viewport.size(), QImage::Format_ARGB32_Premultiplied);
    first.fill(Qt::transparent);
    viewport.render(&first);

    const QRectF imageRect = fittedImageRect(viewport, 2, 1);
    const QColor left = first.pixelColor(QPointF(
        imageRect.left() + imageRect.width() * 0.25,
        imageRect.center().y()).toPoint());
    const QColor right = first.pixelColor(QPointF(
        imageRect.left() + imageRect.width() * 0.75,
        imageRect.center().y()).toPoint());
    QVERIFY(qGray(left.rgb()) < 20);
    QVERIFY(qGray(right.rgb()) > 235);
    QCOMPARE(source->sampleCalls, std::uint64_t{2});

    viewport.render(&first);
    QCOMPARE(source->sampleCalls, std::uint64_t{2});
}

void PixelStatisticsUiTest::rendersRgbValuesAsThreeLinesAtLowerLeft() {
    rawviewer::presentation::PixelInfoDialog dialog;
    QCOMPARE(dialog.findChildren<QCheckBox*>().size(), 3);
    QVERIFY(dialog.findChild<QCheckBox*>(
        QStringLiteral("pixelValueOverlayCheck")));
    QVERIFY(dialog.findChild<QCheckBox*>(QStringLiteral("bayerMeshCheck")));
    QVERIFY(dialog.findChild<QCheckBox*>(QStringLiteral("bayerPatternCheck")));
    QVERIFY(dialog.findChildren<QDoubleSpinBox*>().isEmpty());
    const auto options = dialog.options();
    QVERIFY(options.enabled);
    QVERIFY(!options.showMesh);
    QVERIFY(!options.showBayerLabel);

    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(500, 400);
    viewport.setImage(makeRgbImage());
    rawviewer::presentation::PixelOverlayOptions rgbOptions;
    rgbOptions.enabled = true;
    rgbOptions.showBayerLabel = false;
    viewport.setPixelOverlayOptions(rgbOptions);
    QImage rendered(viewport.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    viewport.render(&rendered);

    const QRectF image = fittedImageRect(viewport, 1, 1);
    QRect textBounds;
    std::array<int, 3> channelPixelCounts{};
    const QRect scan(
        static_cast<int>(std::floor(image.left() + 5.0)),
        static_cast<int>(std::floor(image.top() + image.height() * 0.25)),
        static_cast<int>(std::floor(image.width() * 0.6)),
        static_cast<int>(std::floor(image.height() * 0.75 - 5.0)));
    for (int y = scan.top(); y <= scan.bottom(); ++y) {
        for (int x = scan.left(); x <= scan.right(); ++x) {
            const QColor color = rendered.pixelColor(x, y);
            const int redScore = color.red() -
                std::max(color.green(), color.blue());
            const int greenScore = color.green() -
                std::max(color.red(), color.blue());
            const int blueScore = color.blue() -
                std::max(color.red(), color.green());
            if (redScore > 30 || greenScore > 25 || blueScore > 30) {
                textBounds |= QRect(x, y, 1, 1);
                channelPixelCounts[0] += redScore > 30;
                channelPixelCounts[1] += greenScore > 25;
                channelPixelCounts[2] += blueScore > 30;
            }
        }
    }
    QVERIFY(!textBounds.isEmpty());
    QVERIFY(textBounds.left() < image.left() + image.width() * 0.2);
    QVERIFY(textBounds.bottom() > image.top() + image.height() * 0.8);
    QVERIFY2(textBounds.height() >= viewport.zoom() / 5.0,
             qPrintable(QStringLiteral("text=%1x%2 zoom=%3 scan=%4x%5")
                 .arg(textBounds.width()).arg(textBounds.height())
                 .arg(viewport.zoom()).arg(scan.width()).arg(scan.height())));
    QVERIFY(textBounds.height() <= viewport.zoom() * 0.5);
    for (const int count : channelPixelCounts) {
        QVERIFY(count > 0);
    }
}

void PixelStatisticsUiTest::resetsPixelOverlayGeometryForSecondImage() {
    rawviewer::presentation::ImageViewport viewport;
    viewport.resize(600, 600);
    rawviewer::presentation::PixelOverlayOptions options;
    options.enabled = true;
    viewport.setPixelOverlayOptions(options);

    auto firstSource = std::make_shared<ContrastPixelSource>();
    viewport.setImage(makeContrastImage(firstSource));
    viewport.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewport));
    const QPoint firstCenter = fittedImageRect(viewport, 2, 1).center().toPoint();
    QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, firstCenter);

    QSignalSpy coordinates(
        &viewport,
        &rawviewer::presentation::ImageViewport::imageCoordinateChanged);
    auto secondSource = std::make_shared<CountingGridPixelSource>(10, 10);
    viewport.setImage(makeGridImage(secondSource));
    QVERIFY(!coordinates.isEmpty());
    QCOMPARE(coordinates.last()[2].toBool(), false);

    QImage rendered(viewport.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    viewport.render(&rendered);
    QVERIFY(secondSource->sampleCalls > 0);

    coordinates.clear();
    const QPoint secondCenter = fittedImageRect(viewport, 10, 10)
        .center().toPoint();
    sendMouseMove(viewport, secondCenter);
    QVERIFY(!coordinates.isEmpty());
    QCOMPARE(coordinates.last()[0].toLongLong(), qint64{5});
    QCOMPARE(coordinates.last()[1].toLongLong(), qint64{5});
    QCOMPARE(coordinates.last()[2].toBool(), true);
}

void PixelStatisticsUiTest::previewsPixelInfoOptionsOnRggbGrid() {
    rawviewer::presentation::PixelInfoDialog dialog;
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    auto* preview = dialog.findChild<QWidget*>(
        QStringLiteral("pixelOverlayPreview"));
    auto* enabled = dialog.findChild<QCheckBox*>(
        QStringLiteral("pixelValueOverlayCheck"));
    auto* mesh = dialog.findChild<QCheckBox*>(
        QStringLiteral("bayerMeshCheck"));
    auto* pattern = dialog.findChild<QCheckBox*>(
        QStringLiteral("bayerPatternCheck"));
    QVERIFY(preview);
    QVERIFY(enabled);
    QVERIFY(mesh);
    QVERIFY(pattern);
    QCOMPARE(preview->property("gridColumns").toInt(), 4);
    QCOMPARE(preview->property("gridRows").toInt(), 4);
    QVERIFY(!dialog.styleSheet().contains(QStringLiteral("border-radius")));

    const auto renderPreview = [preview] {
        QImage frame(preview->size(), QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::transparent);
        preview->render(&frame);
        return frame;
    };
    const auto differs = [](const QImage& left, const QImage& right) {
        if (left.size() != right.size()) return true;
        for (int y = 0; y < left.height(); ++y) {
            for (int x = 0; x < left.width(); ++x) {
                if (left.pixel(x, y) != right.pixel(x, y)) return true;
            }
        }
        return false;
    };

    const QImage valuesOnly = renderPreview();
    mesh->setChecked(true);
    QCoreApplication::processEvents();
    const QImage withMesh = renderPreview();
    QVERIFY(differs(valuesOnly, withMesh));
    pattern->setChecked(true);
    QCoreApplication::processEvents();
    const QImage withPattern = renderPreview();
    QVERIFY(differs(withMesh, withPattern));
    enabled->setChecked(false);
    QCoreApplication::processEvents();
    const QImage withoutValues = renderPreview();
    QVERIFY(differs(withPattern, withoutValues));
}

void PixelStatisticsUiTest::enablesPixelOverlayBeforePixelInfoIsOpened() {
    auto store = std::make_shared<MemoryRecentDocumentStore>();
    rawviewer::presentation::MainWindow window({}, {}, store);
    auto* viewport = window.findChild<rawviewer::presentation::ImageViewport*>();
    QVERIFY(viewport);
    viewport->resize(320, 240);

    auto source = std::make_shared<ContrastPixelSource>();
    viewport->setImage(makeContrastImage(source));
    QImage rendered(viewport->size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    viewport->render(&rendered);

    QVERIFY(source->sampleCalls > 0);
}

void PixelStatisticsUiTest::statisticsComputesFourChannelsFromMainWindow() {
    QTemporaryFile input;
    QVERIFY(input.open());
    QVERIFY(input.write("raw") == 3);
    const QString path = input.fileName();
    input.close();

    auto decoder = std::make_shared<StaticRawDecoder>();
    auto service = std::make_shared<rawviewer::application::OpenImageService>(
        std::vector<std::shared_ptr<const rawviewer::application::IImageDecoder>>{
            decoder});
    auto store = std::make_shared<MemoryRecentDocumentStore>();
    rawviewer::presentation::MainWindow window(service, {}, store);
    window.resize(1100, 720);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openPath(path);

    QAction* statisticsAction = nullptr;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("Pixel Statistics")) {
            statisticsAction = action;
        }
    }
    QVERIFY(statisticsAction);
    QTRY_VERIFY_WITH_TIMEOUT(statisticsAction->isEnabled(), 3000);
    statisticsAction->trigger();

    auto* dialog =
        window.findChild<rawviewer::presentation::PixelStatisticsDialog*>();
    auto* viewport =
        window.findChild<rawviewer::presentation::ImageViewport*>();
    QVERIFY(dialog);
    QVERIFY(viewport);
    auto* channels = dialog->findChild<QCheckBox*>(
        QStringLiteral("statisticsChannelsCheck"));
    QVERIFY(channels);
    QTest::mouseClick(channels, Qt::LeftButton);

    const QPoint start = imagePixelCenter(*viewport, 10, 10, 0, 0);
    const QPoint end = imagePixelCenter(*viewport, 10, 10, 9, 9);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(viewport, end);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);

    auto* stack = dialog->findChild<QStackedWidget*>(
        QStringLiteral("statisticsResultStack"));
    auto* channelResults = dialog->findChild<QWidget*>(
        QStringLiteral("statisticsChannelResults"));
    QVERIFY(stack);
    QVERIFY(channelResults);
    QTRY_COMPARE_WITH_TIMEOUT(stack->currentWidget(), channelResults, 3000);
    int summariesWithExpectedCount = 0;
    for (auto* label : channelResults->findChildren<QLabel*>()) {
        if (label->property("role") == QStringLiteral("channelSummary") &&
            label->text().contains(QStringLiteral("25"))) {
            ++summariesWithExpectedCount;
        }
    }
    QCOMPARE(summariesWithExpectedCount, 4);
}

void PixelStatisticsUiTest::statisticsUsesDisplayedBayerExtraction() {
    QTemporaryFile input;
    QVERIFY(input.open());
    QVERIFY(input.write("raw") == 3);
    const QString path = input.fileName();
    input.close();

    auto decoder = std::make_shared<StaticRawDecoder>();
    auto service = std::make_shared<rawviewer::application::OpenImageService>(
        std::vector<std::shared_ptr<const rawviewer::application::IImageDecoder>>{
            decoder});
    auto store = std::make_shared<MemoryRecentDocumentStore>();
    rawviewer::presentation::MainWindow window(service, {}, store);
    window.resize(1100, 720);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openPath(path);

    const auto findAction = [&window](const QString& text) {
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text() == text) {
                return action;
            }
        }
        return static_cast<QAction*>(nullptr);
    };
    auto* extractAction = findAction(QStringLiteral("Bayer Extract"));
    auto* statisticsAction = findAction(QStringLiteral("Pixel Statistics"));
    QVERIFY(extractAction);
    QVERIFY(statisticsAction);
    QTRY_VERIFY_WITH_TIMEOUT(extractAction->isEnabled(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(statisticsAction->isEnabled(), 3000);

    extractAction->trigger();
    auto* extractDialog =
        window.findChild<rawviewer::presentation::BayerExtractDialog*>();
    QVERIFY(extractDialog);
    QPushButton* extractButton = nullptr;
    for (auto* button : extractDialog->findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Extract")) {
            extractButton = button;
        }
    }
    QVERIFY(extractButton);
    QTest::mouseClick(extractButton, Qt::LeftButton);

    auto* viewport =
        window.findChild<rawviewer::presentation::ImageViewport*>();
    QVERIFY(viewport);
    QTRY_VERIFY_WITH_TIMEOUT(extractButton->isEnabled(), 3000);
    statisticsAction->trigger();

    auto* statisticsDialog =
        window.findChild<rawviewer::presentation::PixelStatisticsDialog*>();
    QVERIFY(statisticsDialog);
    QVERIFY(statisticsDialog->isVisible());
    const QPoint start = imagePixelCenter(*viewport, 5, 5, 0, 0);
    const QPoint end = imagePixelCenter(*viewport, 5, 5, 4, 4);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(viewport, end);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);

    auto* selection = statisticsDialog->findChild<QLabel*>(
        QStringLiteral("statisticsSelectionLabel"));
    QVERIFY(selection);
    QTRY_VERIFY_WITH_TIMEOUT(selection->text().contains(
        QStringLiteral("(0, 0) → (4, 4)")), 3000);
}

void PixelStatisticsUiTest::pixelAnnotationUsesSecondIndependentExtraction() {
    QTemporaryFile input;
    QVERIFY(input.open());
    QVERIFY(input.write("raw") == 3);
    const QString path = input.fileName();
    input.close();

    auto decoder = std::make_shared<StaticRawDecoder>();
    auto service = std::make_shared<rawviewer::application::OpenImageService>(
        std::vector<std::shared_ptr<const rawviewer::application::IImageDecoder>>{
            decoder});
    auto store = std::make_shared<MemoryRecentDocumentStore>();
    rawviewer::presentation::MainWindow window(service, {}, store);
    window.resize(1100, 720);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openPath(path);

    QAction* extractAction = nullptr;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("Bayer Extract")) {
            extractAction = action;
        }
    }
    QVERIFY(extractAction);
    QTRY_VERIFY_WITH_TIMEOUT(extractAction->isEnabled(), 3000);
    extractAction->trigger();

    auto* dialog =
        window.findChild<rawviewer::presentation::BayerExtractDialog*>();
    auto* viewport =
        window.findChild<rawviewer::presentation::ImageViewport*>();
    auto* coordinate = window.findChild<QLabel*>(
        QStringLiteral("coordinateLabel"));
    QVERIFY(dialog);
    QVERIFY(viewport);
    QVERIFY(coordinate);
    QPushButton* extractButton = nullptr;
    for (auto* button : dialog->findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Extract")) {
            extractButton = button;
        }
    }
    QVERIFY(extractButton);

    // First extract the top-left R position.
    QTest::mouseClick(extractButton, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(extractButton->isEnabled(), 3000);

    // Then select top-right Gr. If MainWindow chained this request onto the
    // first 5x5 R result, output (0, 0) would be 2. From original it is 1.
    const auto cells =
        dialog->findChildren<QToolButton*>(QStringLiteral("maskCell"));
    QCOMPARE(cells.size(), 4);
    QVERIFY(cells[0]->isChecked());
    cells[0]->setChecked(false);
    cells[1]->setChecked(true);
    QTest::mouseClick(extractButton, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(extractButton->isEnabled(), 3000);

    viewport->imageCoordinateChanged(0, 0, true);
    QTRY_VERIFY_WITH_TIMEOUT(
        coordinate->text().contains(QStringLiteral("Extract")), 3000);
    QVERIFY2(coordinate->text().contains(QStringLiteral("Raw 1")),
             qPrintable(coordinate->text()));
    QVERIFY(coordinate->text().contains(QStringLiteral("坐标 0, 0")));
    QVERIFY(!coordinate->text().contains(QStringLiteral("Source")));
}

void PixelStatisticsUiTest::editsAndPersistsBayerMaskPatterns() {
    QCoreApplication::setOrganizationName(QStringLiteral("RawViewerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("BayerMaskUiTest"));
    QSettings settings;
    settings.remove(QStringLiteral("bayerExtract/customPatterns"));

    rawviewer::presentation::BayerExtractDialog dialog;
    rawviewer::application::ImageMetadata metadata;
    metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    metadata.width = 11;
    metadata.height = 10;
    metadata.bayerPattern = rawviewer::domain::BayerPattern::None;
    dialog.setSource(&metadata);

    auto* patterns = dialog.findChild<QComboBox*>(QStringLiteral("patternCombo"));
    auto* order = dialog.findChild<QComboBox*>(QStringLiteral("packingOrderCombo"));
    auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("patternNameEdit"));
    auto* columns = dialog.findChild<QSpinBox*>(QStringLiteral("patternColumnsSpin"));
    auto* rows = dialog.findChild<QSpinBox*>(QStringLiteral("patternRowsSpin"));
    auto* customEditor =
        dialog.findChild<QWidget*>(QStringLiteral("customPatternEditor"));
    auto* matrixScroll =
        dialog.findChild<QScrollArea*>(QStringLiteral("patternMatrixScroll"));
    auto* packingHelp =
        dialog.findChild<QToolButton*>(QStringLiteral("packingHelpButton"));
    auto* patternTitle =
        dialog.findChild<QLabel*>(QStringLiteral("sectionTitle"));
    auto* matrix = dialog.findChild<QWidget*>(QStringLiteral("patternMatrix"));
    QVERIFY(patterns);
    QVERIFY(order);
    QVERIFY(name);
    QVERIFY(columns);
    QVERIFY(rows);
    QVERIFY(customEditor);
    QVERIFY(matrixScroll);
    QVERIFY(packingHelp);
    QVERIFY(patternTitle);
    QVERIFY(matrix);
    QCOMPARE(patterns->count(), 4);
    QCOMPARE(patterns->itemText(3), QStringLiteral("Custom"));
    QCOMPARE(order->itemText(0), QStringLiteral("Row-major"));
    QCOMPARE(order->itemText(1), QStringLiteral("Column-major"));
    QCOMPARE(packingHelp->text(), QStringLiteral("?"));
    QCOMPARE(packingHelp->size(), QSize(18, 18));
    QVERIFY(packingHelp->autoRaise());
    QCOMPARE(patternTitle->text(), QStringLiteral("Pattern"));
    QVERIFY(patternTitle->height() * 2 >= patternTitle->fontMetrics().height() * 3);
    QCOMPARE(qobject_cast<QGridLayout*>(matrix->layout())->spacing(), 3);
    QVERIFY(customEditor->isHidden());
    QCOMPARE(columns->value(), 2);
    QCOMPARE(rows->value(), 2);
    auto initialCells =
        dialog.findChildren<QToolButton*>(QStringLiteral("maskCell"));
    QCOMPARE(initialCells.size(), 4);
    for (const auto* cell : initialCells) {
        QVERIFY(cell->text().isEmpty());
        QCOMPARE(cell->width(), cell->height());
        QCOMPARE(cell->width(), 48);
    }
    for (const auto* label : dialog.findChildren<QLabel*>()) {
        QVERIFY(!label->text().startsWith(QStringLiteral("Source")));
    }
    QCOMPARE(dialog.findChildren<QLabel*>(QStringLiteral("axisLabel")).size(), 4);
    QVERIFY(dialog.findChildren<QCheckBox*>().isEmpty());
    QVERIFY(dialog.findChildren<QLabel*>(QStringLiteral("selectionChip")).isEmpty());
    QVERIFY(patterns->maximumWidth() <= 88);
    QVERIFY(order->maximumWidth() <= 126);
    const auto compactSize = dialog.size();
    QVERIFY(compactSize.width() < 420);
    QVERIFY(compactSize.height() < 390);
    QVERIFY(matrixScroll->height() * 5 >= compactSize.height() * 2);
    patterns->setCurrentIndex(1);
    auto fourByFourCells =
        dialog.findChildren<QToolButton*>(QStringLiteral("maskCell"));
    QCOMPARE(fourByFourCells.size(), 16);
    QCOMPARE(fourByFourCells.front()->size(), QSize(36, 36));
    patterns->setCurrentIndex(2);
    auto eightByEightCells =
        dialog.findChildren<QToolButton*>(QStringLiteral("maskCell"));
    QCOMPARE(eightByEightCells.size(), 64);
    QCOMPARE(eightByEightCells.front()->size(), QSize(24, 24));
    QVERIFY(dialog.height() > compactSize.height());
    patterns->setCurrentIndex(0);
    QCOMPARE(dialog.size(), compactSize);
    QVERIFY(dialog.needsPartialEdgeConfirmation());
    QPushButton* extract = nullptr;
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Extract")) extract = button;
    }
    QVERIFY(extract);
    QVERIFY(extract->isEnabled());

    patterns->setCurrentIndex(3);
    QVERIFY(!customEditor->isHidden());
    columns->setValue(16);
    rows->setValue(1);
    auto customCells =
        dialog.findChildren<QToolButton*>(QStringLiteral("maskCell"));
    QCOMPARE(customCells.size(), 16);
    QCOMPARE(customCells.front()->size(), QSize(12, 12));
    columns->setValue(2);
    rows->setValue(2);
    name->setText(QStringLiteral("Three-cell test"));
    auto cells = dialog.findChildren<QToolButton*>(QStringLiteral("maskCell"));
    cells[1]->setChecked(true);
    cells[3]->setChecked(true);
    order->setCurrentIndex(1);
    QPushButton* save = nullptr;
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Save")) save = button;
    }
    QVERIFY(save);
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(patterns->count(), 5);
    QCOMPARE(patterns->itemText(4), QStringLiteral("Custom"));

    const auto request = dialog.request(makeUiImage(), {});
    QCOMPARE(request.mask.name, std::string("Three-cell test"));
    QCOMPARE(request.mask.selectedCount(), std::uint64_t{3});
    QCOMPARE(request.packingOrder,
             rawviewer::application::BayerPackingOrder::ColumnMajor);
    QVERIFY(!request.sourceRegion.has_value());
    bool hasCsvButton = false;
    for (const auto* button : dialog.findChildren<QPushButton*>()) {
        hasCsvButton = hasCsvButton || button->text().contains("CSV");
    }
    QVERIFY(!hasCsvButton);
    QSettings persisted;
    persisted.sync();
    QVERIFY(!persisted.value(QStringLiteral("bayerExtract/customPatterns"))
                 .toByteArray().isEmpty());

    rawviewer::presentation::BayerExtractDialog reloaded;
    reloaded.setSource(&metadata);
    auto* reloadedPatterns =
        reloaded.findChild<QComboBox*>(QStringLiteral("patternCombo"));
    QVERIFY(reloadedPatterns);
    QCOMPARE(reloadedPatterns->count(), 5);
    reloadedPatterns->setCurrentIndex(3);
    const auto reloadedRequest = reloaded.request(makeUiImage(), {});
    QCOMPARE(reloadedRequest.mask.name, std::string("Three-cell test"));
    QCOMPARE(reloadedRequest.mask.selectedCount(), std::uint64_t{3});
    settings.remove(QStringLiteral("bayerExtract/customPatterns"));
}

void PixelStatisticsUiTest::showsRecentFilesAndDisablesMissingDocuments() {
    QTemporaryFile existing;
    QVERIFY(existing.open());
    auto store = std::make_shared<MemoryRecentDocumentStore>();
    rawviewer::application::RecentDocument available;
    available.path = std::filesystem::path(existing.fileName().toStdWString());
    available.rawDescriptor.width = 11776;
    available.rawDescriptor.height = 8842;
    available.rawDescriptor.headerBytes = 32;
    store->documents.push_back(available);
    rawviewer::application::RecentDocument missing = available;
    missing.path = std::filesystem::path(
        QString(existing.fileName() + ".deleted").toStdWString());
    store->documents.push_back(missing);

    rawviewer::presentation::MainWindow window({}, {}, store);
    auto* menu = window.findChild<QMenu*>(QStringLiteral("recentFilesMenu"));
    QVERIFY(menu);
    QCOMPARE(menu->title(), QStringLiteral("Recent Files(&R)"));
    QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow",
                                      Qt::DirectConnection));
    const auto actions = menu->actions();
    QCOMPARE(actions.size(), 4);
    QVERIFY(actions[0]->isEnabled());
    QVERIFY(actions[0]->toolTip().contains(QStringLiteral("11776 × 8842")));
    QVERIFY(!actions[1]->isEnabled());
    QVERIFY(actions[1]->text().contains(QStringLiteral("文件已删除")));
    QVERIFY(actions[2]->isSeparator());
    actions[3]->trigger();
    QVERIFY(store->cleared);
    QVERIFY(store->documents.empty());
}

void PixelStatisticsUiTest::configuresCompactProfessionalFilterDialog() {
    rawviewer::presentation::FilterDialog dialog;
    rawviewer::application::ImageMetadata metadata;
    metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    metadata.width = 12'000;
    metadata.height = 9'000;
    metadata.scalarType = rawviewer::domain::ScalarType::UInt16;
    metadata.format = "Synthetic RAW";
    dialog.setSource(&metadata);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* type = dialog.findChild<QComboBox*>(
        QStringLiteral("filterTypeCombo"));
    auto* kernel = dialog.findChild<QComboBox*>(
        QStringLiteral("filterKernelCombo"));
    auto* sigma = dialog.findChild<QDoubleSpinBox*>(
        QStringLiteral("filterSigmaSpin"));
    auto* apply = dialog.findChild<QPushButton*>(
        QStringLiteral("filterApplyButton"));
    QVERIFY(type);
    QVERIFY(kernel);
    QVERIFY(sigma);
    QVERIFY(apply);
    QCOMPARE(type->count(), 3);
    QCOMPARE(kernel->count(), 3);
    QCOMPARE(kernel->itemData(0).toInt(), 3);
    QCOMPARE(kernel->itemData(2).toInt(), 7);
    QVERIFY(sigma->isEnabled());
    QVERIFY(apply->isEnabled());
    type->setCurrentText(QStringLiteral("Median"));
    QVERIFY(!sigma->isEnabled());
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("filterParametersPane")));
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("filterExecutionPane")));
    QVERIFY(dialog.width() <= 420);
    QVERIFY(dialog.height() <= 360);

    const auto request = dialog.request(makeUiImage(), {});
    QCOMPARE(request.parameters.type,
             rawviewer::application::FilterType::Median);
    QCOMPARE(request.parameters.kernelSize, std::uint32_t{3});
}

void PixelStatisticsUiTest::filtersTheCurrentlyDisplayedBayerExtraction() {
    QTemporaryFile input;
    QVERIFY(input.open());
    QVERIFY(input.write("raw") == 3);
    const QString path = input.fileName();
    input.close();

    auto decoder = std::make_shared<StaticRawDecoder>();
    auto service = std::make_shared<rawviewer::application::OpenImageService>(
        std::vector<std::shared_ptr<const rawviewer::application::IImageDecoder>>{
            decoder});
    auto store = std::make_shared<MemoryRecentDocumentStore>();
    rawviewer::presentation::MainWindow window(service, {}, store);
    window.resize(1100, 720);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openPath(path);

    const auto findAction = [&window](const QString& text) {
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text() == text) {
                return action;
            }
        }
        return static_cast<QAction*>(nullptr);
    };
    auto* extractAction = findAction(QStringLiteral("Bayer Extract"));
    auto* filterAction = findAction(QStringLiteral("Filter"));
    auto* statisticsAction = findAction(QStringLiteral("Pixel Statistics"));
    auto* undoAction = window.findChild<QAction*>(QStringLiteral("undoAction"));
    QVERIFY(extractAction);
    QVERIFY(filterAction);
    QVERIFY(statisticsAction);
    QVERIFY(undoAction);
    QTRY_VERIFY_WITH_TIMEOUT(extractAction->isEnabled(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(filterAction->isEnabled(), 3000);

    extractAction->trigger();
    auto* extractDialog =
        window.findChild<rawviewer::presentation::BayerExtractDialog*>();
    QVERIFY(extractDialog);
    QPushButton* extractButton = nullptr;
    for (auto* button : extractDialog->findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Extract")) {
            extractButton = button;
        }
    }
    QVERIFY(extractButton);
    QTest::mouseClick(extractButton, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(extractButton->isEnabled(), 3000);

    filterAction->trigger();
    auto* filterDialog =
        window.findChild<rawviewer::presentation::FilterDialog*>();
    QVERIFY(filterDialog);
    QVERIFY(filterDialog->isVisible());
    auto* type = filterDialog->findChild<QComboBox*>(
        QStringLiteral("filterTypeCombo"));
    auto* apply = filterDialog->findChild<QPushButton*>(
        QStringLiteral("filterApplyButton"));
    QVERIFY(type);
    QVERIFY(apply);
    type->setCurrentText(QStringLiteral("Mean"));
    QTest::mouseClick(apply, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 3000);

    auto* viewport =
        window.findChild<rawviewer::presentation::ImageViewport*>();
    auto* coordinate = window.findChild<QLabel*>(
        QStringLiteral("coordinateLabel"));
    QVERIFY(viewport);
    QVERIFY(coordinate);
    viewport->imageCoordinateChanged(0, 0, true);
    QTRY_VERIFY_WITH_TIMEOUT(
        coordinate->text().contains(QStringLiteral("Raw 7.333")), 3000);

    statisticsAction->trigger();
    auto* statisticsDialog =
        window.findChild<rawviewer::presentation::PixelStatisticsDialog*>();
    QVERIFY(statisticsDialog);
    QVERIFY(statisticsDialog->isVisible());
    const QPoint firstPixel = imagePixelCenter(*viewport, 5, 5, 0, 0);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, firstPixel);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, firstPixel);
    const auto hasMetric = [statisticsDialog](const QString& value) {
        const auto labels = statisticsDialog->findChildren<QLabel*>();
        return std::any_of(
            labels.begin(), labels.end(),
            [&value](const QLabel* label) {
                return label->property("role") ==
                           QStringLiteral("metricValue") &&
                    label->text() == value;
            });
    };
    QTRY_VERIFY_WITH_TIMEOUT(hasMetric(QStringLiteral("7.3333")), 3000);

    QVERIFY(undoAction->isEnabled());
    undoAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(hasMetric(QStringLiteral("0.0000")), 3000);
    viewport->imageCoordinateChanged(0, 0, true);
    QTRY_VERIFY_WITH_TIMEOUT(
        coordinate->text().contains(QStringLiteral("Raw 0")), 3000);
}

void PixelStatisticsUiTest::configuresCompactProfessionalDemosaicDialog() {
    rawviewer::presentation::DemosaicDialog dialog;
    rawviewer::application::ImageMetadata metadata;
    metadata.kind = rawviewer::application::ImageKind::CameraRaw;
    metadata.width = 11'904;
    metadata.height = 8'842;
    metadata.scalarType = rawviewer::domain::ScalarType::UInt16;
    metadata.bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    metadata.format = "Camera RAW";
    dialog.setSource(&metadata);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* algorithms = dialog.findChild<QComboBox*>(
        QStringLiteral("demosaicAlgorithmCombo"));
    auto* description = dialog.findChild<QLabel*>(
        QStringLiteral("demosaicAlgorithmDescription"));
    auto* apply = dialog.findChild<QPushButton*>(
        QStringLiteral("demosaicApplyButton"));
    QVERIFY(algorithms);
    QVERIFY(description);
    QVERIFY(apply);
    QCOMPARE(algorithms->count(), 3);
    QCOMPARE(algorithms->currentText(), QStringLiteral("Malvar-He-Cutler"));
    QVERIFY(description->text().contains(QStringLiteral("RECOMMENDED")));
    QVERIFY(apply->isEnabled());
    algorithms->setCurrentText(QStringLiteral("Hamilton-Adams"));
    QVERIFY(description->text().contains(QStringLiteral("EDGE-AWARE")));
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("demosaicAlgorithmPane")));
    QVERIFY(dialog.findChild<QWidget*>(
        QStringLiteral("demosaicPipelinePane")));
    QVERIFY(dialog.width() <= 440);
    QVERIFY(dialog.height() <= 380);

    const auto request = dialog.request(
        makeUiImage(), {0.0, 255.0, 1.0}, {});
    QCOMPARE(request.algorithm,
             rawviewer::application::DemosaicAlgorithm::HamiltonAdams);
    metadata.bayerPattern = rawviewer::domain::BayerPattern::None;
    dialog.setSource(&metadata);
    QVERIFY(!apply->isEnabled());
}

void PixelStatisticsUiTest::demosaicsTheCurrentlyDisplayedFilteredRaw() {
    QTemporaryFile input;
    QVERIFY(input.open());
    QVERIFY(input.write("raw") == 3);
    const QString path = input.fileName();
    input.close();

    auto decoder = std::make_shared<StaticRawDecoder>();
    auto service = std::make_shared<rawviewer::application::OpenImageService>(
        std::vector<std::shared_ptr<const rawviewer::application::IImageDecoder>>{
            decoder});
    auto store = std::make_shared<MemoryRecentDocumentStore>();
    rawviewer::presentation::MainWindow window(service, {}, store);
    window.resize(1100, 720);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openPath(path);

    const auto findAction = [&window](const QString& text) {
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text() == text) {
                return action;
            }
        }
        return static_cast<QAction*>(nullptr);
    };
    auto* filterAction = findAction(QStringLiteral("Filter"));
    auto* demosaicAction = findAction(QStringLiteral("Demosaic"));
    QVERIFY(filterAction);
    QVERIFY(demosaicAction);
    QTRY_VERIFY_WITH_TIMEOUT(filterAction->isEnabled(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(demosaicAction->isEnabled(), 3000);

    filterAction->trigger();
    auto* filterDialog =
        window.findChild<rawviewer::presentation::FilterDialog*>();
    QVERIFY(filterDialog);
    auto* filterType = filterDialog->findChild<QComboBox*>(
        QStringLiteral("filterTypeCombo"));
    auto* filterApply = filterDialog->findChild<QPushButton*>(
        QStringLiteral("filterApplyButton"));
    QVERIFY(filterType);
    QVERIFY(filterApply);
    filterType->setCurrentText(QStringLiteral("Mean"));
    QTest::mouseClick(filterApply, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(filterApply->isEnabled(), 3000);

    demosaicAction->trigger();
    auto* demosaicDialog =
        window.findChild<rawviewer::presentation::DemosaicDialog*>();
    QVERIFY(demosaicDialog);
    auto* source = demosaicDialog->findChild<QLabel*>(
        QStringLiteral("demosaicSourceLabel"));
    auto* algorithms = demosaicDialog->findChild<QComboBox*>(
        QStringLiteral("demosaicAlgorithmCombo"));
    auto* apply = demosaicDialog->findChild<QPushButton*>(
        QStringLiteral("demosaicApplyButton"));
    QVERIFY(source);
    QVERIFY(algorithms);
    QVERIFY(apply);
    QVERIFY2(source->text().contains(QStringLiteral("Mean filter")),
             qPrintable(source->text()));
    algorithms->setCurrentText(QStringLiteral("Bilinear"));
    QTest::mouseClick(apply, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!demosaicAction->isEnabled(), 3000);

    auto* viewport =
        window.findChild<rawviewer::presentation::ImageViewport*>();
    auto* coordinate = window.findChild<QLabel*>(
        QStringLiteral("coordinateLabel"));
    QVERIFY(viewport);
    QVERIFY(coordinate);
    viewport->imageCoordinateChanged(0, 0, true);
    QTRY_VERIFY2_WITH_TIMEOUT(
        coordinate->text().contains(QStringLiteral("RGB 4,7,11")),
        qPrintable(coordinate->text()), 3000);
    auto* restore = demosaicDialog->findChild<QPushButton*>(
        QStringLiteral("demosaicRestoreButton"));
    QVERIFY(restore);
    QVERIFY(restore->isEnabled());
    QTest::mouseClick(restore, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(demosaicAction->isEnabled(), 3000);
}

QTEST_MAIN(PixelStatisticsUiTest)
#include "pixel_statistics_ui_test.moc"
