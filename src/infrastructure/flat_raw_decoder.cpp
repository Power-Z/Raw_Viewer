#include "infrastructure/flat_raw_decoder.h"

#include "domain/raw_descriptor.h"

#include <QFile>
#include <QString>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace rawviewer::infrastructure {
namespace {

QString pathToQString(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::string lowerExtension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension;
}

std::uint16_t readU16(const unsigned char* bytes,
                      domain::ByteOrder order) noexcept {
    if (order == domain::ByteOrder::LittleEndian) {
        return static_cast<std::uint16_t>(bytes[0]) |
               (static_cast<std::uint16_t>(bytes[1]) << 8);
    }
    return (static_cast<std::uint16_t>(bytes[0]) << 8) |
           static_cast<std::uint16_t>(bytes[1]);
}

std::uint32_t readU32(const unsigned char* bytes,
                      domain::ByteOrder order) noexcept {
    if (order == domain::ByteOrder::LittleEndian) {
        return static_cast<std::uint32_t>(bytes[0]) |
               (static_cast<std::uint32_t>(bytes[1]) << 8) |
               (static_cast<std::uint32_t>(bytes[2]) << 16) |
               (static_cast<std::uint32_t>(bytes[3]) << 24);
    }
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

class MappedFlatPixelSource final : public application::IPixelSource {
public:
    MappedFlatPixelSource(std::unique_ptr<QFile> file,
                          unsigned char* mapping,
                          domain::RawDescriptor descriptor,
                          domain::DescriptorValidation validation)
        : file_(std::move(file)),
          mapping_(mapping),
          descriptor_(descriptor),
          validation_(validation) {}

    ~MappedFlatPixelSource() override {
        if (mapping_) {
            file_->unmap(mapping_);
        }
    }

    std::uint64_t width() const noexcept override { return descriptor_.width; }
    std::uint64_t height() const noexcept override { return descriptor_.height; }

    application::PixelSample sample(std::uint64_t x,
                                    std::uint64_t y) const noexcept override {
        if (!mapping_ || x >= width() || y >= height()) {
            return {};
        }
        const auto offset = descriptor_.headerBytes +
                            y * validation_.effectiveRowStride +
                            x * validation_.bytesPerSample;
        const auto* bytes = mapping_ + offset;
        switch (descriptor_.scalarType) {
        case domain::ScalarType::UInt8:
            return {true, static_cast<double>(*bytes)};
        case domain::ScalarType::UInt16:
            return {true, static_cast<double>(readU16(bytes, descriptor_.byteOrder))};
        case domain::ScalarType::UInt32:
            return {true, static_cast<double>(readU32(bytes, descriptor_.byteOrder))};
        case domain::ScalarType::Float32: {
            const auto bits = readU32(bytes, descriptor_.byteOrder);
            float value = 0.0F;
            static_assert(sizeof(value) == sizeof(bits));
            std::memcpy(&value, &bits, sizeof(value));
            return {std::isfinite(value), static_cast<double>(value)};
        }
        }
        return {};
    }

private:
    std::unique_ptr<QFile> file_;
    unsigned char* mapping_ = nullptr;
    domain::RawDescriptor descriptor_;
    domain::DescriptorValidation validation_;
};

std::pair<int, int> previewSize(std::uint64_t width, std::uint64_t height) {
    constexpr double maxSide = 2048.0;
    const double scale = std::min(1.0,
                                  maxSide / static_cast<double>(
                                                std::max(width, height)));
    return {
        std::max(1, static_cast<int>(std::round(width * scale))),
        std::max(1, static_cast<int>(std::round(height * scale)))
    };
}

double integerMaximum(domain::ScalarType type) {
    switch (type) {
    case domain::ScalarType::UInt8: return 255.0;
    case domain::ScalarType::UInt16: return 65535.0;
    case domain::ScalarType::UInt32:
        return static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    case domain::ScalarType::Float32: return 1.0;
    }
    return 1.0;
}

} // namespace

application::ProbeStrength FlatRawDecoder::probe(
    const std::filesystem::path& path,
    std::span<const std::byte>,
    bool hasFlatDescriptor) const {
    if (!hasFlatDescriptor) {
        return application::ProbeStrength::None;
    }
    const auto extension = lowerExtension(path);
    return extension == ".raw" || extension == ".bin"
               ? application::ProbeStrength::Fallback
               : application::ProbeStrength::None;
}

application::DecodeResult FlatRawDecoder::decode(
    const application::OpenImageRequest& request) const {
    if (!request.flatRawDescriptor) {
        return {nullptr, "raw.parameters_required", "Flat RAW parameters are required."};
    }

    auto file = std::make_unique<QFile>(pathToQString(request.path));
    if (!file->open(QIODevice::ReadOnly)) {
        return {nullptr, "file.open_failed", "The RAW file cannot be opened read-only."};
    }
    const auto fileSize = static_cast<std::uint64_t>(file->size());
    const auto validation =
        domain::validateDescriptor(*request.flatRawDescriptor, fileSize);
    if (!validation.valid) {
        return {nullptr, validation.errorCode, validation.message};
    }
    if (fileSize > static_cast<std::uint64_t>(
                       std::numeric_limits<qsizetype>::max())) {
        return {nullptr,
                "raw.mapping_too_large",
                "This RAW is too large for the current mapping implementation."};
    }

    auto* mapping = file->map(0, static_cast<qsizetype>(fileSize));
    if (!mapping) {
        return {nullptr, "raw.map_failed", "The RAW file could not be mapped read-only."};
    }

    auto pixels = std::make_shared<MappedFlatPixelSource>(
        std::move(file),
        mapping,
        *request.flatRawDescriptor,
        validation);
    const auto [previewWidth, previewHeight] =
        previewSize(pixels->width(), pixels->height());

    double minimum = 0.0;
    double maximum = integerMaximum(request.flatRawDescriptor->scalarType);
    if (request.flatRawDescriptor->scalarType == domain::ScalarType::Float32) {
        minimum = std::numeric_limits<double>::infinity();
        maximum = -std::numeric_limits<double>::infinity();
        for (int y = 0; y < previewHeight; ++y) {
            const auto sourceY = static_cast<std::uint64_t>(
                (static_cast<long double>(y) * pixels->height()) / previewHeight);
            for (int x = 0; x < previewWidth; ++x) {
                const auto sourceX = static_cast<std::uint64_t>(
                    (static_cast<long double>(x) * pixels->width()) / previewWidth);
                const auto value = pixels->sample(sourceX, sourceY);
                if (value.valid) {
                    minimum = std::min(minimum, value.value);
                    maximum = std::max(maximum, value.value);
                }
            }
        }
        if (!std::isfinite(minimum) || maximum <= minimum) {
            minimum = 0.0;
            maximum = 1.0;
        }
    }

    auto decoded = std::make_shared<application::DecodedImage>();
    decoded->metadata.kind = application::ImageKind::FlatRaw;
    decoded->metadata.width = pixels->width();
    decoded->metadata.height = pixels->height();
    decoded->metadata.scalarType = request.flatRawDescriptor->scalarType;
    decoded->metadata.bayerPattern = request.flatRawDescriptor->bayerPattern;
    decoded->metadata.format = "Flat RAW";
    decoded->metadata.details =
        std::string(domain::toString(request.flatRawDescriptor->byteOrder));
    decoded->pixels = pixels;
    decoded->preview.width = previewWidth;
    decoded->preview.height = previewHeight;
    decoded->preview.rgba.resize(
        static_cast<std::size_t>(previewWidth) * previewHeight * 4);

    const double range = maximum - minimum;
    for (int y = 0; y < previewHeight; ++y) {
        if (request.cancellation && request.cancellation->load()) {
            return {nullptr, "task.cancelled", "The open operation was cancelled."};
        }
        const auto sourceY = static_cast<std::uint64_t>(
            (static_cast<long double>(y) * pixels->height()) / previewHeight);
        for (int x = 0; x < previewWidth; ++x) {
            const auto sourceX = static_cast<std::uint64_t>(
                (static_cast<long double>(x) * pixels->width()) / previewWidth);
            const auto sample = pixels->sample(sourceX, sourceY);
            const double normalized =
                sample.valid ? std::clamp((sample.value - minimum) / range, 0.0, 1.0)
                             : 0.0;
            const auto gray = static_cast<std::uint8_t>(
                std::round(normalized * 255.0));
            const auto index =
                (static_cast<std::size_t>(y) * previewWidth + x) * 4;
            decoded->preview.rgba[index] = gray;
            decoded->preview.rgba[index + 1] = gray;
            decoded->preview.rgba[index + 2] = gray;
            decoded->preview.rgba[index + 3] = 255;
        }
    }
    return {decoded, {}, {}};
}

} // namespace rawviewer::infrastructure
