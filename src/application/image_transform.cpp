#include "application/image_transform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace rawviewer::application {
namespace {

constexpr int previewMaximumSide = 1024;

struct AffineMapping {
    std::int64_t xx = 1;
    std::int64_t xy = 0;
    std::int64_t xOffset = 0;
    std::int64_t yx = 0;
    std::int64_t yy = 1;
    std::int64_t yOffset = 0;
};

bool cancelled(const std::shared_ptr<std::atomic_bool>& token) noexcept {
    return token && token->load(std::memory_order_relaxed);
}

bool swapsAxes(ImageTransform transform) noexcept {
    return transform == ImageTransform::RotateLeft ||
        transform == ImageTransform::RotateRight;
}

AffineMapping operationMapping(ImageTransform transform,
                               std::int64_t width,
                               std::int64_t height) noexcept {
    switch (transform) {
    case ImageTransform::FlipVertical:
        return {1, 0, 0, 0, -1, height - 1};
    case ImageTransform::MirrorHorizontal:
        return {-1, 0, width - 1, 0, 1, 0};
    case ImageTransform::RotateLeft:
        return {0, -1, width - 1, 1, 0, 0};
    case ImageTransform::RotateRight:
        return {0, 1, 0, -1, 0, height - 1};
    case ImageTransform::Rotate180:
        return {-1, 0, width - 1, 0, -1, height - 1};
    }
    return {};
}

AffineMapping compose(const AffineMapping& outer,
                      const AffineMapping& inner) noexcept {
    // outer maps the current source to the immutable root; inner maps the
    // new output to the current source. Their orthogonal coefficients remain
    // in {-1, 0, 1}, so offsets remain bounded by the root dimensions.
    return {
        outer.xx * inner.xx + outer.xy * inner.yx,
        outer.xx * inner.xy + outer.xy * inner.yy,
        outer.xx * inner.xOffset + outer.xy * inner.yOffset + outer.xOffset,
        outer.yx * inner.xx + outer.yy * inner.yx,
        outer.yx * inner.xy + outer.yy * inner.yy,
        outer.yx * inner.xOffset + outer.yy * inner.yOffset + outer.yOffset
    };
}

class TransformedPixelSource final : public IPixelSource {
public:
    static std::shared_ptr<const TransformedPixelSource> create(
        const std::shared_ptr<const IPixelSource>& source,
        domain::BayerPattern sourcePattern,
        ImageTransform transform) {
        const auto sourceWidth = source->width();
        const auto sourceHeight = source->height();
        const auto operation = operationMapping(
            transform,
            static_cast<std::int64_t>(sourceWidth),
            static_cast<std::int64_t>(sourceHeight));
        const auto transformed =
            std::dynamic_pointer_cast<const TransformedPixelSource>(source);
        if (transformed) {
            return std::shared_ptr<const TransformedPixelSource>(
                new TransformedPixelSource(
                    transformed->root_,
                    transformed->rootPattern_,
                    swapsAxes(transform) ? sourceHeight : sourceWidth,
                    swapsAxes(transform) ? sourceWidth : sourceHeight,
                    compose(transformed->mapping_, operation)));
        }
        return std::shared_ptr<const TransformedPixelSource>(
            new TransformedPixelSource(
                source,
                sourcePattern,
                swapsAxes(transform) ? sourceHeight : sourceWidth,
                swapsAxes(transform) ? sourceWidth : sourceHeight,
                operation));
    }

    std::uint64_t width() const noexcept override { return width_; }
    std::uint64_t height() const noexcept override { return height_; }

    PixelSample sample(std::uint64_t x,
                       std::uint64_t y) const noexcept override {
        const auto coordinate = sourceCoordinate(x, y);
        if (!coordinate) return {};
        return root_->sample(coordinate->first, coordinate->second);
    }

    domain::BayerChannel bayerChannel(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        const auto coordinate = sourceCoordinate(x, y);
        if (!coordinate) return domain::BayerChannel::None;
        const auto channel = root_->bayerChannel(
            coordinate->first, coordinate->second);
        return channel != domain::BayerChannel::None
            ? channel
            : domain::bayerChannelAt(
                  rootPattern_, coordinate->first, coordinate->second);
    }

private:
    TransformedPixelSource(std::shared_ptr<const IPixelSource> root,
                           domain::BayerPattern rootPattern,
                           std::uint64_t width,
                           std::uint64_t height,
                           AffineMapping mapping)
        : root_(std::move(root)),
          rootPattern_(rootPattern),
          width_(width),
          height_(height),
          mapping_(mapping) {}

    std::optional<std::pair<std::uint64_t, std::uint64_t>> sourceCoordinate(
        std::uint64_t x,
        std::uint64_t y) const noexcept {
        if (x >= width_ || y >= height_ ||
            x > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()) ||
            y > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        const auto signedX = static_cast<std::int64_t>(x);
        const auto signedY = static_cast<std::int64_t>(y);
        const auto sourceX = mapping_.xx * signedX +
            mapping_.xy * signedY + mapping_.xOffset;
        const auto sourceY = mapping_.yx * signedX +
            mapping_.yy * signedY + mapping_.yOffset;
        if (sourceX < 0 || sourceY < 0 ||
            static_cast<std::uint64_t>(sourceX) >= root_->width() ||
            static_cast<std::uint64_t>(sourceY) >= root_->height()) {
            return std::nullopt;
        }
        return std::pair{static_cast<std::uint64_t>(sourceX),
                         static_cast<std::uint64_t>(sourceY)};
    }

    std::shared_ptr<const IPixelSource> root_;
    domain::BayerPattern rootPattern_ = domain::BayerPattern::None;
    std::uint64_t width_ = 0;
    std::uint64_t height_ = 0;
    AffineMapping mapping_;
};

domain::BayerPattern patternFor(
    const std::shared_ptr<const IPixelSource>& pixels) noexcept {
    if (!pixels || pixels->width() < 2 || pixels->height() < 2) {
        return domain::BayerPattern::None;
    }
    const std::array<domain::BayerChannel, 4> actual{
        pixels->bayerChannel(0, 0), pixels->bayerChannel(1, 0),
        pixels->bayerChannel(0, 1), pixels->bayerChannel(1, 1)};
    for (const auto pattern : {domain::BayerPattern::RGGB,
                               domain::BayerPattern::BGGR,
                               domain::BayerPattern::GRBG,
                               domain::BayerPattern::GBRG}) {
        const std::array<domain::BayerChannel, 4> expected{
            domain::bayerChannelAt(pattern, 0, 0),
            domain::bayerChannelAt(pattern, 1, 0),
            domain::bayerChannelAt(pattern, 0, 1),
            domain::bayerChannelAt(pattern, 1, 1)};
        const auto sameColor = [](domain::BayerChannel left,
                                  domain::BayerChannel right) {
            const auto green = [](domain::BayerChannel channel) {
                return channel == domain::BayerChannel::Gr ||
                    channel == domain::BayerChannel::Gb;
            };
            return left == right || (green(left) && green(right));
        };
        if (std::equal(actual.begin(), actual.end(), expected.begin(),
                       sameColor)) {
            return pattern;
        }
    }
    return domain::BayerPattern::None;
}

std::pair<int, int> boundedPreviewSize(std::uint64_t width,
                                       std::uint64_t height) noexcept {
    if (width == 0 || height == 0) return {};
    const long double scale = std::min<long double>(
        1.0L,
        static_cast<long double>(previewMaximumSide) /
            static_cast<long double>(std::max(width, height)));
    return {
        std::max(1, static_cast<int>(std::llround(width * scale))),
        std::max(1, static_cast<int>(std::llround(height * scale)))};
}

std::uint64_t previewCoordinate(int coordinate,
                                int previewExtent,
                                std::uint64_t sourceExtent) noexcept {
    const long double ratio = static_cast<long double>(coordinate) /
        static_cast<long double>(previewExtent);
    return std::min(sourceExtent - 1,
                    static_cast<std::uint64_t>(ratio * sourceExtent));
}

} // namespace

const char* toString(ImageTransform transform) noexcept {
    switch (transform) {
    case ImageTransform::FlipVertical: return "Flip";
    case ImageTransform::MirrorHorizontal: return "Mirror";
    case ImageTransform::RotateLeft: return "Rotate Left";
    case ImageTransform::RotateRight: return "Rotate Right";
    case ImageTransform::Rotate180: return "Rotate 180";
    }
    return "Transform";
}

ImageTransformResult ImageTransformService::execute(
    const ImageTransformRequest& request) const {
    if (!request.source || !request.source->pixels ||
        request.source->metadata.width == 0 ||
        request.source->metadata.height == 0) {
        return {{}, "transform.missing_source",
                "The current image has no transformable pixel source."};
    }
    // Affine composition adds at most two dimension-derived offsets. Keep
    // generous headroom so every signed multiply/add remains defined.
    constexpr auto maximumTransformExtent =
        static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max() / 4);
    if (request.source->metadata.width > maximumTransformExtent ||
        request.source->metadata.height > maximumTransformExtent) {
        return {{}, "transform.dimension_too_large",
                "The image dimensions exceed the transform coordinate range."};
    }
    if (cancelled(request.cancellation)) {
        return {{}, "task.cancelled", "Image transform cancelled."};
    }

    const auto pixels = TransformedPixelSource::create(
        request.source->pixels,
        request.source->metadata.bayerPattern,
        request.transform);
    auto image = std::make_shared<DecodedImage>();
    image->metadata = request.source->metadata;
    image->metadata.width = pixels->width();
    image->metadata.height = pixels->height();
    image->metadata.bayerPattern = patternFor(pixels);
    image->metadata.format += std::string(" | ") + toString(request.transform);
    image->pixels = pixels;
    image->displayReadyRgb = request.source->displayReadyRgb;

    const auto [previewWidth, previewHeight] = boundedPreviewSize(
        pixels->width(), pixels->height());
    const bool rgb = image->displayReadyRgb ||
        image->metadata.kind == ImageKind::Standard;
    std::shared_ptr<SignalPreview> signal;
    if (rgb) {
        image->preview.width = previewWidth;
        image->preview.height = previewHeight;
        image->preview.rgba.resize(
            static_cast<std::size_t>(previewWidth) * previewHeight * 4);
    } else {
        signal = std::make_shared<SignalPreview>();
        signal->width = previewWidth;
        signal->height = previewHeight;
        signal->preservesBayerPhase =
            pixels->width() == static_cast<std::uint64_t>(previewWidth) &&
            pixels->height() == static_cast<std::uint64_t>(previewHeight);
        signal->bayerPattern = signal->preservesBayerPhase
            ? image->metadata.bayerPattern : domain::BayerPattern::None;
        signal->values.resize(
            static_cast<std::size_t>(previewWidth) * previewHeight);
    }

    for (int y = 0; y < previewHeight; ++y) {
        if (cancelled(request.cancellation)) {
            return {{}, "task.cancelled", "Image transform cancelled."};
        }
        const auto sourceY = previewCoordinate(
            y, previewHeight, pixels->height());
        for (int x = 0; x < previewWidth; ++x) {
            const auto sourceX = previewCoordinate(
                x, previewWidth, pixels->width());
            const auto sample = pixels->sample(sourceX, sourceY);
            const auto index = static_cast<std::size_t>(y) * previewWidth + x;
            if (rgb) {
                const auto output = index * 4;
                const auto gray = static_cast<std::uint8_t>(std::clamp(
                    std::lround(sample.value), 0L, 255L));
                image->preview.rgba[output] = sample.rgbValid
                    ? sample.red : gray;
                image->preview.rgba[output + 1] = sample.rgbValid
                    ? sample.green : gray;
                image->preview.rgba[output + 2] = sample.rgbValid
                    ? sample.blue : gray;
                image->preview.rgba[output + 3] = 255;
            } else {
                signal->values[index] = sample.valid
                    ? static_cast<float>(sample.value) : 0.0F;
            }
        }
    }
    if (signal) {
        image->signalPreview = std::move(signal);
    }
    return {std::move(image), {}, {}};
}

} // namespace rawviewer::application
