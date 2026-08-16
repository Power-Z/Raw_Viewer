#include "infrastructure/bayer_csv_exporter.h"
#include "infrastructure/camera_raw_decoder.h"
#include "infrastructure/flat_raw_decoder.h"
#include "infrastructure/qt_image_decoder.h"

#include "application/bayer_extract.h"
#include "application/pixel_info.h"
#include "application/pixel_statistics.h"

#include <QFile>
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
    const QByteArray bytes = QByteArray::fromRawData("\0\1\2\3\4\5\6\7", 8);
    QCOMPARE(sourceFile.write(bytes), bytes.size());
    sourceFile.close();

    rawviewer::domain::RawDescriptor descriptor;
    descriptor.width = 4;
    descriptor.height = 2;
    descriptor.scalarType = rawviewer::domain::ScalarType::UInt8;
    descriptor.bayerPattern = rawviewer::domain::BayerPattern::RGGB;
    rawviewer::infrastructure::FlatRawDecoder decoder;
    const auto decoded = decoder.decode(makeRequest(sourcePath, descriptor));
    QVERIFY2(decoded.succeeded(), decoded.message.c_str());

    rawviewer::application::BayerExtractRequest extractRequest;
    extractRequest.source = decoded.image;
    extractRequest.channel = rawviewer::domain::BayerChannel::Gr;
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
    QCOMPARE(exported.exportedSamples, std::uint64_t{2});

    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(csv.readAll(),
             QByteArray("channel_x,channel_y,source_x,source_y,value\n"
                        "0,0,1,0,1\n"
                        "1,0,3,0,3\n"));
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
