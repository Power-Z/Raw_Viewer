#include "application/bayer_extract.h"
#include "application/document_session.h"
#include "application/pixel_info.h"
#include "application/preview_renderer.h"

#include <QTest>

#include <memory>

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
    void queriesOriginalProcessedRgbAndBayerValues();
    void rejectsOutOfBoundsPixelInfo();
    void extractsChannelsFromOddSizedImage();
    void extractsUnalignedRoiAndConvertsCoordinates();
    void rejectsInvalidAndEmptyBayerRegions();
    void cancelsBayerExtraction();
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
    QVERIFY(info.rgbValid);
    QCOMPARE(info.red, std::uint8_t{128});
    QCOMPARE(info.green, std::uint8_t{128});
    QCOMPARE(info.blue, std::uint8_t{128});
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
    request.channel = rawviewer::domain::BayerChannel::R;
    const auto red = service.execute(request);
    QVERIFY2(red.succeeded(), red.message.c_str());
    QCOMPARE(red.extraction->geometry.width, std::uint64_t{3});
    QCOMPARE(red.extraction->geometry.height, std::uint64_t{2});
    QCOMPARE(red.extraction->geometry.sourceOriginX, std::uint64_t{0});
    QCOMPARE(red.extraction->geometry.sourceOriginY, std::uint64_t{0});
    QCOMPARE(red.extraction->image->pixels->sample(2, 1).value, 24.0);
    QCOMPARE(request.source->pixels->sample(4, 2).value, 24.0);

    request.channel = rawviewer::domain::BayerChannel::B;
    const auto blue = service.execute(request);
    QVERIFY2(blue.succeeded(), blue.message.c_str());
    QCOMPARE(blue.extraction->geometry.width, std::uint64_t{2});
    QCOMPARE(blue.extraction->geometry.height, std::uint64_t{1});
    QCOMPARE(blue.extraction->image->pixels->sample(1, 0).value, 13.0);
}

void DocumentSessionTest::extractsUnalignedRoiAndConvertsCoordinates() {
    rawviewer::application::BayerExtractRequest request;
    request.source = makeBayerImage(5, 5);
    request.channel = rawviewer::domain::BayerChannel::Gr;
    request.sourceRegion = rawviewer::application::PixelRegion{1, 1, 4, 4};
    const auto result =
        rawviewer::application::BayerExtractService().execute(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    const auto& geometry = result.extraction->geometry;
    QCOMPARE(geometry.width, std::uint64_t{2});
    QCOMPARE(geometry.height, std::uint64_t{2});
    QCOMPARE(geometry.sourceOriginX, std::uint64_t{1});
    QCOMPARE(geometry.sourceOriginY, std::uint64_t{2});
    const std::optional<rawviewer::domain::BayerCoordinate> source34 =
        rawviewer::domain::BayerCoordinate{3, 4};
    const std::optional<rawviewer::domain::BayerCoordinate> channel11 =
        rawviewer::domain::BayerCoordinate{1, 1};
    QCOMPARE(geometry.sourceCoordinate(1, 1), source34);
    QCOMPARE(geometry.channelCoordinate(3, 4), channel11);
    QVERIFY(!geometry.channelCoordinate(2, 4));
    QVERIFY(!geometry.sourceCoordinate(2, 1));
    QCOMPARE(result.extraction->image->pixels->sample(1, 1).value, 43.0);
}

void DocumentSessionTest::rejectsInvalidAndEmptyBayerRegions() {
    rawviewer::application::BayerExtractRequest request;
    request.source = makeBayerImage(5, 5);
    request.channel = rawviewer::domain::BayerChannel::R;
    request.sourceRegion = rawviewer::application::PixelRegion{4, 4, 2, 1};
    auto result = rawviewer::application::BayerExtractService().execute(request);
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("bayer.invalid_region"));

    request.channel = rawviewer::domain::BayerChannel::B;
    request.sourceRegion = rawviewer::application::PixelRegion{0, 0, 1, 1};
    result = rawviewer::application::BayerExtractService().execute(request);
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("bayer.empty_channel"));
}

void DocumentSessionTest::cancelsBayerExtraction() {
    rawviewer::application::BayerExtractRequest request;
    request.source = makeBayerImage(5, 5);
    request.channel = rawviewer::domain::BayerChannel::R;
    request.cancellation = std::make_shared<std::atomic_bool>(true);
    const auto result =
        rawviewer::application::BayerExtractService().execute(request);
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("task.cancelled"));
}

QTEST_APPLESS_MAIN(DocumentSessionTest)
#include "document_session_test.moc"
