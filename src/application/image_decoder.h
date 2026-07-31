#pragma once

#include "application/image_types.h"
#include "domain/raw_descriptor.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace rawviewer::application {

enum class ProbeStrength {
    None = 0,
    Fallback = 1,
    Definitive = 2
};

struct OpenImageRequest {
    std::filesystem::path path;
    std::optional<domain::RawDescriptor> flatRawDescriptor;
    std::shared_ptr<std::atomic_bool> cancellation;
};

struct DecodeResult {
    std::shared_ptr<DecodedImage> image;
    std::string errorCode;
    std::string message;

    bool succeeded() const noexcept { return static_cast<bool>(image); }
};

class IImageDecoder {
public:
    virtual ~IImageDecoder() = default;
    virtual ProbeStrength probe(const std::filesystem::path& path,
                                std::span<const std::byte> signature,
                                bool hasFlatDescriptor) const = 0;
    virtual DecodeResult decode(const OpenImageRequest& request) const = 0;
};

} // namespace rawviewer::application
