#include "application/bayer_extract.h"

#include "domain/display_mapping.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <sstream>

namespace rawviewer::application {
namespace {

constexpr int previewMaximumSide = 2048;

bool checkedAdd(std::uint64_t left,
                std::uint64_t right,
                std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

std::uint64_t firstWithParity(std::uint64_t start,
                              std::uint64_t parity) noexcept {
    return (start & 1U) == parity ? start : start + 1;
}

std::uint64_t sampleCount(std::uint64_t first,
                          std::uint64_t endExclusive) noexcept {
    if (first >= endExclusive) {
        return 0;
    }
    return 1 + (endExclusive - 1 - first) / 2;
}

std::pair<int, int> previewSize(std::uint64_t width,
                                std::uint64_t height) {
    const double scale = std::min(
        1.0,
        static_cast<double>(previewMaximumSide) /
            static_cast<double>(std::max(width, height)));
    return {
        std::max(1, static_cast<int>(std::round(width * scale))),
        std::max(1, static_cast<int>(std::round(height * scale)))
    };
}

class BayerPlanePixelSource final : public IPixelSource {
public:
    BayerPlanePixelSource(std::shared_ptr<const IPixelSource> source,
                          BayerPlaneGeometry geometry)
        : source_(std::move(source)), geometry_(geometry) {}

    std::uint64_t width() const noexcept override {
        return geometry_.width;
    }

    std::uint64_t height() const noexcept override {
        return geometry_.height;
    }

    PixelSample sample(std::uint64_t x,
                       std::uint64_t y) const noexcept override {
        const auto source = geometry_.sourceCoordinate(x, y);
        return source ? source_->sample(source->x, source->y) : PixelSample{};
    }

private:
    std::shared_ptr<const IPixelSource> source_;
    BayerPlaneGeometry geometry_;
};

domain::DisplayMapping previewMapping(const ImageMetadata& metadata) {
    domain::DisplayMapping mapping;
    mapping.blackPoint = metadata.sensorBlackLevel;
    mapping.whitePoint = metadata.whiteLevel;
    mapping.gamma = 2.2;
    if (!domain::validateDisplayMapping(mapping).valid) {
        mapping.blackPoint = 0.0;
        mapping.whitePoint = 1.0;
        mapping.gamma = 1.0;
    }
    return mapping;
}

} // namespace

std::optional<domain::BayerCoordinate> BayerPlaneGeometry::sourceCoordinate(
    std::uint64_t channelX,
    std::uint64_t channelY) const noexcept {
    if (channelX >= width || channelY >= height ||
        channelX > (std::numeric_limits<std::uint64_t>::max() - sourceOriginX) / 2 ||
        channelY > (std::numeric_limits<std::uint64_t>::max() - sourceOriginY) / 2) {
        return std::nullopt;
    }
    return domain::BayerCoordinate{
        sourceOriginX + channelX * 2,
        sourceOriginY + channelY * 2
    };
}

std::optional<domain::BayerCoordinate> BayerPlaneGeometry::channelCoordinate(
    std::uint64_t sourceX,
    std::uint64_t sourceY) const noexcept {
    if (sourceX < sourceOriginX || sourceY < sourceOriginY ||
        ((sourceX - sourceOriginX) & 1U) != 0 ||
        ((sourceY - sourceOriginY) & 1U) != 0) {
        return std::nullopt;
    }
    const domain::BayerCoordinate result{
        (sourceX - sourceOriginX) / 2,
        (sourceY - sourceOriginY) / 2
    };
    if (result.x >= width || result.y >= height) {
        return std::nullopt;
    }
    return result;
}

BayerExtractResult BayerExtractService::execute(
    const BayerExtractRequest& request) const {
    if (!request.source || !request.source->pixels) {
        return {nullptr,
                "bayer.no_source",
                "A decoded pixel source is required for Bayer extraction."};
    }
    if (request.source->metadata.bayerPattern == domain::BayerPattern::None) {
        return {nullptr,
                "bayer.pattern_required",
                "Bayer extraction requires an RGGB, BGGR, GRBG, or GBRG source."};
    }
    const auto offset = domain::bayerChannelOffset(
        request.source->metadata.bayerPattern, request.channel);
    if (!offset) {
        return {nullptr,
                "bayer.invalid_channel",
                "Select one of the R, Gr, Gb, or B Bayer channels."};
    }

    const PixelRegion region = request.sourceRegion.value_or(PixelRegion{
        0,
        0,
        request.source->metadata.width,
        request.source->metadata.height
    });
    std::uint64_t endX = 0;
    std::uint64_t endY = 0;
    if (region.width == 0 || region.height == 0 ||
        !checkedAdd(region.x, region.width, endX) ||
        !checkedAdd(region.y, region.height, endY) ||
        endX > request.source->metadata.width ||
        endY > request.source->metadata.height) {
        return {nullptr,
                "bayer.invalid_region",
                "The Bayer extraction region must be non-empty and inside the source image."};
    }

    BayerPlaneGeometry geometry;
    geometry.sourceRegion = region;
    geometry.pattern = request.source->metadata.bayerPattern;
    geometry.channel = request.channel;
    geometry.sourceOriginX = firstWithParity(region.x, offset->x);
    geometry.sourceOriginY = firstWithParity(region.y, offset->y);
    geometry.width = sampleCount(geometry.sourceOriginX, endX);
    geometry.height = sampleCount(geometry.sourceOriginY, endY);
    if (geometry.width == 0 || geometry.height == 0) {
        return {nullptr,
                "bayer.empty_channel",
                "The selected region contains no pixels from this Bayer channel."};
    }

    if (request.cancellation && request.cancellation->load()) {
        return {nullptr, "task.cancelled", "The Bayer extraction was cancelled."};
    }

    auto extracted = std::make_shared<DecodedImage>();
    extracted->metadata = request.source->metadata;
    extracted->metadata.width = geometry.width;
    extracted->metadata.height = geometry.height;
    extracted->metadata.bayerPattern = domain::BayerPattern::None;
    extracted->metadata.format = request.source->metadata.format +
        " / Bayer " + domain::toString(request.channel);
    std::ostringstream details;
    details << "Bayer " << domain::toString(request.channel)
            << " from " << domain::toString(geometry.pattern)
            << ", source ROI " << region.x << ',' << region.y << ' '
            << region.width << 'x' << region.height
            << ", origin " << geometry.sourceOriginX << ','
            << geometry.sourceOriginY << ", step 2x2";
    extracted->metadata.details = details.str();
    extracted->pixels = std::make_shared<BayerPlanePixelSource>(
        request.source->pixels, geometry);

    const auto [previewWidth, previewHeight] =
        previewSize(geometry.width, geometry.height);
    extracted->preview.width = previewWidth;
    extracted->preview.height = previewHeight;
    extracted->preview.rgba.resize(
        static_cast<std::size_t>(previewWidth) * previewHeight * 4);
    auto signal = std::make_shared<SignalPreview>();
    signal->width = previewWidth;
    signal->height = previewHeight;
    signal->values.resize(
        static_cast<std::size_t>(previewWidth) * previewHeight);
    extracted->signalPreview = signal;
    const auto mapping = previewMapping(extracted->metadata);

    for (int y = 0; y < previewHeight; ++y) {
        if (request.cancellation && request.cancellation->load()) {
            return {nullptr, "task.cancelled", "The Bayer extraction was cancelled."};
        }
        const auto channelY = static_cast<std::uint64_t>(
            (static_cast<long double>(y) * geometry.height) / previewHeight);
        for (int x = 0; x < previewWidth; ++x) {
            const auto channelX = static_cast<std::uint64_t>(
                (static_cast<long double>(x) * geometry.width) / previewWidth);
            const auto sample = extracted->pixels->sample(channelX, channelY);
            const auto index =
                static_cast<std::size_t>(y) * previewWidth + x;
            const double value = sample.valid ? sample.value : 0.0;
            signal->values[index] = static_cast<float>(value);
            const auto gray = static_cast<std::uint8_t>(std::round(
                domain::mapDisplayValue(value, mapping) * 255.0));
            const auto output = index * 4;
            extracted->preview.rgba[output] = gray;
            extracted->preview.rgba[output + 1] = gray;
            extracted->preview.rgba[output + 2] = gray;
            extracted->preview.rgba[output + 3] = 255;
        }
    }

    auto extraction = std::make_shared<BayerExtraction>();
    extraction->image = std::move(extracted);
    extraction->geometry = geometry;
    return {std::move(extraction), {}, {}};
}

} // namespace rawviewer::application
