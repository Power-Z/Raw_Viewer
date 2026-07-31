#pragma once

#include "application/image_decoder.h"

namespace rawviewer::infrastructure {

class QtImageDecoder final : public application::IImageDecoder {
public:
    application::ProbeStrength probe(
        const std::filesystem::path& path,
        std::span<const std::byte> signature,
        bool hasFlatDescriptor) const override;
    application::DecodeResult decode(
        const application::OpenImageRequest& request) const override;
};

} // namespace rawviewer::infrastructure
