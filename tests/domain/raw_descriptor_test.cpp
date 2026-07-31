#include "domain/raw_descriptor.h"
#include "domain/display_mapping.h"

#include <QTest>

#include <limits>

using rawviewer::domain::RawDescriptor;
using rawviewer::domain::ScalarType;
using rawviewer::domain::validateDescriptor;

class RawDescriptorTest final : public QObject {
    Q_OBJECT

private slots:
    void acceptsTightUInt16();
    void acceptsHeaderAndStride();
    void rejectsTruncation();
    void rejectsSmallStride();
    void rejectsOverflow();
    void validatesDisplayMapping();
    void mapsDisplayRange();
};

void RawDescriptorTest::acceptsTightUInt16() {
    RawDescriptor descriptor;
    descriptor.width = 4;
    descriptor.height = 3;
    descriptor.scalarType = ScalarType::UInt16;

    const auto result = validateDescriptor(descriptor, 24);
    QVERIFY(result.valid);
    QCOMPARE(result.minimumRowBytes, std::uint64_t{8});
    QCOMPARE(result.effectiveRowStride, std::uint64_t{8});
    QCOMPARE(result.requiredFileBytes, std::uint64_t{24});
}

void RawDescriptorTest::acceptsHeaderAndStride() {
    RawDescriptor descriptor;
    descriptor.width = 3;
    descriptor.height = 2;
    descriptor.headerBytes = 16;
    descriptor.rowStrideBytes = 8;
    descriptor.scalarType = ScalarType::UInt16;

    const auto result = validateDescriptor(descriptor, 32);
    QVERIFY(result.valid);
    QCOMPARE(result.requiredFileBytes, std::uint64_t{32});
}

void RawDescriptorTest::rejectsTruncation() {
    RawDescriptor descriptor;
    descriptor.width = 4;
    descriptor.height = 3;
    descriptor.scalarType = ScalarType::UInt16;

    const auto result = validateDescriptor(descriptor, 23);
    QVERIFY(!result.valid);
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("raw.file_truncated"));
}

void RawDescriptorTest::rejectsSmallStride() {
    RawDescriptor descriptor;
    descriptor.width = 4;
    descriptor.height = 3;
    descriptor.rowStrideBytes = 7;
    descriptor.scalarType = ScalarType::UInt16;

    const auto result = validateDescriptor(descriptor, 100);
    QVERIFY(!result.valid);
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("raw.invalid_stride"));
}

void RawDescriptorTest::rejectsOverflow() {
    RawDescriptor descriptor;
    descriptor.width = std::numeric_limits<std::uint64_t>::max();
    descriptor.height = 2;
    descriptor.scalarType = ScalarType::UInt32;

    const auto result =
        validateDescriptor(descriptor, std::numeric_limits<std::uint64_t>::max());
    QVERIFY(!result.valid);
    QCOMPARE(QString::fromStdString(result.errorCode),
             QStringLiteral("raw.size_overflow"));
}

void RawDescriptorTest::validatesDisplayMapping() {
    rawviewer::domain::DisplayMapping mapping;
    mapping.blackPoint = 100.0;
    mapping.whitePoint = 100.0;
    QVERIFY(!rawviewer::domain::validateDisplayMapping(mapping).valid);

    mapping.whitePoint = 1000.0;
    mapping.gamma = 0.0;
    QVERIFY(!rawviewer::domain::validateDisplayMapping(mapping).valid);

    mapping.gamma = 2.2;
    QVERIFY(rawviewer::domain::validateDisplayMapping(mapping).valid);
}

void RawDescriptorTest::mapsDisplayRange() {
    rawviewer::domain::DisplayMapping mapping;
    mapping.blackPoint = 100.0;
    mapping.whitePoint = 1100.0;
    mapping.gamma = 1.0;
    QCOMPARE(rawviewer::domain::mapDisplayValue(0.0, mapping), 0.0);
    QCOMPARE(rawviewer::domain::mapDisplayValue(100.0, mapping), 0.0);
    QCOMPARE(rawviewer::domain::mapDisplayValue(600.0, mapping), 0.5);
    QCOMPARE(rawviewer::domain::mapDisplayValue(1100.0, mapping), 1.0);
    QCOMPARE(rawviewer::domain::mapDisplayValue(2000.0, mapping), 1.0);
}

QTEST_APPLESS_MAIN(RawDescriptorTest)
#include "raw_descriptor_test.moc"
