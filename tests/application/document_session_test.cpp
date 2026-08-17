#include "application/bayer_extract.h"
#include "application/document_session.h"
#include "application/pixel_info.h"
#include "application/pixel_statistics.h"
#include "application/preview_renderer.h"

#include <QTest>

#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

namespace {

class TestPixelSource final : public rawviewer::application::IPixelSource {
public:
    std::uint64_t width() const noexcept override { return 3; }
    std::uint64_t height() const noexcept override { return 1; }

    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (y != 0 || x >= width()) {
            return {};
        }
        return {true, static_cast<double>(x * 50)};
    }
};

class GridPixelSource final : public rawviewer::application::IPixelSource {
public:
    GridPixelSource(std::uint64_t width, std::uint64_t height)
        : width_(width), height_(height) {}

    std::uint64_t width() const noexcept override { return width_; }
    std::uint64_t height() const noexcept override { return height_; }

    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (x >= width_ || y >= height_) {
            return {};
        }
        return {true, static_cast<double>(y * 10 + x)};
    }

private:
    std::uint64_t width_ = 0;
    std::uint64_t height_ = 0;
};

std::shared_ptr<rawviewer::application::DecodedImage> makeImage() {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.width = 3;
    image->metadata.height = 1;
    image->metadata.scalarType = rawviewer::domain::ScalarType::UInt16;
    image->metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    image->metadata.sensorBlackLevel = 0.0;
    image->metadata.whiteLevel = 100.0;
    image->preview.width = 3;
    image->preview.height = 1;
    image->preview.rgba = {
        0, 0, 0, 255,
        128, 128, 128, 255,
        255, 255, 255, 255
    };
    auto signal =
        std::make_shared<rawviewer::application::SignalPreview>();
    signal->width = 3;
    signal->height = 1;
    signal->values = {0.0F, 50.0F, 100.0F};
    image->signalPreview = signal;
    image->pixels = std::make_shared<TestPixelSource>();
    return image;
}

std::shared_ptr<rawviewer::application::DecodedImage> makeBayerImage(
    std::uint64_t width,
    std::uint64_t height,
    rawviewer::domain::BayerPattern pattern =
        rawviewer::domain::BayerPattern::RGGB) {
    auto image = std::make_shared<rawviewer::application::DecodedImage>();
    image->metadata.width = width;
    image->metadata.height = height;
    image->metadata.scalarType = rawviewer::domain::ScalarType::UInt16;
    image->metadata.kind = rawviewer::application::ImageKind::FlatRaw;
    image->metadata.bayerPattern = pattern;
    image->metadata.sensorBlackLevel = 0.0;
    image->metadata.whiteLevel = 65535.0;
    image->metadata.format = "Synthetic RAW";
    image->pixels = std::make_shared<GridPixelSource>(width, height);
    return image;
}

void commitBlackPoint(rawviewer::application::DocumentSession& session,
                      double blackPoint) {
    auto mapping = session.displayMapping();
    mapping.blackPoint = blackPoint;
    session.beginDisplayEdit();
    QVERIFY(session.updateDisplayMapping(mapping).valid);
    QVERIFY(session.commitDisplayEdit());
}

} // namespace

class DocumentSessionTest final : public QObject {
    Q_OBJECT

private slots:
    void keepsOnlyFiveUndoOperations();
    void clearsRedoAfterNewEdit();
    void isolatesDocumentHistory();
    void rendersWithoutChangingOriginal();
    void preservesDirectGray16WithoutRgbRendering();
    void queriesOriginalProcessedRgbAndBayerValues();
    void rejectsOutOfBoundsPixelInfo();
    void extractsChannelsFromOddSizedImage();
    void packsArbitraryMaskByRowsAndColumns();
    void extractsStandardQuadHexAndSpecialPatternPositions();
    void selectingEveryCellIsExactZeroCopyIdentity();
    void extractsUnalignedRoiAndConvertsCoordinates();
    void rejectsInvalidAndEmptyBayerRegions();
    void cancelsBayerExtraction();
    void calculatesStatusAndProfilesFromOriginalBayerSamples();
    void filtersStatisticsByBayerChannelAndCancels();
};

void DocumentSessionTest::keepsOnlyFiveUndoOperations() {
    rawviewer::application::DocumentSession session(makeImage());
    for (int value = 1; value <= 6; ++value) {
        commitBlackPoint(session, value);
    }
    QCOMPARE(session.undoCount(), std::size_t{5});
    QCOMPARE(session.displayMapping().blackPoint, 6.0);

    for (int count = 0; count < 5; ++count) {
        QVERIFY(session.undo());
    }
    QCOMPARE(session.displayMapping().blackPoint, 1.0);
    QVERIFY(!session.canUndo());
    QCOMPARE(session.redoCount(), std::size_t{5});
}

void DocumentSessionTest::clearsRedoAfterNewEdit() {
    rawviewer::application::DocumentSession session(makeImage());
    commitBlackPoint(session, 1.0);
    commitBlackPoint(session, 2.0);
    QVERIFY(session.undo());
    QVERIFY(session.canRedo());
    commitBlackPoint(session, 3.0);
    QVERIFY(!session.canRedo());
}

void DocumentSessionTest::isolatesDocumentHistory() {
    rawviewer::application::DocumentSession first(makeImage());
    rawviewer::application::DocumentSession second(makeImage());
    commitBlackPoint(first, 10.0);
    QVERIFY(first.canUndo());
    QVERIFY(!second.canUndo());
    QCOMPARE(second.displayMapping().blackPoint, 0.0);
}

void DocumentSessionTest::rendersWithoutChangingOriginal() {
    const auto original = makeImage();
    rawviewer::domain::DisplayMapping mapping;
    mapping.blackPoint = 0.0;
    mapping.whitePoint = 100.0;
    mapping.gamma = 1.0;

    const auto rendered = rawviewer::application::PreviewRenderer::render(
        original, mapping);
    QVERIFY(rendered);
    QCOMPARE(rendered->preview.rgba[0], std::uint8_t{0});
    QCOMPARE(rendered->preview.rgba[4], std::uint8_t{128});
    QCOMPARE(rendered->preview.rgba[8], std::uint8_t{255});
    QCOMPARE(original->preview.rgba[4], std::uint8_t{128});
    QCOMPARE(rendered->signalPreview.get(), original->signalPreview.get());
}

void DocumentSessionTest::preservesDirectGray16WithoutRgbRendering() {
    const auto original = makeBayerImage(2, 2);
    auto grayscale = std::make_shared<std::vector<std::uint16_t>>(
        std::initializer_list<std::uint16_t>{25, 50, 75, 100});
    original->preview.width = 2;
    original->preview.height = 2;
    original->preview.grayscale16Storage = grayscale;
    original->preview.grayscale16Pixels = grayscale->data();
    original->preview.grayscale16StrideSamples = 2;

    rawviewer::domain::DisplayMapping mapping;
    mapping.blackPoint = 20.0;
    mapping.whitePoint = 80.0;
    mapping.gamma = 2.2;
    const auto rendered = rawviewer::application::PreviewRenderer::render(
        original, mapping);

    QVERIFY(rendered);
    QCOMPARE(rendered->preview.grayscale16Storage.get(), grayscale.get());
    QCOMPARE(rendered->preview.grayscale16Pixels, grayscale->data());
    QVERIFY(rendered->preview.rgba.empty());
    QCOMPARE(rendered->preview.grayscale16Pixels[0], std::uint16_t{25});
    QCOMPARE(rendered->preview.grayscale16Pixels[3], std::uint16_t{100});
    QCOMPARE(original->pixels->sample(1, 1).value, 11.0);
}

void DocumentSessionTest::queriesOriginalProcessedRgbAndBayerValues() {
    const auto image = makeImage();
    image->metadata.bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    rawviewer::domain::DisplayMapping mapping;
    mapping.blackPoint = 0.0;
    mapping.whitePoint = 100.0;
    mapping.gamma = 1.0;

    const auto info = rawviewer::application::queryPixelInfo(
        *image, mapping, 1, 0);
    QVERIFY(info.valid);
    QCOMPARE(info.originalValue, 50.0);
    QCOMPARE(info.processedValue, 0.5);
    QVERIFY(!info.rgbValid);
    QCOMPARE(info.channel, rawviewer::domain::BayerChannel::Gr);
}

void DocumentSessionTest::rejectsOutOfBoundsPixelInfo() {
    const auto image = makeImage();
    const auto info = rawviewer::application::queryPixelInfo(
        *image, {}, 3, 0);
    QVERIFY(!info.valid);
}

void DocumentSessionTest::extractsChannelsFromOddSizedImage() {
    rawviewer::application::BayerExtractService service;
    rawviewer::application::BayerExtractRequest request;
    request.source = makeBayerImage(5, 3);
    request.mask = {"R", 2, 2, {1, 0, 0, 0}};
    const auto red = service.execute(request);
    QVERIFY2(red.succeeded(), red.message.c_str());
    QCOMPARE(red.extraction->geometry.width, std::uint64_t{3});
    QCOMPARE(red.extraction->geometry.height, std::uint64_t{2});
    QVERIFY(red.extraction->geometry.hasPartialEdgeUnits());
    QCOMPARE(red.extraction->image->pixels->sample(2, 1).value, 24.0);
    QCOMPARE(request.source->pixels->sample(4, 2).value, 24.0);

    request.mask = {"B", 2, 2, {0, 0, 0, 1}};
    const auto blue = service.execute(request);
    QVERIFY2(blue.succeeded(), blue.message.c_str());
    QCOMPARE(blue.extraction->geometry.width, std::uint64_t{3});
    QCOMPARE(blue.extraction->geometry.height, std::uint64_t{2});
    QCOMPARE(blue.extraction->image->pixels->sample(1, 0).value, 13.0);
    QVERIFY(!blue.extraction->image->pixels->sample(2, 1).valid);
}

void DocumentSessionTest::packsArbitraryMaskByRowsAndColumns() {
    rawviewer::application::BayerExtractRequest request;
    request.source = makeBayerImage(4, 4);
    request.mask = {"three cells", 2, 2, {0, 1, 1, 1}};

    const auto rowMajor =
        rawviewer::application::BayerExtractService().execute(request);
    QVERIFY2(rowMajor.succeeded(), rowMajor.message.c_str());
    QCOMPARE(rowMajor.extraction->geometry.outputUnitWidth, std::uint64_t{2});
    QCOMPARE(rowMajor.extraction->geometry.outputUnitHeight, std::uint64_t{2});
    QCOMPARE(rowMajor.extraction->image->pixels->sample(0, 0).value, 1.0);
    QCOMPARE(rowMajor.extraction->image->pixels->sample(1, 0).value, 10.0);
    QCOMPARE(rowMajor.extraction->image->pixels->sample(0, 1).value, 11.0);
    QVERIFY(!rowMajor.extraction->image->pixels->sample(1, 1).valid);
    const std::optional<rawviewer::domain::BayerCoordinate> source11 =
        rawviewer::domain::BayerCoordinate{1, 1};
    QCOMPARE(rowMajor.extraction->geometry.sourceCoordinate(0, 1), source11);

    request.packingOrder = rawviewer::application::BayerPackingOrder::ColumnMajor;
    const auto columnMajor =
        rawviewer::application::BayerExtractService().execute(request);
    QVERIFY2(columnMajor.succeeded(), columnMajor.message.c_str());
    QCOMPARE(columnMajor.extraction->image->pixels->sample(0, 0).value, 10.0);
    QCOMPARE(columnMajor.extraction->image->pixels->sample(0, 1).value, 1.0);
    QCOMPARE(columnMajor.extraction->image->pixels->sample(1, 0).value, 11.0);
    QVERIFY(!columnMajor.extraction->image->pixels->sample(1, 1).valid);
    const std::optional<rawviewer::domain::BayerCoordinate> output10 =
        rawviewer::domain::BayerCoordinate{1, 0};
    QCOMPARE(columnMajor.extraction->geometry.outputCoordinate(1, 1), output10);
}

void DocumentSessionTest::extractsStandardQuadHexAndSpecialPatternPositions() {
    rawviewer::application::BayerExtractService service;

    rawviewer::application::BayerExtractRequest standard;
    standard.source = makeBayerImage(6, 4);
    const std::vector<std::vector<std::uint8_t>> corners{
        {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    const std::vector<double> firstValues{0.0, 1.0, 10.0, 11.0};
    for (std::size_t index = 0; index < corners.size(); ++index) {
        standard.mask = {"standard", 2, 2, corners[index]};
        const auto result = service.execute(standard);
        QVERIFY2(result.succeeded(), result.message.c_str());
        QCOMPARE(result.extraction->geometry.width, std::uint64_t{3});
        QCOMPARE(result.extraction->geometry.height, std::uint64_t{2});
        QCOMPARE(result.extraction->image->pixels->sample(0, 0).value,
                 firstValues[index]);
        QCOMPARE(result.extraction->image->pixels->sample(2, 1).value,
                 firstValues[index] + 24.0);
    }

    rawviewer::application::BayerExtractRequest quad;
    quad.source = makeBayerImage(8, 8);
    quad.mask = {"quad-position", 4, 4, std::vector<std::uint8_t>(16, 0)};
    quad.mask.selected[2 * 4 + 1] = 1; // source offset (1, 2)
    const auto quadResult = service.execute(quad);
    QVERIFY2(quadResult.succeeded(), quadResult.message.c_str());
    QCOMPARE(quadResult.extraction->geometry.width, std::uint64_t{2});
    QCOMPARE(quadResult.extraction->geometry.height, std::uint64_t{2});
    QCOMPARE(quadResult.extraction->image->pixels->sample(0, 0).value, 21.0);
    QCOMPARE(quadResult.extraction->image->pixels->sample(1, 1).value, 65.0);

    rawviewer::application::BayerExtractRequest hex;
    hex.source = makeBayerImage(16, 16);
    hex.mask = {"hex-position", 8, 8, std::vector<std::uint8_t>(64, 0)};
    hex.mask.selected[7 * 8 + 7] = 1; // source offset (7, 7)
    const auto hexResult = service.execute(hex);
    QVERIFY2(hexResult.succeeded(), hexResult.message.c_str());
    QCOMPARE(hexResult.extraction->geometry.width, std::uint64_t{2});
    QCOMPARE(hexResult.extraction->geometry.height, std::uint64_t{2});
    QCOMPARE(hexResult.extraction->image->pixels->sample(0, 0).value, 77.0);
    QCOMPARE(hexResult.extraction->image->pixels->sample(1, 1).value, 165.0);

    auto specialSource = makeBayerImage(6, 4);
    specialSource->metadata.bayerPattern = rawviewer::domain::BayerPattern::None;
    rawviewer::application::BayerExtractRequest special;
    special.source = specialSource;
    special.mask = {"special", 3, 2, {0, 0, 1, 0, 0, 0}};
    const auto specialResult = service.execute(special);
    QVERIFY2(specialResult.succeeded(), specialResult.message.c_str());
    QCOMPARE(specialResult.extraction->geometry.width, std::uint64_t{2});
    QCOMPARE(specialResult.extraction->geometry.height, std::uint64_t{2});
    QCOMPARE(specialResult.extraction->image->pixels->sample(0, 0).value, 2.0);
    QCOMPARE(specialResult.extraction->image->pixels->sample(1, 1).value, 25.0);
}

void DocumentSessionTest::selectingEveryCellIsExactZeroCopyIdentity() {
    auto source = makeBayerImage(5, 3);
    auto grayscale = std::make_shared<std::vector<std::uint16_t>>(15);
    std::iota(grayscale->begin(), grayscale->end(), std::uint16_t{0});
    source->preview.width = 5;
    source->preview.height = 3;
    source->preview.grayscale16Storage = grayscale;
    source->preview.grayscale16Pixels = grayscale->data();
    source->preview.grayscale16StrideSamples = 5;

    for (const std::uint32_t size : {2U, 4U, 8U}) {
        rawviewer::application::BayerExtractRequest request;
        request.source = source;
        request.mask = {"all", size, size,
            std::vector<std::uint8_t>(static_cast<std::size_t>(size) * size, 1)};
        const auto result =
            rawviewer::application::BayerExtractService().execute(request);
        QVERIFY2(result.succeeded(), result.message.c_str());
        QCOMPARE(result.extraction->geometry.width, std::uint64_t{5});
        QCOMPARE(result.extraction->geometry.height, std::uint64_t{3});
        QCOMPARE(result.extraction->image->pixels.get(), source->pixels.get());
        QCOMPARE(result.extraction->image->preview.grayscale16Pixels,
                 source->preview.grayscale16Pixels);
        QCOMPARE(result.extraction->image->metadata.bayerPattern,
                 source->metadata.bayerPattern);
        const std::optional<rawviewer::domain::BayerCoordinate> source42 =
            rawviewer::domain::BayerCoordinate{4, 2};
        QCOMPARE(result.extraction->geometry.sourceCoordinate(4, 2), source42);
        QCOMPARE(result.extraction->image->pixels->sample(4, 2).value, 24.0);
    }

    rawviewer::application::BayerExtractRequest channel;
    channel.source = makeBayerImage(4096, 3072);
    channel.mask = {"channel", 2, 2, {1, 0, 0, 0}};
    const auto result =
        rawviewer::application::BayerExtractService().execute(channel);
    QVERIFY2(result.succeeded(), result.message.c_str());
    QVERIFY(result.extraction->image->signalPreview);
    QCOMPARE(result.extraction->image->signalPreview->width, 1024);
    QCOMPARE(result.extraction->image->signalPreview->height, 768);
    QVERIFY(result.extraction->image->preview.rgba.empty());
}

void DocumentSessionTest::extractsUnalignedRoiAndConvertsCoordinates() {
    rawviewer::application::BayerExtractRequest request;
    request.source = makeBayerImage(5, 5);
    request.mask = {"Gr", 2, 2, {1, 0, 0, 0}};
    request.sourceRegion = rawviewer::application::PixelRegion{1, 1, 4, 4};
    const auto result =
        rawviewer::application::BayerExtractService().execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    const auto& geometry = result.extraction->geometry;
    QCOMPARE(geometry.width, std::uint64_t{2});
    QCOMPARE(geometry.height, std::uint64_t{2});
    const std::optional<rawviewer::domain::BayerCoordinate> source34 =
        rawviewer::domain::BayerCoordinate{3, 3};
    const std::optional<rawviewer::domain::BayerCoordinate> channel11 =
        rawviewer::domain::BayerCoordinate{1, 1};
    QCOMPARE(geometry.sourceCoordinate(1, 1), source34);
    QCOMPARE(geometry.outputCoordinate(3, 3), channel11);
    QVERIFY(!geometry.outputCoordinate(2, 3));
    QVERIFY(!geometry.sourceCoordinate(2, 1));
    QCOMPARE(result.extraction->image->pixels->sample(1, 1).value, 33.0);
}

void DocumentSessionTest::rejectsInvalidAndEmptyBayerRegions() {
    rawviewer::application::BayerExtractRequest request;
    request.source = makeBayerImage(5, 5);
    request.sourceRegion = rawviewer::application::PixelRegion{4, 4, 2, 1};
    auto result = rawviewer::application::BayerExtractService().execute(request);
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("bayer.invalid_region"));

    request.mask.selected = {0, 0, 0, 0};
    request.sourceRegion = rawviewer::application::PixelRegion{0, 0, 1, 1};
    result = rawviewer::application::BayerExtractService().execute(request);
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("bayer.invalid_mask"));
}

void DocumentSessionTest::cancelsBayerExtraction() {
    rawviewer::application::BayerExtractRequest request;
    request.source = makeBayerImage(5, 5);
    request.cancellation = std::make_shared<std::atomic_bool>(true);
    const auto result =
        rawviewer::application::BayerExtractService().execute(request);
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("task.cancelled"));
}

void DocumentSessionTest::calculatesStatusAndProfilesFromOriginalBayerSamples() {
    rawviewer::application::PixelStatisticsService service;
    rawviewer::application::PixelStatisticsRequest request;
    request.source = makeBayerImage(4, 3);
    request.selection = {1, 0, 3, 1};
    request.histogramBins = 256;

    request.mode = rawviewer::application::PixelStatisticsMode::Status;
    auto result = service.execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.summary.count, std::uint64_t{6});
    QCOMPARE(result.summary.minimum, 1.0);
    QCOMPARE(result.summary.maximum, 13.0);
    QCOMPARE(result.summary.mean, 7.0);
    QVERIFY(std::abs(result.summary.standardDeviation -
                     std::sqrt(154.0 / 6.0)) < 1.0e-12);
    QCOMPARE(result.plot.x.size(), std::size_t{256});
    QCOMPARE(std::accumulate(result.plot.y.begin(),
                             result.plot.y.end(),
                             0.0),
             6.0);

    request.mode =
        rawviewer::application::PixelStatisticsMode::HorizontalBox;
    result = service.execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.plot.y, std::vector<double>({6.0, 7.0, 8.0}));
    QCOMPARE(result.summary.count, std::uint64_t{3});
    QCOMPARE(result.summary.mean, 7.0);

    request.mode = rawviewer::application::PixelStatisticsMode::VerticalBox;
    result = service.execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.plot.y, std::vector<double>({2.0, 12.0}));
    QCOMPARE(result.summary.count, std::uint64_t{2});
    QCOMPARE(result.summary.standardDeviation, 5.0);

    request.mode = rawviewer::application::PixelStatisticsMode::Line;
    request.selection = {0, 0, 3, 2};
    result = service.execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.plot.y, std::vector<double>({0.0, 11.0, 12.0, 23.0}));
    QCOMPARE(result.summary.count, std::uint64_t{4});
    QCOMPARE(result.summary.mean, 11.5);
}

void DocumentSessionTest::filtersStatisticsByBayerChannelAndCancels() {
    rawviewer::application::PixelStatisticsService service;
    rawviewer::application::PixelStatisticsRequest request;
    request.source = makeBayerImage(4, 2);
    request.selection = {0, 0, 3, 1};
    request.mode = rawviewer::application::PixelStatisticsMode::Status;
    request.channel = rawviewer::domain::BayerChannel::R;
    request.histogramBins = 64;
    auto result = service.execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.summary.count, std::uint64_t{2});
    QCOMPARE(result.summary.minimum, 0.0);
    QCOMPARE(result.summary.maximum, 2.0);
    QCOMPARE(result.summary.mean, 1.0);

    request.mode =
        rawviewer::application::PixelStatisticsMode::HorizontalBox;
    result = service.execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.plot.y, std::vector<double>({0.0, 2.0}));
    QCOMPARE(result.summary.count, std::uint64_t{2});
    QCOMPARE(result.summary.mean, 1.0);

    request.cancellation = std::make_shared<std::atomic_bool>(true);
    result = service.execute(request);
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("task.cancelled"));
}

QTEST_APPLESS_MAIN(DocumentSessionTest)
#include "document_session_test.moc"
