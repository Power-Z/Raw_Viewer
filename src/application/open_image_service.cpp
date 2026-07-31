#include "application/open_image_service.h"

#include <array>
#include <fstream>

namespace rawviewer::application {

OpenImageService::OpenImageService(
    std::vector<std::shared_ptr<const IImageDecoder>> decoders)
    : decoders_(std::move(decoders)) {}

DecodeResult OpenImageService::execute(const OpenImageRequest& request) const {
    if (request.cancellation && request.cancellation->load()) {
        return {nullptr, "task.cancelled", "The open operation was cancelled."};
    }

    std::array<std::byte, 32> signature{};
    std::size_t signatureSize = 0;
    std::ifstream stream(request.path, std::ios::binary);
    if (!stream) {
        return {nullptr, "file.open_failed", "The selected file cannot be opened."};
    }
    stream.read(reinterpret_cast<char*>(signature.data()),
                static_cast<std::streamsize>(signature.size()));
    signatureSize = static_cast<std::size_t>(stream.gcount());

    const IImageDecoder* selected = nullptr;
    ProbeStrength selectedStrength = ProbeStrength::None;
    for (const auto& decoder : decoders_) {
        const auto strength = decoder->probe(
            request.path,
            std::span<const std::byte>(signature.data(), signatureSize),
            request.flatRawDescriptor.has_value());
        if (static_cast<int>(strength) > static_cast<int>(selectedStrength)) {
            selected = decoder.get();
            selectedStrength = strength;
        }
    }

    if (!selected) {
        return {nullptr,
                "image.unsupported_format",
                "The file format is not supported by this version."};
    }
    return selected->decode(request);
}

} // namespace rawviewer::application
