#include "application/preview_renderer.h"

#include <cmath>

namespace rawviewer::application {
namespace {

std::uint8_t mapByte(double value,
                     const domain::DisplayMapping& mapping) {
    return static_cast<std::uint8_t>(std::round(
        domain::mapDisplayValue(value, mapping) * 255.0));
}

} // namespace

std::shared_ptr<DecodedImage> PreviewRenderer::render(
    const std::shared_ptr<const DecodedImage>& original,
    const domain::DisplayMapping& mapping) {
    if (!original) {
        return {};
    }
    const auto validation = domain::validateDisplayMapping(mapping);
    if (!validation.valid) {
        return {};
    }

    auto rendered = std::make_shared<DecodedImage>(*original);
    if (original->signalPreview &&
        original->signalPreview->values.size() ==
            static_cast<std::size_t>(original->signalPreview->width) *
                original->signalPreview->height) {
        const auto& signal = *original->signalPreview;
        rendered->preview.width = signal.width;
        rendered->preview.height = signal.height;
        rendered->preview.rgba.resize(signal.values.size() * 4);
        for (std::size_t index = 0; index < signal.values.size(); ++index) {
            const auto gray = mapByte(signal.values[index], mapping);
            const auto output = index * 4;
            rendered->preview.rgba[output] = gray;
            rendered->preview.rgba[output + 1] = gray;
            rendered->preview.rgba[output + 2] = gray;
            rendered->preview.rgba[output + 3] = 255;
        }
        return rendered;
    }

    rendered->preview = original->preview;
    for (std::size_t index = 0;
         index + 3 < rendered->preview.rgba.size();
         index += 4) {
        rendered->preview.rgba[index] =
            mapByte(original->preview.rgba[index], mapping);
        rendered->preview.rgba[index + 1] =
            mapByte(original->preview.rgba[index + 1], mapping);
        rendered->preview.rgba[index + 2] =
            mapByte(original->preview.rgba[index + 2], mapping);
    }
    return rendered;
}

} // namespace rawviewer::application
