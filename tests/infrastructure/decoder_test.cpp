#include "infrastructure/bayer_csv_exporter.h"
#include "infrastructure/camera_raw_decoder.h"
#include "infrastructure/flat_raw_decoder.h"
#include "infrastructure/qt_image_decoder.h"
#include "infrastructure/qt_recent_document_store.h"

#include "application/bayer_extract.h"
#include "application/pixel_info.h"
#include "application/pixel_statistics.h"

#include <QFile>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <memory>
#include <numeric>

namespace {

std::filesystem::path nativePath(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

rawviewer::application::OpenImageRequest makeRequest(
    const QString& path,
    rawviewer::domain::RawDescriptor descriptor) {
    rawviewer::application::OpenImageRequest request;
    request.path = nativePath(path);
    request.flatRawDescriptor = descriptor;
    request.cancellation = std::make_shared<std::atomic_bool>(false);
    return request;
}

} // namespace

class DecoderTest final : public QObject {
    Q_OBJECT

private slots:
    void decodesLittleEndianWithHeaderAndStride();
    void unfoldsSequentialBayerSamplesAfterSkip();
    void decodesBigEndianUInt32();
    void decodesFloat32();
    void refusesTruncatedFile();
    void cameraContainerWinsBySignature();
    void preservesStandardImageRgbForPixelInfo();
    void exportsExtractedBayerChannelAsCsv();
    void remembersTenSuccessfulDocumentConfigurations();
    void verifiesApprovedFlatSampleWhenConfigured();
    void verifiesApprovedCameraSampleWhenConfigured();
};

void DecoderTest::decodesLittleEndianWithHeaderAndStride() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("little.raw");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray bytes;
    bytes.append('\x7f');
    bytes.append('\x7f');
    bytes.append('\x01');
    bytes.append('\x00');
    bytes.append('\x02');
    bytes.append('\x00');
    bytes.append('\0');
    bytes.append('\0');
    bytes.append('\x34');
    bytes.append('\x12');
    bytes.append('\xff');
    bytes.append('\x00');
    bytes.append('\0');
    bytes.append('\0');
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();

    rawviewer::domain::RawDescriptor descriptor;
    descriptor.width = 2;
    descriptor.height = 2;
    descriptor.headerBytes = 2;
    descriptor.rowStrideBytes = 6;
    descriptor.scalarType = rawviewer::domain::ScalarType::UInt16;

    rawviewer::infrastructure::FlatRawDecoder decoder;
    const auto result = decoder.decode(makeRequest(path, descriptor));
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.image->pixels->sample(0, 0).value, 1.0);
    QCOMPARE(result.image->pixels->sample(1, 0).value, 2.0);
    QCOMPARE(result.image->pixels->sample(0, 1).value, 4660.0);
    QCOMPARE(result.image->pixels->sample(1, 1).value, 255.0);
}

void DecoderTest::unfoldsSequentialBayerSamplesAfterSkip() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("sequence.RAW");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray bytes;
    bytes.append("SKIP", 4);
    for (std::uint16_t value = 1; value <= 6; ++value) {
        bytes.append(static_cast<char>(value & 0xff));
        bytes.append(static_cast<char>((value >> 8) & 0xff));
    }
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();

    rawviewer::domain::RawDescriptor descriptor;
    descriptor.width = 3;
    descriptor.height = 2;
    descriptor.headerBytes = 4;
    descriptor.scalarType = rawviewer::domain::ScalarType::UInt16;
    descriptor.byteOrder = rawviewer::domain::ByteOrder::LittleEndian;
    descriptor.bayerPattern = rawviewer::domain::BayerPattern::RGGB;

    rawviewer::infrastructure::FlatRawDecoder decoder;
    const auto result = decoder.decode(makeRequest(path, descriptor));
    QVERIFY2(result.succeeded(), result.message.c_str());
    for (std::uint64_t y = 0; y < 2; ++y) {
        for (std::uint64_t x = 0; x < 3; ++x) {
            QCOMPARE(result.image->pixels->sample(x, y).value,
                     static_cast<double>(y * 3 + x + 1));
        }
    }
    QCOMPARE(result.image->metadata.bayerPattern,
             rawviewer::domain::BayerPattern::RGGB);
    QVERIFY(result.image->preview.hasGrayscale16());
    QCOMPARE(result.image->preview.width, 3);
    QCOMPARE(result.image->preview.height, 2);
    QCOMPARE(result.image->preview.grayscale16StrideSamples, 4);
    QCOMPARE(result.image->preview.grayscale16Pixels[0], std::uint16_t{1});
    QCOMPARE(result.image->preview.grayscale16Pixels[1], std::uint16_t{2});
    QCOMPARE(result.image->preview.grayscale16Pixels[2], std::uint16_t{3});
    QCOMPARE(result.image->preview.grayscale16Pixels[4], std::uint16_t{4});
    QCOMPARE(result.image->preview.grayscale16Pixels[5], std::uint16_t{5});
    QCOMPARE(result.image->preview.grayscale16Pixels[6], std::uint16_t{6});
    QVERIFY(!result.image->signalPreview);
}

void DecoderTest::decodesBigEndianUInt32() {
    QTemporaryDir directory;
    const QString path = directory.filePath("big.bin");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray bytes;
    bytes.append('\x01');
    bytes.append('\x02');
    bytes.append('\x03');
    bytes.append('\x04');
    bytes.append('\x7f');
    bytes.append('\xff');
    bytes.append('\xff');
    bytes.append('\xff');
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();

    rawviewer::domain::RawDescriptor descriptor;
    descriptor.width = 2;
    descriptor.height = 1;
    descriptor.scalarType = rawviewer::domain::ScalarType::UInt32;
    descriptor.byteOrder = rawviewer::domain::ByteOrder::BigEndian;

    rawviewer::infrastructure::FlatRawDecoder decoder;
    const auto result = decoder.decode(makeRequest(path, descriptor));
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.image->pixels->sample(0, 0).value, 16909060.0);
    QCOMPARE(result.image->pixels->sample(1, 0).value, 2147483647.0);
}

void DecoderTest::decodesFloat32() {
    QTemporaryDir directory;
    const QString path = directory.filePath("float.raw");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray bytes;
    bytes.append('\0');
    bytes.append('\0');
    bytes.append('\x80');
    bytes.append('\x3f');
    bytes.append('\0');
    bytes.append('\0');
    bytes.append('\x20');
    bytes.append('\x40');
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();

    rawviewer::domain::RawDescriptor descriptor;
    descriptor.width = 2;
    descriptor.height = 1;
    descriptor.scalarType = rawviewer::domain::ScalarType::Float32;

    rawviewer::infrastructure::FlatRawDecoder decoder;
    const auto result = decoder.decode(makeRequest(path, descriptor));
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.image->pixels->sample(0, 0).value, 1.0);
    QCOMPARE(result.image->pixels->sample(1, 0).value, 2.5);
}

void DecoderTest::refusesTruncatedFile() {
    QTemporaryDir directory;
    const QString path = directory.filePath("short.raw");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("123");
    file.close();

    rawviewer::domain::RawDescriptor descriptor;
    descriptor.width = 2;
    descriptor.height = 2;
    descriptor.scalarType = rawviewer::domain::ScalarType::UInt16;

    rawviewer::infrastructure::FlatRawDecoder decoder;
    const auto result = decoder.decode(makeRequest(path, descriptor));
    QVERIFY(!result.succeeded());
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("raw.file_truncated"));
}

void DecoderTest::cameraContainerWinsBySignature() {
    const std::array<std::byte, 4> signature{
        std::byte{'I'}, std::byte{'I'}, std::byte{42}, std::byte{0}
    };
    rawviewer::infrastructure::CameraRawDecoder camera;
    rawviewer::infrastructure::FlatRawDecoder flat;
    const auto path = std::filesystem::path("container.raw");
    QCOMPARE(camera.probe(path, signature, true),
             rawviewer::application::ProbeStrength::Definitive);
    QCOMPARE(flat.probe(path, signature, true),
             rawviewer::application::ProbeStrength::Fallback);
}

void DecoderTest::preservesStandardImageRgbForPixelInfo() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("rgb.png");
    QImage source(1, 1, QImage::Format_RGBA8888);
    source.fill(QColor(10, 20, 30));
    QVERIFY(source.save(path));

    rawviewer::application::OpenImageRequest request;
    request.path = nativePath(path);
    request.cancellation = std::make_shared<std::atomic_bool>(false);
    rawviewer::infrastructure::QtImageDecoder decoder;
    const auto result = decoder.decode(request);
    QVERIFY2(result.succeeded(), result.message.c_str());

    rawviewer::domain::DisplayMapping mapping;
    const auto info = rawviewer::application::queryPixelInfo(
        *result.image, mapping, 0, 0);
    QVERIFY(info.valid);
    QVERIFY(info.rgbValid);
    QCOMPARE(info.red, std::uint8_t{10});
    QCOMPARE(info.green, std::uint8_t{20});
    QCOMPARE(info.blue, std::uint8_t{30});
}

void DecoderTest::exportsExtractedBayerChannelAsCsv() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath("source.raw");
    QFile sourceFile(sourcePath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    QByteArray bytes;
    for (char value = 0; value < 15; ++value) {
        bytes.append(value);
    }
    QCOMPARE(sourceFile.write(bytes), bytes.size());
    sourceFile.close();

    rawviewer::domain::RawDescriptor descriptor;
    descriptor.width = 5;
    descriptor.height = 3;
    descriptor.scalarType = rawviewer::domain::ScalarType::UInt8;
    descriptor.bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    rawviewer::infrastructure::FlatRawDecoder decoder;
    const auto decoded = decoder.decode(makeRequest(sourcePath, descriptor));
    QVERIFY2(decoded.succeeded(), decoded.message.c_str());

    rawviewer::application::BayerExtractRequest extractRequest;
    extractRequest.source = decoded.image;
    extractRequest.mask = {"Gr", 2, 2, {0, 1, 0, 0}};
    const auto extracted =
        rawviewer::application::BayerExtractService().execute(extractRequest);
    QVERIFY2(extracted.succeeded(), extracted.message.c_str());

    const QString csvPath = directory.filePath("gr.csv");
    rawviewer::application::BayerExportRequest exportRequest;
    exportRequest.extraction = extracted.extraction;
    exportRequest.path = nativePath(csvPath);
    exportRequest.cancellation = std::make_shared<std::atomic_bool>(false);
    rawviewer::infrastructure::BayerCsvExporter exporter;
    const auto exported = exporter.exportCsv(exportRequest);
    QVERIFY2(exported.succeeded, exported.message.c_str());
    QCOMPARE(exported.exportedSamples, std::uint64_t{4});

    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(csv.readAll(),
             QByteArray("output_x,output_y,source_x,source_y,value\n"
                        "0,0,1,0,1\n"
                        "1,0,3,0,3\n"
                        "0,1,1,2,11\n"
                        "1,1,3,2,13\n"));
}

void DecoderTest::remembersTenSuccessfulDocumentConfigurations() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsPath = directory.filePath("recent-files.ini");
    rawviewer::infrastructure::QtRecentDocumentStore store(settingsPath);

    for (int index = 0; index < 12; ++index) {
        rawviewer::application::RecentDocument document;
        document.path = nativePath(directory.filePath(
            QStringLiteral("image-%1.raw").arg(index)));
        document.rawDescriptor.width = static_cast<std::uint64_t>(100 + index);
        document.rawDescriptor.height = static_cast<std::uint64_t>(200 + index);
        document.rawDescriptor.headerBytes =
            std::uint64_t{9007199254740992} + static_cast<std::uint64_t>(index);
        document.rawDescriptor.rowStrideBytes = 4096 + index;
        document.rawDescriptor.scalarType = rawviewer::domain::ScalarType::UInt32;
        document.rawDescriptor.byteOrder = rawviewer::domain::ByteOrder::BigEndian;
        document.rawDescriptor.bayerPattern = rawviewer::domain::BayerPattern::GBRG;
        document.rawDescriptor.sensorBlackLevel = 64.5 + index;
        store.remember(document);
    }

    auto recent = store.load();
    QCOMPARE(recent.size(), std::size_t{10});
    QCOMPARE(recent.front().rawDescriptor.width, std::uint64_t{111});
    QCOMPARE(recent.back().rawDescriptor.width, std::uint64_t{102});
    QCOMPARE(recent.front().rawDescriptor.headerBytes,
             std::uint64_t{9007199254741003});
    QCOMPARE(recent.front().rawDescriptor.scalarType,
             rawviewer::domain::ScalarType::UInt32);
    QCOMPARE(recent.front().rawDescriptor.byteOrder,
             rawviewer::domain::ByteOrder::BigEndian);
    QCOMPARE(recent.front().rawDescriptor.bayerPattern,
             rawviewer::domain::BayerPattern::GBRG);
    QCOMPARE(recent.front().rawDescriptor.sensorBlackLevel, 75.5);

    auto reopened = recent[5];
    reopened.rawDescriptor.width = 11776;
    reopened.rawDescriptor.height = 8842;
    store.remember(reopened);
    recent = store.load();
    QCOMPARE(recent.size(), std::size_t{10});
    QCOMPARE(recent.front().path, reopened.path);
    QCOMPARE(recent.front().rawDescriptor.width, std::uint64_t{11776});
    QCOMPARE(std::count_if(recent.begin(), recent.end(),
        [&reopened](const rawviewer::application::RecentDocument& item) {
            return item.path == reopened.path;
        }), std::ptrdiff_t{1});

    store.clear();
    QVERIFY(store.load().empty());
}

void DecoderTest::verifiesApprovedFlatSampleWhenConfigured() {
    const QByteArray configured = qgetenv("RAWVIEWER_FLAT_SAMPLE");
    if (configured.isEmpty()) {
        QSKIP("RAWVIEWER_FLAT_SAMPLE is not configured.");
    }
    const QString path = QString::fromLocal8Bit(configured);
    QVERIFY2(QFile::exists(path), configured.constData());

    rawviewer::domain::RawDescriptor descriptor;
    descriptor.width = 11776;
    descriptor.height = 8842;
    descriptor.headerBytes = 0;
    descriptor.rowStrideBytes = 0;
    descriptor.scalarType = rawviewer::domain::ScalarType::UInt16;
    descriptor.byteOrder = rawviewer::domain::ByteOrder::LittleEndian;
    descriptor.bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    rawviewer::infrastructure::FlatRawDecoder decoder;
    const auto result = decoder.decode(makeRequest(path, descriptor));

    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.image->metadata.width, std::uint64_t{11776});
    QCOMPARE(result.image->metadata.height, std::uint64_t{8842});
    QCOMPARE(result.image->metadata.bayerPattern,
             rawviewer::domain::BayerPattern::RGGB);
    QVERIFY(result.image->preview.hasGrayscale16());
    QCOMPARE(result.image->preview.width, 11776);
    QCOMPARE(result.image->preview.height, 8842);
    QCOMPARE(result.image->preview.grayscale16StrideSamples, 11776);
    QVERIFY(!result.image->preview.grayscale16Storage);
    QCOMPARE(result.image->preview.grayscale16Pixels[0], std::uint16_t{1});
    QCOMPARE(result.image->preview.grayscale16Pixels[1], std::uint16_t{0});
    QCOMPARE(result.image->preview.grayscale16Pixels[11776],
             std::uint16_t{23734});
    QVERIFY(!result.image->signalPreview);
    QCOMPARE(result.image->pixels->sample(0, 0).value, 1.0);
    QCOMPARE(result.image->pixels->sample(1, 0).value, 0.0);
    QCOMPARE(result.image->pixels->sample(0, 1).value, 23734.0);
    QCOMPARE(result.image->pixels->sample(124, 92).value, 4142.0);
    QCOMPARE(result.image->pixels->sample(11775, 8841).value, 4219.0);

    rawviewer::application::BayerExtractRequest identityRequest;
    identityRequest.source = result.image;
    identityRequest.mask = {
        "all-8x8", 8, 8, std::vector<std::uint8_t>(64, 1)};
    QElapsedTimer identityTimer;
    identityTimer.start();
    const auto identity =
        rawviewer::application::BayerExtractService().execute(identityRequest);
    const auto identityMilliseconds = identityTimer.elapsed();
    QVERIFY2(identity.succeeded(), identity.message.c_str());
    QCOMPARE(identity.extraction->geometry.width, std::uint64_t{11776});
    QCOMPARE(identity.extraction->geometry.height, std::uint64_t{8842});
    QCOMPARE(identity.extraction->image->pixels.get(), result.image->pixels.get());
    QCOMPARE(identity.extraction->image->preview.grayscale16Pixels,
             result.image->preview.grayscale16Pixels);

    rawviewer::application::BayerExtractRequest channelRequest;
    channelRequest.source = result.image;
    channelRequest.mask = {"top-left", 2, 2, {1, 0, 0, 0}};
    QElapsedTimer channelTimer;
    channelTimer.start();
    const auto channel =
        rawviewer::application::BayerExtractService().execute(channelRequest);
    const auto channelMilliseconds = channelTimer.elapsed();
    QVERIFY2(channel.succeeded(), channel.message.c_str());
    QCOMPARE(channel.extraction->geometry.width, std::uint64_t{5888});
    QCOMPARE(channel.extraction->geometry.height, std::uint64_t{4421});
    QVERIFY(channel.extraction->image->signalPreview);
    QCOMPARE(channel.extraction->image->signalPreview->width, 1024);
    QCOMPARE(channel.extraction->image->signalPreview->height, 769);
    QCOMPARE(channel.extraction->image->pixels->sample(62, 46).value, 4142.0);
    qInfo("V0.6.1 real RAW extract: all-selected=%lld ms, single-position=%lld ms",
          static_cast<long long>(identityMilliseconds),
          static_cast<long long>(channelMilliseconds));

    rawviewer::application::PixelStatisticsRequest statisticsRequest;
    statisticsRequest.source = result.image;
    statisticsRequest.mode =
        rawviewer::application::PixelStatisticsMode::Status;
    statisticsRequest.selection = {0, 0, 11775, 8841};
    statisticsRequest.histogramBins = 256;
    statisticsRequest.progressPermille =
        std::make_shared<std::atomic_uint32_t>(0);
    const auto statistics =
        rawviewer::application::PixelStatisticsService().execute(
            statisticsRequest);
    QVERIFY2(statistics.succeeded(), statistics.message.c_str());
    QCOMPARE(statistics.summary.count, std::uint64_t{104123392});
    QCOMPARE(statistics.summary.minimum, 0.0);
    QCOMPARE(statistics.summary.maximum, 65535.0);
    QVERIFY(std::abs(statistics.summary.mean - 13043.657121994258) < 1.0e-6);
    QVERIFY(std::abs(statistics.summary.standardDeviation -
                     11863.858747775810) < 1.0e-6);
    QCOMPARE(std::accumulate(statistics.plot.y.begin(),
                             statistics.plot.y.end(),
                             0.0),
             104123392.0);
    QCOMPARE(statisticsRequest.progressPermille->load(),
             std::uint32_t{1000});
}

void DecoderTest::verifiesApprovedCameraSampleWhenConfigured() {
    const QByteArray configured = qgetenv("RAWVIEWER_CAMERA_SAMPLE");
    if (configured.isEmpty()) {
        QSKIP("RAWVIEWER_CAMERA_SAMPLE is not configured.");
    }
    const QString path = QString::fromLocal8Bit(configured);
    QVERIFY2(QFile::exists(path), configured.constData());

    rawviewer::application::OpenImageRequest request;
    request.path = nativePath(path);
    request.cancellation = std::make_shared<std::atomic_bool>(false);
    rawviewer::infrastructure::CameraRawDecoder decoder;
    const auto result = decoder.decode(request);
    QVERIFY2(result.succeeded(), result.message.c_str());
    QCOMPARE(result.image->metadata.width, std::uint64_t{11904});
    QCOMPARE(result.image->metadata.height, std::uint64_t{8842});
    QCOMPARE(result.image->metadata.scalarType,
             rawviewer::domain::ScalarType::UInt16);
    QCOMPARE(result.image->metadata.bayerPattern,
             rawviewer::domain::BayerPattern::RGGB);
    QVERIFY(std::abs(result.image->metadata.sensorBlackLevel - 4093.5) < 0.01);
    QCOMPARE(result.image->metadata.whiteLevel, 65535.0);
    QVERIFY(QString::fromStdString(result.image->metadata.camera)
                .contains("Hasselblad", Qt::CaseInsensitive));
    QVERIFY(QString::fromStdString(result.image->metadata.camera)
                .contains("X2D 100C", Qt::CaseInsensitive));
}

QTEST_APPLESS_MAIN(DecoderTest)
#include "decoder_test.moc"
