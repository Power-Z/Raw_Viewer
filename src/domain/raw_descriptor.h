#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rawviewer::domain {

enum class ScalarType {
    UInt8,
    UInt16,
    UInt32,
    Float32
};

enum class ByteOrder {
    LittleEndian,
    BigEndian
};

enum class BayerPattern {
    None,
    RGGB,
    BGGR,
    GRBG,
    GBRG
};

enum class BayerChannel {
    None,
    R,
    Gr,
    Gb,
    B
};

struct BayerCoordinate {
    std::uint64_t x = 0;
    std::uint64_t y = 0;

    bool operator==(const BayerCoordinate&) const = default;
};

struct RawDescriptor {
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    std::uint64_t headerBytes = 0;
    std::uint64_t rowStrideBytes = 0;
    ScalarType scalarType = ScalarType::UInt16;
    ByteOrder byteOrder = ByteOrder::LittleEndian;
    BayerPattern bayerPattern = BayerPattern::RGGB;
    double sensorBlackLevel = 0.0;
};

struct DescriptorValidation {
    bool valid = false;
    std::uint64_t bytesPerSample = 0;
    std::uint64_t minimumRowBytes = 0;
    std::uint64_t effectiveRowStride = 0;
    std::uint64_t requiredFileBytes = 0;
    std::string errorCode;
    std::string message;
};

DescriptorValidation validateDescriptor(const RawDescriptor& descriptor,
                                        std::uint64_t fileSize);

const char* toString(ScalarType value) noexcept;
const char* toString(ByteOrder value) noexcept;
const char* toString(BayerPattern value) noexcept;
const char* toString(BayerChannel value) noexcept;
BayerChannel bayerChannelAt(BayerPattern pattern,
                            std::uint64_t x,
                            std::uint64_t y) noexcept;
std::optional<BayerCoordinate> bayerChannelOffset(
    BayerPattern pattern,
    BayerChannel channel) noexcept;
std::optional<BayerCoordinate> bayerChannelToSource(
    BayerPattern pattern,
    BayerChannel channel,
    std::uint64_t channelX,
    std::uint64_t channelY) noexcept;
std::optional<BayerCoordinate> sourceToBayerChannel(
    BayerPattern pattern,
    BayerChannel channel,
    std::uint64_t sourceX,
    std::uint64_t sourceY) noexcept;

} // namespace rawviewer::domain
