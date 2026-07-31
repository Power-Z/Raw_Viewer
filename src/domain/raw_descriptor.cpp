#include "domain/raw_descriptor.h"

#include <limits>

namespace rawviewer::domain {
namespace {

bool checkedMultiply(std::uint64_t left,
                     std::uint64_t right,
                     std::uint64_t& result) noexcept {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

bool checkedAdd(std::uint64_t left,
                std::uint64_t right,
                std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

} // namespace

DescriptorValidation validateDescriptor(const RawDescriptor& descriptor,
                                        std::uint64_t fileSize) {
    DescriptorValidation result;
    switch (descriptor.scalarType) {
    case ScalarType::UInt8:
        result.bytesPerSample = 1;
        break;
    case ScalarType::UInt16:
        result.bytesPerSample = 2;
        break;
    case ScalarType::UInt32:
    case ScalarType::Float32:
        result.bytesPerSample = 4;
        break;
    }

    if (descriptor.width == 0 || descriptor.height == 0) {
        result.errorCode = "raw.invalid_dimensions";
        result.message = "RAW width and height must both be greater than zero.";
        return result;
    }

    if (!checkedMultiply(descriptor.width,
                         result.bytesPerSample,
                         result.minimumRowBytes)) {
        result.errorCode = "raw.size_overflow";
        result.message = "RAW row size overflows 64-bit arithmetic.";
        return result;
    }

    result.effectiveRowStride =
        descriptor.rowStrideBytes == 0 ? result.minimumRowBytes
                                       : descriptor.rowStrideBytes;
    if (result.effectiveRowStride < result.minimumRowBytes) {
        result.errorCode = "raw.invalid_stride";
        result.message = "RAW row stride is smaller than the pixel row.";
        return result;
    }

    std::uint64_t dataBytes = 0;
    if (!checkedMultiply(result.effectiveRowStride,
                         descriptor.height,
                         dataBytes) ||
        !checkedAdd(descriptor.headerBytes,
                    dataBytes,
                    result.requiredFileBytes)) {
        result.errorCode = "raw.size_overflow";
        result.message = "RAW file size overflows 64-bit arithmetic.";
        return result;
    }

    if (result.requiredFileBytes > fileSize) {
        result.errorCode = "raw.file_truncated";
        result.message = "The file is shorter than the supplied RAW parameters require.";
        return result;
    }

    result.valid = true;
    return result;
}

const char* toString(ScalarType value) noexcept {
    switch (value) {
    case ScalarType::UInt8: return "UInt8";
    case ScalarType::UInt16: return "UInt16";
    case ScalarType::UInt32: return "UInt32";
    case ScalarType::Float32: return "Float32";
    }
    return "Unknown";
}

const char* toString(ByteOrder value) noexcept {
    return value == ByteOrder::LittleEndian ? "Little endian" : "Big endian";
}

const char* toString(BayerPattern value) noexcept {
    switch (value) {
    case BayerPattern::None: return "None";
    case BayerPattern::RGGB: return "RGGB";
    case BayerPattern::BGGR: return "BGGR";
    case BayerPattern::GRBG: return "GRBG";
    case BayerPattern::GBRG: return "GBRG";
    }
    return "Unknown";
}

const char* toString(BayerChannel value) noexcept {
    switch (value) {
    case BayerChannel::None: return "";
    case BayerChannel::R: return "R";
    case BayerChannel::Gr: return "Gr";
    case BayerChannel::Gb: return "Gb";
    case BayerChannel::B: return "B";
    }
    return "";
}

BayerChannel bayerChannelAt(BayerPattern pattern,
                            std::uint64_t x,
                            std::uint64_t y) noexcept {
    const bool oddX = (x & 1U) != 0;
    const bool oddY = (y & 1U) != 0;
    switch (pattern) {
    case BayerPattern::RGGB:
        if (!oddY) return oddX ? BayerChannel::Gr : BayerChannel::R;
        return oddX ? BayerChannel::B : BayerChannel::Gb;
    case BayerPattern::BGGR:
        if (!oddY) return oddX ? BayerChannel::Gb : BayerChannel::B;
        return oddX ? BayerChannel::R : BayerChannel::Gr;
    case BayerPattern::GRBG:
        if (!oddY) return oddX ? BayerChannel::R : BayerChannel::Gr;
        return oddX ? BayerChannel::Gb : BayerChannel::B;
    case BayerPattern::GBRG:
        if (!oddY) return oddX ? BayerChannel::B : BayerChannel::Gb;
        return oddX ? BayerChannel::Gr : BayerChannel::R;
    case BayerPattern::None:
        return BayerChannel::None;
    }
    return BayerChannel::None;
}

std::optional<BayerCoordinate> bayerChannelOffset(
    BayerPattern pattern,
    BayerChannel channel) noexcept {
    if (channel == BayerChannel::None) {
        return std::nullopt;
    }
    for (std::uint64_t y = 0; y < 2; ++y) {
        for (std::uint64_t x = 0; x < 2; ++x) {
            if (bayerChannelAt(pattern, x, y) == channel) {
                return BayerCoordinate{x, y};
            }
        }
    }
    return std::nullopt;
}

std::optional<BayerCoordinate> bayerChannelToSource(
    BayerPattern pattern,
    BayerChannel channel,
    std::uint64_t channelX,
    std::uint64_t channelY) noexcept {
    const auto offset = bayerChannelOffset(pattern, channel);
    if (!offset ||
        channelX > (std::numeric_limits<std::uint64_t>::max() - offset->x) / 2 ||
        channelY > (std::numeric_limits<std::uint64_t>::max() - offset->y) / 2) {
        return std::nullopt;
    }
    return BayerCoordinate{
        offset->x + channelX * 2,
        offset->y + channelY * 2
    };
}

std::optional<BayerCoordinate> sourceToBayerChannel(
    BayerPattern pattern,
    BayerChannel channel,
    std::uint64_t sourceX,
    std::uint64_t sourceY) noexcept {
    const auto offset = bayerChannelOffset(pattern, channel);
    if (!offset || sourceX < offset->x || sourceY < offset->y ||
        ((sourceX - offset->x) & 1U) != 0 ||
        ((sourceY - offset->y) & 1U) != 0) {
        return std::nullopt;
    }
    return BayerCoordinate{
        (sourceX - offset->x) / 2,
        (sourceY - offset->y) / 2
    };
}

} // namespace rawviewer::domain
