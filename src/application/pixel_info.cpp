#include "application/pixel_info.h"

#include <cmath>

namespace rawviewer::application {
namespace {

std::uint8_t mapByte(double value,
                     const domain::DisplayMapping& mapping) noexcept {
    return static_cast<std::uint8_t>(std::round(
        domain::mapDisplayValue(value, mapping) * 255.0));
}

} // namespace

PixelInfo queryPixelInfo(const DecodedImage& image,
                         const domain::DisplayMapping& mapping,
                         std::uint64_t x,
                         std::uint64_t y) noexcept {
    PixelInfo result;
    result.x = x;
    result.y = y;
    if (!image.pixels || x >= image.metadata.width || y >= image.metadata.height) {
        return result;
    }
    const auto sample = image.pixels->sample(x, y);
    if (!sample.valid) {
        return result;
    }
    result.valid = true;
    result.originalValue = sample.value;
    result.processedValue = domain::mapDisplayValue(sample.value, mapping);
    result.channel = domain::bayerChannelAt(image.metadata.bayerPattern, x, y);
    if (sample.rgbValid) {
        result.rgbValid = true;
        result.red = mapByte(sample.red, mapping);
        result.green = mapByte(sample.green, mapping);
        result.blue = mapByte(sample.blue, mapping);
    } else if (image.metadata.kind != ImageKind::FlatRaw) {
        const auto gray = mapByte(sample.value, mapping);
        result.rgbValid = true;
        result.red = gray;
        result.green = gray;
        result.blue = gray;
    }
    return result;
}

} // namespace rawviewer::application
