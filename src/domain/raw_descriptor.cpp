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

} // namespace rawviewer::domain
