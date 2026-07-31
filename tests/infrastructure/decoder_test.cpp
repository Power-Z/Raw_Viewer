#include "infrastructure/camera_raw_decoder.h"
#include "infrastructure/flat_raw_decoder.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <filesystem>
#include <memory>

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
    void decodesBigEndianUInt32();
    void decodesFloat32();
    void refusesTruncatedFile();
    void cameraContainerWinsBySignature();
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
    QVERIFY(QString::fromStdString(result.image->metadata.camera)
                .contains("Hasselblad", Qt::CaseInsensitive));
    QVERIFY(QString::fromStdString(result.image->metadata.camera)
                .contains("X2D 100C", Qt::CaseInsensitive));
}

QTEST_APPLESS_MAIN(DecoderTest)
#include "decoder_test.moc"
