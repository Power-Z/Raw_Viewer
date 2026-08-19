#include "application/bayer_extract.h"
#include "application/demosaic.h"
#include "application/document_session.h"
#include "application/filter.h"
#include "application/global_histogram.h"
#include "application/image_transform.h"
#include "application/pixel_info.h"
#include "application/pixel_statistics.h"
#include "application/preview_renderer.h"

#include <QTest>

#include <array>
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
        ++sampleCalls;
        return {true, static_cast<double>(y * 10 + x)};
    }

    mutable std::uint64_t sampleCalls = 0;

private:
    std::uint64_t width_ = 0;
    std::uint64_t height_ = 0;
};

class VectorPixelSource final : public rawviewer::application::IPixelSource {
public:
    VectorPixelSource(std::uint64_t width,
                      std::uint64_t height,
                      std::vector<double> values)
        : width_(width), height_(height), values_(std::move(values)) {}

    std::uint64_t width() const noexcept override { return width_; }
    std::uint64_t height() const noexcept override { return height_; }

    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (x >= width_ || y >= height_) {
            return {};
        }
        return {true, values_[static_cast<std::size_t>(y * width_ + x)]};
    }

private:
    std::uint64_t width_ = 0;
    std::uint64_t height_ = 0;
    std::vector<double> values_;
};

class RgbPixelSource final : public rawviewer::application::IPixelSource {
public:
    std::uint64_t width() const noexcept override { return 2; }
    std::uint64_t height() const noexcept override { return 1; }

    rawviewer::application::PixelSample sample(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        if (y != 0 || x >= 2) return {};
        return x == 0
            ? rawviewer::application::PixelSample{
                  true, 85.0, true, 255, 0, 0}
            : rawviewer::application::PixelSample{
                  true, 85.0, true, 0, 255, 0};
    }
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

std::shared_ptr<rawviewer::application::DecodedImage> makeConstantColorBayer(
    std::uint64_t width,
    std::uint64_t height,
    rawviewer::domain::BayerPattern pattern) {
    auto image = makeBayerImage(width, height, pattern);
    std::vector<double> values(static_cast<std::size_t>(width * height));
    for (std::uint64_t y = 0; y < height; ++y) {
        for (std::uint64_t x = 0; x < width; ++x) {
            const auto channel = rawviewer::domain::bayerChannelAt(
                pattern, x, y);
            values[static_cast<std::size_t>(y * width + x)] =
                channel == rawviewer::domain::BayerChannel::R ? 100.0 :
                channel == rawviewer::domain::BayerChannel::B ? 10.0 : 50.0;
        }
    }
    image->metadata.whiteLevel = 100.0;
    image->pixels = std::make_shared<VectorPixelSource>(
        width, height, std::move(values));
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
    void undoesAnActiveDisplayEditImmediately();
    void serializesPipelineCompletionAfterActiveDisplayEdit();
    void defaultsRawDisplayToLinearGamma();
    void isolatesDocumentHistory();
    void restoresPipelineAndDisplayStateFromGlobalHistory();
    void transformsPixelsBayerPhaseAndBoundedPreview();
    void calculatesThreeGlobalHistogramModesAndCancellation();
    void rendersWithoutChangingOriginal();
    void preservesDirectGray16WithoutRgbRendering();
    void queriesOriginalProcessedRgbAndBayerValues();
    void rejectsOutOfBoundsPixelInfo();
    void keepsPixelInfoOnLatestIndependentExtraction();
    void extractsSinglePositionToDenseOutputCoordinates();
    void extractsChannelsFromOddSizedImage();
    void packsArbitraryMaskByRowsAndColumns();
    void extractsStandardQuadHexAndSpecialPatternPositions();
    void selectingEveryCellIsExactZeroCopyIdentity();
    void extractsUnalignedRoiAndConvertsCoordinates();
    void rejectsInvalidAndEmptyBayerRegions();
    void cancelsBayerExtraction();
    void calculatesStatusAndProfilesFromOriginalBayerSamples();
    void filtersStatisticsByBayerChannelAndCancels();
    void calculatesStatisticsFromExtractedDisplayPipeline();
    void calculatesFourChannelsInSinglePass();
    void filtersCurrentRawWithGoldenValues();
    void chainsFiltersWithoutMutatingTheSource();
    void reusesBoundedFilterTiles();
    void rejectsInvalidAndCancelledFilters();
    void demosaicsAllBayerPatternsWithThreeAlgorithms();
    void keepsDemosaicPreviewAndExactTilesBounded();
    void rejectsUnsupportedAndCancelledDemosaic();
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

void DocumentSessionTest::undoesAnActiveDisplayEditImmediately() {
    rawviewer::application::DocumentSession session(makeImage());
    auto mapping = session.displayMapping();
    mapping.blackPoint = 7.0;
    QVERIFY(session.updateDisplayMapping(mapping).valid);
    QVERIFY(session.canUndo());
    QVERIFY(!session.canRedo());
    QVERIFY(session.undo());
    QCOMPARE(session.displayMapping().blackPoint, 0.0);
    QVERIFY(session.canRedo());
    QVERIFY(session.redo());
    QCOMPARE(session.displayMapping().blackPoint, 7.0);
    QCOMPARE(session.undoCount(), std::size_t{1});
}

void DocumentSessionTest::serializesPipelineCompletionAfterActiveDisplayEdit() {
    const auto original = makeImage();
    const auto processed = makeImage();
    rawviewer::application::DocumentSession session(original);
    auto mapping = session.displayMapping();
    mapping.blackPoint = 7.0;
    QVERIFY(session.updateDisplayMapping(mapping).valid);

    QVERIFY(session.commitPipelineEdit(processed));
    QCOMPARE(session.undoCount(), std::size_t{2});
    QCOMPARE(session.displaySource().get(), processed.get());
    QCOMPARE(session.displayMapping().blackPoint, 7.0);

    QVERIFY(session.undo());
    QCOMPARE(session.displaySource().get(), original.get());
    QCOMPARE(session.displayMapping().blackPoint, 7.0);
    QVERIFY(session.undo());
    QCOMPARE(session.displayMapping().blackPoint, 0.0);
    QVERIFY(session.redo());
    QVERIFY(session.redo());
    QCOMPARE(session.displaySource().get(), processed.get());
}

void DocumentSessionTest::defaultsRawDisplayToLinearGamma() {
    rawviewer::application::DocumentSession session(makeBayerImage(2, 2));
    QCOMPARE(session.displayMapping().gamma, 1.0);
    rawviewer::domain::DisplayMapping mapping;
    mapping.blackPoint = 10.0;
    mapping.whitePoint = 110.0;
    mapping.gamma = 1.0;
    QCOMPARE(rawviewer::domain::mapDisplayValue(0.0, mapping), 0.0);
    QCOMPARE(rawviewer::domain::mapDisplayValue(10.0, mapping), 0.0);
    QCOMPARE(rawviewer::domain::mapDisplayValue(60.0, mapping), 0.5);
    QCOMPARE(rawviewer::domain::mapDisplayValue(110.0, mapping), 1.0);
    QCOMPARE(rawviewer::domain::mapDisplayValue(200.0, mapping), 1.0);
}

void DocumentSessionTest::isolatesDocumentHistory() {
    rawviewer::application::DocumentSession first(makeImage());
    rawviewer::application::DocumentSession second(makeImage());
    commitBlackPoint(first, 10.0);
    QVERIFY(first.canUndo());
    QVERIFY(!second.canUndo());
    QCOMPARE(second.displayMapping().blackPoint, 0.0);
}

void DocumentSessionTest::restoresPipelineAndDisplayStateFromGlobalHistory() {
    const auto original = makeBayerImage(4, 3);
    rawviewer::application::DocumentSession session(original);
    rawviewer::application::ImageTransformService transformService;

    auto mirror = transformService.execute(
        {original,
         rawviewer::application::ImageTransform::MirrorHorizontal,
         {}});
    QVERIFY2(mirror.succeeded(), mirror.message.c_str());
    auto extraction = std::make_shared<
        rawviewer::application::BayerExtraction>();
    extraction->image = mirror.image;
    QVERIFY(session.commitPipelineEdit(mirror.image, {}, extraction));

    auto rotate = transformService.execute(
        {mirror.image,
         rawviewer::application::ImageTransform::RotateRight,
         {}});
    QVERIFY2(rotate.succeeded(), rotate.message.c_str());
    QVERIFY(session.commitPipelineEdit(
        rotate.image, mirror.image, extraction));
    commitBlackPoint(session, 5.0);
    QCOMPARE(session.undoCount(), std::size_t{3});
    QCOMPARE(session.displaySource().get(), rotate.image.get());
    QCOMPARE(session.preDemosaicSource().get(), mirror.image.get());
    QCOMPARE(session.bayerExtraction().get(), extraction.get());

    QVERIFY(session.undo());
    QCOMPARE(session.displayMapping().blackPoint, 0.0);
    QCOMPARE(session.displaySource().get(), rotate.image.get());
    QVERIFY(session.undo());
    QCOMPARE(session.displaySource().get(), mirror.image.get());
    QVERIFY(!session.preDemosaicSource());
    QCOMPARE(session.bayerExtraction().get(), extraction.get());
    QVERIFY(session.undo());
    QCOMPARE(session.displaySource().get(), original.get());
    QVERIFY(!session.bayerExtraction());

    QVERIFY(session.redo());
    QCOMPARE(session.displaySource().get(), mirror.image.get());
    commitBlackPoint(session, 2.0);
    QVERIFY(!session.canRedo());
}

void DocumentSessionTest::transformsPixelsBayerPhaseAndBoundedPreview() {
    using rawviewer::application::ImageTransform;
    rawviewer::application::ImageTransformService service;
    const auto source = makeBayerImage(3, 2);

    struct Golden {
        ImageTransform transform;
        std::uint64_t width;
        std::uint64_t height;
        std::vector<double> values;
    };
    const std::array cases{
        Golden{ImageTransform::FlipVertical, 3, 2,
               {10, 11, 12, 0, 1, 2}},
        Golden{ImageTransform::MirrorHorizontal, 3, 2,
               {2, 1, 0, 12, 11, 10}},
        Golden{ImageTransform::RotateLeft, 2, 3,
               {2, 12, 1, 11, 0, 10}},
        Golden{ImageTransform::RotateRight, 2, 3,
               {10, 0, 11, 1, 12, 2}},
        Golden{ImageTransform::Rotate180, 3, 2,
               {12, 11, 10, 2, 1, 0}}};
    for (const auto& golden : cases) {
        const auto result = service.execute({source, golden.transform, {}});
        QVERIFY2(result.succeeded(), result.message.c_str());
        QCOMPARE(result.image->metadata.width, golden.width);
        QCOMPARE(result.image->metadata.height, golden.height);
        QCOMPARE(result.image->pixels->width(), golden.width);
        QCOMPARE(result.image->pixels->height(), golden.height);
        for (std::uint64_t y = 0; y < golden.height; ++y) {
            for (std::uint64_t x = 0; x < golden.width; ++x) {
                const auto index = static_cast<std::size_t>(
                    y * golden.width + x);
                QCOMPARE(result.image->pixels->sample(x, y).value,
                         golden.values[index]);
                const auto actual =
                    result.image->pixels->bayerChannel(x, y);
                const auto expected = rawviewer::domain::bayerChannelAt(
                    result.image->metadata.bayerPattern, x, y);
                const auto green = [](rawviewer::domain::BayerChannel channel) {
                    return channel == rawviewer::domain::BayerChannel::Gr ||
                        channel == rawviewer::domain::BayerChannel::Gb;
                };
                QVERIFY(actual == expected ||
                        (green(actual) && green(expected)));
            }
        }
    }

    const auto first = service.execute(
        {source, ImageTransform::MirrorHorizontal, {}});
    const auto second = service.execute(
        {first.image, ImageTransform::MirrorHorizontal, {}});
    QVERIFY(first.succeeded());
    QVERIFY(second.succeeded());
    for (std::uint64_t y = 0; y < 2; ++y) {
        for (std::uint64_t x = 0; x < 3; ++x) {
            QCOMPARE(second.image->pixels->sample(x, y).value,
                     source->pixels->sample(x, y).value);
        }
    }

    auto large = makeBayerImage(4096, 3072);
    const auto bounded = service.execute(
        {large, ImageTransform::RotateRight, {}});
    QVERIFY2(bounded.succeeded(), bounded.message.c_str());
    QVERIFY(bounded.image->signalPreview);
    QCOMPARE(bounded.image->signalPreview->width, 768);
    QCOMPARE(bounded.image->signalPreview->height, 1024);
    QVERIFY(!bounded.image->signalPreview->preservesBayerPhase);
    QCOMPARE(bounded.image->signalPreview->values.size(),
             std::size_t{768 * 1024});

    auto cancellation = std::make_shared<std::atomic_bool>(true);
    const auto cancelled = service.execute(
        {source, ImageTransform::Rotate180, cancellation});
    QVERIFY(!cancelled.succeeded());
    QCOMPARE(QString::fromStdString(cancelled.errorCode),
             QStringLiteral("task.cancelled"));
}

void DocumentSessionTest::calculatesThreeGlobalHistogramModesAndCancellation() {
    using rawviewer::application::GlobalHistogramMode;
    rawviewer::application::GlobalHistogramService service;

    auto bayer = service.execute({makeBayerImage(2, 2), 64, {}, {}});
    QVERIFY2(bayer.succeeded(), bayer.message.c_str());
    QCOMPARE(bayer.mode, GlobalHistogramMode::BayerChannels);
    QCOMPARE(bayer.series.size(), std::size_t{4});
    for (const auto& series : bayer.series) {
        QCOMPARE(series.sampleCount, std::uint64_t{1});
        QCOMPARE(std::accumulate(series.bins.begin(), series.bins.end(),
                                 std::uint64_t{0}),
                 std::uint64_t{1});
    }

    auto directImage = makeBayerImage(4, 4);
    auto directSource = std::dynamic_pointer_cast<const GridPixelSource>(
        directImage->pixels);
    auto directPixels = std::make_shared<std::vector<std::uint16_t>>(16);
    std::iota(directPixels->begin(), directPixels->end(), std::uint16_t{0});
    directImage->preview.width = 4;
    directImage->preview.height = 4;
    directImage->preview.grayscale16Storage = directPixels;
    directImage->preview.grayscale16Pixels = directPixels->data();
    directImage->preview.grayscale16StrideSamples = 4;
    auto direct = service.execute({directImage, 65536, {}, {}});
    QVERIFY2(direct.succeeded(), direct.message.c_str());
    QCOMPARE(directSource->sampleCalls, std::uint64_t{0});
    QCOMPARE(direct.series[0].bins[0], std::uint64_t{1});
    QCOMPARE(direct.series[3].bins[5], std::uint64_t{1});

    auto mono = service.execute({makeImage(), 64, {}, {}});
    QVERIFY2(mono.succeeded(), mono.message.c_str());
    QCOMPARE(mono.mode, GlobalHistogramMode::SingleChannel);
    QCOMPARE(mono.series.size(), std::size_t{1});
    QCOMPARE(mono.series.front().sampleCount, std::uint64_t{3});

    rawviewer::application::BayerExtractRequest extractRequest;
    extractRequest.source = makeBayerImage(4, 4);
    extractRequest.mask = {"R only", 2, 2, {1, 0, 0, 0}};
    auto extracted = rawviewer::application::BayerExtractService().execute(
        extractRequest);
    QVERIFY2(extracted.succeeded(), extracted.message.c_str());
    auto singleBayer = service.execute(
        {extracted.extraction->image, 64, {}, {}});
    QVERIFY2(singleBayer.succeeded(), singleBayer.message.c_str());
    QCOMPARE(singleBayer.mode, GlobalHistogramMode::SingleChannel);
    QCOMPARE(singleBayer.series.front().component,
             rawviewer::application::GlobalHistogramComponent::Red);

    auto rgbImage = std::make_shared<rawviewer::application::DecodedImage>();
    rgbImage->metadata.kind = rawviewer::application::ImageKind::Standard;
    rgbImage->metadata.scalarType = rawviewer::domain::ScalarType::UInt8;
    rgbImage->metadata.width = 2;
    rgbImage->metadata.height = 1;
    rgbImage->pixels = std::make_shared<RgbPixelSource>();
    auto rgb = service.execute({rgbImage, 64, {}, {}});
    QVERIFY2(rgb.succeeded(), rgb.message.c_str());
    QCOMPARE(rgb.mode, GlobalHistogramMode::RgbLuminance);
    QCOMPARE(rgb.series.size(), std::size_t{4});
    for (const auto& series : rgb.series) {
        QCOMPARE(series.sampleCount, std::uint64_t{2});
    }
    QCOMPARE(rgb.series[2].bins.front(), std::uint64_t{2});

    auto token = std::make_shared<std::atomic_bool>(true);
    auto cancelled = service.execute({makeImage(), 64, token, {}});
    QCOMPARE(cancelled.errorCode, std::string("task.cancelled"));
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

void DocumentSessionTest::keepsPixelInfoOnLatestIndependentExtraction() {
    const auto original = makeBayerImage(4, 4);
    rawviewer::application::BayerExtractService service;
    rawviewer::application::BayerExtractRequest request;
    request.source = original;
    request.mask = {"R", 2, 2, {1, 0, 0, 0}};

    const auto first = service.execute(request);
    QVERIFY2(first.succeeded(), first.message.c_str());
    QCOMPARE(first.extraction->image->pixels->sample(0, 0).value, 0.0);
    QCOMPARE(first.extraction->image->pixels->sample(1, 1).value, 22.0);

    // MainWindow deliberately submits every extraction against original().
    // Reusing that contract here catches accidental extract-on-extract
    // chaining while also verifying the exact source used by pixel labels.
    request.source = original;
    request.mask = {"Gr", 2, 2, {0, 1, 0, 0}};
    const auto second = service.execute(request);
    QVERIFY2(second.succeeded(), second.message.c_str());
    QCOMPARE(second.extraction->image->pixels->sample(0, 0).value, 1.0);
    QCOMPARE(second.extraction->image->pixels->sample(1, 0).value, 3.0);
    QCOMPARE(second.extraction->image->pixels->sample(0, 1).value, 21.0);
    QCOMPARE(second.extraction->image->pixels->sample(1, 1).value, 23.0);

    const auto info = rawviewer::application::queryPixelInfo(
        *second.extraction->image, {}, 1, 1);
    QVERIFY(info.valid);
    QCOMPARE(info.originalValue, 23.0);
    QCOMPARE(info.channel, rawviewer::domain::BayerChannel::Gr);
    QCOMPARE(second.extraction->image->pixels->bayerChannel(0, 0),
             rawviewer::domain::BayerChannel::Gr);
}

void DocumentSessionTest::extractsSinglePositionToDenseOutputCoordinates() {
    rawviewer::application::BayerExtractRequest request;
    request.source = makeBayerImage(200, 200);
    request.mask = {"top-left", 2, 2, {1, 0, 0, 0}};

    const auto result =
        rawviewer::application::BayerExtractService().execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    const auto& extraction = *result.extraction;
    QCOMPARE(extraction.geometry.width, std::uint64_t{100});
    QCOMPARE(extraction.geometry.height, std::uint64_t{100});
    QCOMPARE(extraction.image->metadata.width, std::uint64_t{100});
    QCOMPARE(extraction.image->metadata.height, std::uint64_t{100});
    QCOMPARE(extraction.image->pixels->width(), std::uint64_t{100});
    QCOMPARE(extraction.image->pixels->height(), std::uint64_t{100});
    QCOMPARE(extraction.image->signalPreview->width, 100);
    QCOMPARE(extraction.image->signalPreview->height, 100);
    QCOMPARE(extraction.image->pixels->sample(0, 0).value, 0.0);
    QCOMPARE(extraction.image->pixels->sample(99, 99).value, 2178.0);
    const std::optional<rawviewer::domain::BayerCoordinate> source198{
        rawviewer::domain::BayerCoordinate{198, 198}};
    QCOMPARE(extraction.geometry.sourceCoordinate(99, 99), source198);
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

void DocumentSessionTest::calculatesStatisticsFromExtractedDisplayPipeline() {
    rawviewer::application::BayerExtractRequest extractRequest;
    extractRequest.source = makeBayerImage(4, 4);
    extractRequest.mask = {"top-right", 2, 2, {0, 1, 0, 0}};
    const auto extracted =
        rawviewer::application::BayerExtractService().execute(extractRequest);
    QVERIFY2(extracted.succeeded(), extracted.message.c_str());
    QCOMPARE(extracted.extraction->image->metadata.bayerPattern,
             rawviewer::domain::BayerPattern::None);

    rawviewer::application::PixelStatisticsRequest request;
    request.source = extracted.extraction->image;
    request.selection = {0, 0, 1, 1};
    request.mode = rawviewer::application::PixelStatisticsMode::Status;
    request.histogramBins = 64;
    const auto result =
        rawviewer::application::PixelStatisticsService().execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.summary.count, std::uint64_t{4});
    QCOMPARE(result.summary.minimum, 1.0);
    QCOMPARE(result.summary.maximum, 23.0);
    QCOMPARE(result.summary.mean, 12.0);

    request.channel = rawviewer::domain::BayerChannel::R;
    const auto unavailable =
        rawviewer::application::PixelStatisticsService().execute(request);
    QVERIFY(!unavailable.succeeded());
    QCOMPARE(QString::fromStdString(unavailable.errorCode),
             QStringLiteral("statistics.channel_unavailable"));
}

void DocumentSessionTest::calculatesFourChannelsInSinglePass() {
    auto image = makeBayerImage(4, 4);
    const auto source = std::dynamic_pointer_cast<const GridPixelSource>(
        image->pixels);
    QVERIFY(source);
    rawviewer::application::PixelStatisticsRequest request;
    request.source = image;
    request.selection = {0, 0, 3, 3};
    request.mode = rawviewer::application::PixelStatisticsMode::Status;
    request.histogramBins = 64;

    const auto results =
        rawviewer::application::PixelStatisticsService().executeChannels(request);
    QCOMPARE(results.size(), std::size_t{4});
    QCOMPARE(source->sampleCalls, std::uint64_t{16});
    const std::array expectedMeans{11.0, 12.0, 21.0, 22.0};
    for (std::size_t index = 0; index < results.size(); ++index) {
        QVERIFY2(results[index].succeeded(), results[index].message.c_str());
        QCOMPARE(results[index].summary.count, std::uint64_t{4});
        QCOMPARE(results[index].summary.mean, expectedMeans[index]);
    }

    for (const auto mode : {
             rawviewer::application::PixelStatisticsMode::HorizontalBox,
             rawviewer::application::PixelStatisticsMode::VerticalBox}) {
        source->sampleCalls = 0;
        request.mode = mode;
        const auto profiles =
            rawviewer::application::PixelStatisticsService().executeChannels(
                request);
        QCOMPARE(source->sampleCalls, std::uint64_t{16});
        QCOMPARE(profiles.size(), std::size_t{4});
        for (std::size_t index = 0; index < profiles.size(); ++index) {
            QVERIFY2(profiles[index].succeeded(),
                     profiles[index].message.c_str());
            QCOMPARE(profiles[index].summary.count, std::uint64_t{2});
            QCOMPARE(profiles[index].summary.mean, expectedMeans[index]);
        }
    }

    source->sampleCalls = 0;
    request.mode = rawviewer::application::PixelStatisticsMode::Line;
    const auto line =
        rawviewer::application::PixelStatisticsService().executeChannels(request);
    QCOMPARE(source->sampleCalls, std::uint64_t{4});
    QCOMPARE(line.size(), std::size_t{4});
    QVERIFY(line[0].succeeded());
    QCOMPARE(line[0].summary.count, std::uint64_t{2});
    QCOMPARE(line[0].summary.mean, 11.0);
    QVERIFY(!line[1].succeeded());
    QVERIFY(!line[2].succeeded());
    QVERIFY(line[3].succeeded());
    QCOMPARE(line[3].summary.count, std::uint64_t{2});
    QCOMPARE(line[3].summary.mean, 22.0);
}

void DocumentSessionTest::filtersCurrentRawWithGoldenValues() {
    rawviewer::application::FilterService service;
    rawviewer::application::FilterRequest request;
    request.source = makeBayerImage(5, 5);
    request.parameters.type = rawviewer::application::FilterType::Mean;
    request.parameters.kernelSize = 3;
    auto result = service.execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    const auto center = result.image->pixels->sample(2, 2);
    QVERIFY(center.valid);
    QCOMPARE(center.value, 22.0);
    const auto clampedCorner = result.image->pixels->sample(0, 0);
    QVERIFY(clampedCorner.valid);
    QVERIFY(std::abs(clampedCorner.value - 33.0 / 9.0) < 1.0e-12);

    request.parameters.type = rawviewer::application::FilterType::Gaussian;
    request.parameters.sigma = 1.0;
    result = service.execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    QVERIFY(std::abs(result.image->pixels->sample(2, 2).value - 22.0) <
            1.0e-12);

    auto impulse = makeBayerImage(3, 3);
    impulse->pixels = std::make_shared<VectorPixelSource>(
        3, 3, std::vector<double>{0, 0, 0, 0, 100, 0, 0, 0, 0});
    request.source = impulse;
    request.parameters.type = rawviewer::application::FilterType::Median;
    result = service.execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.image->pixels->sample(1, 1).value, 0.0);
    QCOMPARE(impulse->pixels->sample(1, 1).value, 100.0);
}

void DocumentSessionTest::chainsFiltersWithoutMutatingTheSource() {
    const auto original = makeBayerImage(5, 5);
    rawviewer::application::FilterService service;
    rawviewer::application::FilterRequest firstRequest;
    firstRequest.source = original;
    firstRequest.parameters.type = rawviewer::application::FilterType::Mean;
    const auto first = service.execute(firstRequest);
    QVERIFY2(first.succeeded(), first.message.c_str());

    rawviewer::application::FilterRequest secondRequest;
    secondRequest.source = first.image;
    secondRequest.parameters.type = rawviewer::application::FilterType::Median;
    const auto second = service.execute(secondRequest);
    QVERIFY2(second.succeeded(), second.message.c_str());
    QVERIFY(second.image->metadata.format.find("Mean filter / Median filter") !=
            std::string::npos);
    QCOMPARE(original->metadata.format, std::string("Synthetic RAW"));
    QCOMPARE(original->pixels->sample(0, 0).value, 0.0);
}

void DocumentSessionTest::reusesBoundedFilterTiles() {
    auto image = makeBayerImage(128, 64);
    const auto source = std::dynamic_pointer_cast<const GridPixelSource>(
        image->pixels);
    QVERIFY(source);
    rawviewer::application::FilterRequest request;
    request.source = image;
    request.parameters.type = rawviewer::application::FilterType::Mean;
    request.parameters.kernelSize = 3;
    const auto result = rawviewer::application::FilterService().execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());

    source->sampleCalls = 0;
    for (std::uint64_t y = 0; y < 64; ++y) {
        for (std::uint64_t x = 0; x < 128; ++x) {
            QVERIFY(result.image->pixels->sample(x, y).valid);
        }
    }
    const auto firstPassReads = source->sampleCalls;
    QVERIFY2(firstPassReads < 12'000,
             "tile filtering must read the halo once, not per output pixel");
    QCOMPARE(firstPassReads, std::uint64_t{8'712});
    QVERIFY(result.image->pixels->sample(0, 0).valid);
    QVERIFY(result.image->pixels->sample(127, 63).valid);
    QCOMPARE(source->sampleCalls, firstPassReads);
    QVERIFY(result.image->signalPreview);
    QVERIFY(result.image->signalPreview->width <= 1024);
    QVERIFY(result.image->signalPreview->height <= 1024);

    auto wideImage = makeBayerImage(4096, 2);
    const auto wideSource = std::dynamic_pointer_cast<const GridPixelSource>(
        wideImage->pixels);
    request.source = wideImage;
    const auto wideResult =
        rawviewer::application::FilterService().execute(request);
    QVERIFY2(wideResult.succeeded(), wideResult.message.c_str());
    wideSource->sampleCalls = 0;
    for (std::uint64_t y = 0; y < 2; ++y) {
        for (std::uint64_t x = 0; x < 4096; ++x) {
            QVERIFY(wideResult.image->pixels->sample(x, y).valid);
        }
    }
    QCOMPARE(wideSource->sampleCalls, std::uint64_t{16'896});

    auto previewImage = makeBayerImage(1100, 10);
    auto signal = std::make_shared<rawviewer::application::SignalPreview>();
    signal->width = 1100;
    signal->height = 10;
    signal->values.resize(11'000, 42.0F);
    previewImage->signalPreview = signal;
    request.source = previewImage;
    const auto bounded =
        rawviewer::application::FilterService().execute(request);
    QVERIFY2(bounded.succeeded(), bounded.message.c_str());
    QCOMPARE(bounded.image->signalPreview->width, 1024);
    QVERIFY(bounded.image->signalPreview->height <= 1024);
}

void DocumentSessionTest::rejectsInvalidAndCancelledFilters() {
    rawviewer::application::FilterRequest request;
    request.source = makeBayerImage(3, 3);
    request.parameters.kernelSize = 9;
    auto result = rawviewer::application::FilterService().execute(request);
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("filter.invalid_parameters"));

    request.parameters.kernelSize = 3;
    request.cancellation = std::make_shared<std::atomic_bool>(true);
    result = rawviewer::application::FilterService().execute(request);
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("task.cancelled"));
}

void DocumentSessionTest::demosaicsAllBayerPatternsWithThreeAlgorithms() {
    rawviewer::application::DemosaicService service;
    const std::array patterns{
        rawviewer::domain::BayerPattern::RGGB,
        rawviewer::domain::BayerPattern::BGGR,
        rawviewer::domain::BayerPattern::GRBG,
        rawviewer::domain::BayerPattern::GBRG};
    const std::array algorithms{
        rawviewer::application::DemosaicAlgorithm::Bilinear,
        rawviewer::application::DemosaicAlgorithm::MalvarHeCutler,
        rawviewer::application::DemosaicAlgorithm::HamiltonAdams};
    for (const auto pattern : patterns) {
        const auto source = makeConstantColorBayer(8, 8, pattern);
        for (const auto algorithm : algorithms) {
            rawviewer::application::DemosaicRequest request;
            request.source = source;
            request.algorithm = algorithm;
            request.displayMapping = {0.0, 100.0, 1.0};
            const auto result = service.execute(request);
            QVERIFY2(result.succeeded(), result.message.c_str());
            QCOMPARE(result.image->metadata.kind,
                     rawviewer::application::ImageKind::Standard);
            QCOMPARE(result.image->metadata.bayerPattern,
                     rawviewer::domain::BayerPattern::None);
            QVERIFY(result.image->displayReadyRgb);
            for (const auto point : {
                     std::pair<std::uint64_t, std::uint64_t>{0, 0},
                     {3, 3}, {7, 7}}) {
                const auto sample = result.image->pixels->sample(
                    point.first, point.second);
                QVERIFY(sample.valid);
                QVERIFY(sample.rgbValid);
                QCOMPARE(sample.red, std::uint8_t{255});
                QCOMPARE(sample.green, std::uint8_t{128});
                QCOMPARE(sample.blue, std::uint8_t{26});
            }
        }
        QCOMPARE(source->metadata.kind,
                 rawviewer::application::ImageKind::FlatRaw);
        QCOMPARE(source->metadata.bayerPattern, pattern);
    }

    auto impulse = makeBayerImage(5, 5);
    impulse->metadata.whiteLevel = 100.0;
    std::vector<double> impulseValues(25, 0.0);
    impulseValues[12] = 80.0; // RGGB red site at (2, 2).
    impulse->pixels = std::make_shared<VectorPixelSource>(
        5, 5, std::move(impulseValues));
    rawviewer::application::DemosaicRequest impulseRequest;
    impulseRequest.source = impulse;
    impulseRequest.displayMapping = {0.0, 100.0, 1.0};

    impulseRequest.algorithm =
        rawviewer::application::DemosaicAlgorithm::Bilinear;
    auto result = service.execute(impulseRequest);
    auto sample = result.image->pixels->sample(2, 2);
    QCOMPARE(sample.red, std::uint8_t{204});
    QCOMPARE(sample.green, std::uint8_t{0});
    QCOMPARE(sample.blue, std::uint8_t{0});

    impulseRequest.algorithm =
        rawviewer::application::DemosaicAlgorithm::MalvarHeCutler;
    result = service.execute(impulseRequest);
    sample = result.image->pixels->sample(2, 2);
    QCOMPARE(sample.red, std::uint8_t{204});
    QCOMPARE(sample.green, std::uint8_t{102});
    QCOMPARE(sample.blue, std::uint8_t{153});

    impulseRequest.algorithm =
        rawviewer::application::DemosaicAlgorithm::HamiltonAdams;
    result = service.execute(impulseRequest);
    sample = result.image->pixels->sample(2, 2);
    QCOMPARE(sample.red, std::uint8_t{204});
    QCOMPARE(sample.green, std::uint8_t{102});
    QCOMPARE(sample.blue, std::uint8_t{102});
    const auto rendered = rawviewer::application::PreviewRenderer::render(
        result.image, {0.0, 65535.0, 2.2});
    QVERIFY(rendered);
    QCOMPARE(rendered->preview.rgba, result.image->preview.rgba);
    const auto info = rawviewer::application::queryPixelInfo(
        *rendered, {0.0, 65535.0, 2.2}, 2, 2);
    QVERIFY(info.rgbValid);
    QCOMPARE(info.red, sample.red);
    QCOMPARE(info.green, sample.green);
    QCOMPARE(info.blue, sample.blue);
}

void DocumentSessionTest::keepsDemosaicPreviewAndExactTilesBounded() {
    auto source = makeBayerImage(128, 64);
    const auto pixels = std::dynamic_pointer_cast<const GridPixelSource>(
        source->pixels);
    QVERIFY(pixels);
    rawviewer::application::DemosaicRequest request;
    request.source = source;
    request.algorithm =
        rawviewer::application::DemosaicAlgorithm::MalvarHeCutler;
    request.displayMapping = {0.0, 65535.0, 1.0};
    const auto result = rawviewer::application::DemosaicService().execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    pixels->sampleCalls = 0;
    for (std::uint64_t y = 0; y < 64; ++y) {
        for (std::uint64_t x = 0; x < 128; ++x) {
            QVERIFY(result.image->pixels->sample(x, y).valid);
        }
    }
    QCOMPARE(pixels->sampleCalls, std::uint64_t{9'800});
    const auto firstPassReads = pixels->sampleCalls;
    QVERIFY(result.image->pixels->sample(0, 0).valid);
    QVERIFY(result.image->pixels->sample(127, 63).valid);
    QCOMPARE(pixels->sampleCalls, firstPassReads);

    auto wide = makeBayerImage(1100, 10);
    request.source = wide;
    const auto bounded =
        rawviewer::application::DemosaicService().execute(request);
    QVERIFY2(bounded.succeeded(), bounded.message.c_str());
    QCOMPARE(bounded.image->preview.width, 1024);
    QVERIFY(bounded.image->preview.height <= 1024);
    QCOMPARE(bounded.image->preview.rgba.size(),
             static_cast<std::size_t>(bounded.image->preview.width *
                 bounded.image->preview.height * 4));

    auto filteredSource = makeBayerImage(1100, 10);
    const auto filteredPixels =
        std::dynamic_pointer_cast<const GridPixelSource>(
            filteredSource->pixels);
    rawviewer::application::FilterRequest filterRequest;
    filterRequest.source = filteredSource;
    filterRequest.parameters.type =
        rawviewer::application::FilterType::Mean;
    const auto filtered =
        rawviewer::application::FilterService().execute(filterRequest);
    QVERIFY2(filtered.succeeded(), filtered.message.c_str());
    QVERIFY(filtered.image->signalPreview->preservesBayerPhase);
    filteredPixels->sampleCalls = 0;
    request.source = filtered.image;
    const auto chained =
        rawviewer::application::DemosaicService().execute(request);
    QVERIFY2(chained.succeeded(), chained.message.c_str());
    QCOMPARE(filteredPixels->sampleCalls, std::uint64_t{0});
}

void DocumentSessionTest::rejectsUnsupportedAndCancelledDemosaic() {
    rawviewer::application::DemosaicRequest request;
    request.source = makeBayerImage(
        4, 4, rawviewer::domain::BayerPattern::None);
    auto result = rawviewer::application::DemosaicService().execute(request);
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("demosaic.unsupported_source"));

    request.source = makeBayerImage(4, 4);
    request.cancellation = std::make_shared<std::atomic_bool>(true);
    result = rawviewer::application::DemosaicService().execute(request);
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("task.cancelled"));
}

QTEST_APPLESS_MAIN(DocumentSessionTest)
#include "document_session_test.moc"
