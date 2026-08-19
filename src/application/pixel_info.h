#pragma once

#include "application/image_types.h"
#include "domain/display_mapping.h"

#include <cstdint>

namespace rawviewer::application {

struct PixelInfo {
    bool valid = false;
    std::uint64_t x = 0;
    std::uint64_t y = 0;
    double originalValue = 0.0;
    double processedValue = 0.0;
    bool rgbValid = false;
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    domain::BayerChannel channel = domain::BayerChannel::None;
};

PixelInfo queryPixelInfo(const DecodedImage& image,
                         const domain::DisplayMapping& mapping,
                         std::uint64_t x,
                         std::uint64_t y) noexcept;

} // namespace rawviewer::application
