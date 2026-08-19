#include "application/demosaic.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rawviewer::application {
namespace {

constexpr std::uint64_t tileSide = 64;
constexpr std::size_t maximumCachedTiles = 320;
constexpr int previewMaximumSide = 1024;
constexpr int demosaicRadius = 3;

struct RawRgb {
    bool valid = false;
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
};

bool cancelled(const std::shared_ptr<std::atomic_bool>& token) noexcept {
    return token && token->load(std::memory_order_relaxed);
}

std::uint64_t ceilingDivide(std::uint64_t value,
                            std::uint64_t divisor) noexcept {
    return value / divisor + (value % divisor != 0 ? 1U : 0U);
}

std::int64_t reflect(std::int64_t coordinate,
                     std::int64_t extent) noexcept {
    if (extent <= 1) {
        return 0;
    }
    while (coordinate < 0 || coordinate >= extent) {
        if (coordinate < 0) {
            coordinate = -coordinate;
        }
        if (coordinate >= extent) {
            coordinate = extent * 2 - 2 - coordinate;
        }
    }
    return coordinate;
}

class DirectAccessor {
public:
    DirectAccessor(const IPixelSource& source,
                   domain::BayerPattern pattern)
        : source_(source), pattern_(pattern) {}

    double value(std::int64_t x, std::int64_t y) const noexcept {
        const auto sourceX = static_cast<std::uint64_t>(reflect(
            x, static_cast<std::int64_t>(source_.width())));
        const auto sourceY = static_cast<std::uint64_t>(reflect(
            y, static_cast<std::int64_t>(source_.height())));
        const auto sample = source_.sample(sourceX, sourceY);
        return sample.valid && std::isfinite(sample.value)
            ? sample.value : std::numeric_limits<double>::quiet_NaN();
    }

    domain::BayerChannel channel(std::int64_t x,
                                 std::int64_t y) const noexcept {
        return domain::bayerChannelAt(
            pattern_,
            static_cast<std::uint64_t>(reflect(
                x, static_cast<std::int64_t>(source_.width()))),
            static_cast<std::uint64_t>(reflect(
                y, static_cast<std::int64_t>(source_.height()))));
    }

private:
    const IPixelSource& source_;
    domain::BayerPattern pattern_ = domain::BayerPattern::None;
};

class BufferedAccessor {
public:
    BufferedAccessor(std::int64_t originX,
                     std::int64_t originY,
                     int width,
                     int height,
                     std::uint64_t sourceWidth,
                     std::uint64_t sourceHeight,
                     domain::BayerPattern pattern,
                     const std::vector<double>& values,
                     const std::vector<std::uint8_t>& valid)
        : originX_(originX), originY_(originY), width_(width), height_(height),
          sourceWidth_(sourceWidth), sourceHeight_(sourceHeight),
          pattern_(pattern), values_(values), valid_(valid) {}

    double value(std::int64_t x, std::int64_t y) const noexcept {
        const auto localX = x - originX_;
        const auto localY = y - originY_;
        if (localX < 0 || localY < 0 || localX >= width_ ||
            localY >= height_) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const auto index = static_cast<std::size_t>(localY) * width_ +
            static_cast<std::size_t>(localX);
        return valid_[index]
            ? values_[index] : std::numeric_limits<double>::quiet_NaN();
    }

    domain::BayerChannel channel(std::int64_t x,
                                 std::int64_t y) const noexcept {
        return domain::bayerChannelAt(
            pattern_,
            static_cast<std::uint64_t>(reflect(
                x, static_cast<std::int64_t>(sourceWidth_))),
            static_cast<std::uint64_t>(reflect(
                y, static_cast<std::int64_t>(sourceHeight_))));
    }

private:
    std::int64_t originX_ = 0;
    std::int64_t originY_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::uint64_t sourceWidth_ = 0;
    std::uint64_t sourceHeight_ = 0;
    domain::BayerPattern pattern_ = domain::BayerPattern::None;
    const std::vector<double>& values_;
    const std::vector<std::uint8_t>& valid_;
};

bool isGreen(domain::BayerChannel channel) noexcept {
    return channel == domain::BayerChannel::Gr ||
        channel == domain::BayerChannel::Gb;
}

template <typename Accessor, std::size_t Count>
double averageChannel(const Accessor& source,
                      std::int64_t x,
                      std::int64_t y,
                      domain::BayerChannel desired,
                      const std::array<std::pair<int, int>, Count>& offsets) {
    double sum = 0.0;
    int count = 0;
    for (const auto [dx, dy] : offsets) {
        const auto channel = source.channel(x + dx, y + dy);
        const bool channelMatches = desired == domain::BayerChannel::Gr
            ? isGreen(channel) : channel == desired;
        if (!channelMatches) {
            continue;
        }
        const double value = source.value(x + dx, y + dy);
        if (std::isfinite(value)) {
            sum += value;
            ++count;
        }
    }
    return count > 0
        ? sum / count : std::numeric_limits<double>::quiet_NaN();
}

template <typename Accessor>
RawRgb bilinearAt(const Accessor& source,
                  std::int64_t x,
                  std::int64_t y) {
    static constexpr std::array cross{
        std::pair{-1, 0}, std::pair{1, 0},
        std::pair{0, -1}, std::pair{0, 1}};
    static constexpr std::array diagonal{
        std::pair{-1, -1}, std::pair{1, -1},
        std::pair{-1, 1}, std::pair{1, 1}};
    static constexpr std::array horizontal{
        std::pair{-1, 0}, std::pair{1, 0}};
    static constexpr std::array vertical{
        std::pair{0, -1}, std::pair{0, 1}};

    const double center = source.value(x, y);
    if (!std::isfinite(center)) {
        return {};
    }
    RawRgb result;
    const auto channel = source.channel(x, y);
    if (channel == domain::BayerChannel::R) {
        result.red = center;
        result.green = averageChannel(
            source, x, y, domain::BayerChannel::Gr, cross);
        result.blue = averageChannel(
            source, x, y, domain::BayerChannel::B, diagonal);
    } else if (channel == domain::BayerChannel::B) {
        result.blue = center;
        result.green = averageChannel(
            source, x, y, domain::BayerChannel::Gr, cross);
        result.red = averageChannel(
            source, x, y, domain::BayerChannel::R, diagonal);
    } else if (isGreen(channel)) {
        result.green = center;
        const bool redHorizontal =
            source.channel(x - 1, y) == domain::BayerChannel::R ||
            source.channel(x + 1, y) == domain::BayerChannel::R;
        result.red = averageChannel(
            source, x, y, domain::BayerChannel::R,
            redHorizontal ? horizontal : vertical);
        result.blue = averageChannel(
            source, x, y, domain::BayerChannel::B,
            redHorizontal ? vertical : horizontal);
    }
    result.valid = std::isfinite(result.red) &&
        std::isfinite(result.green) && std::isfinite(result.blue);
    return result;
}

template <typename Accessor>
double convolve5(const Accessor& source,
                 std::int64_t x,
                 std::int64_t y,
                 const std::array<double, 25>& kernel) {
    double result = 0.0;
    for (int ky = -2; ky <= 2; ++ky) {
        for (int kx = -2; kx <= 2; ++kx) {
            const double value = source.value(x + kx, y + ky);
            if (!std::isfinite(value)) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            result += value * kernel[static_cast<std::size_t>(
                (ky + 2) * 5 + kx + 2)];
        }
    }
    return result / 8.0;
}

std::array<double, 25> transpose5(const std::array<double, 25>& input) {
    std::array<double, 25> result{};
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            result[static_cast<std::size_t>(y * 5 + x)] =
                input[static_cast<std::size_t>(x * 5 + y)];
        }
    }
    return result;
}

template <typename Accessor>
RawRgb malvarAt(const Accessor& source,
                std::int64_t x,
                std::int64_t y) {
    static constexpr std::array<double, 25> greenAtRedBlue{
         0, 0,-1, 0, 0,
         0, 0, 2, 0, 0,
        -1, 2, 4, 2,-1,
         0, 0, 2, 0, 0,
         0, 0,-1, 0, 0};
    static constexpr std::array<double, 25> colorAtGreenHorizontal{
         0, 0, 0.5, 0, 0,
         0,-1, 0,-1, 0,
        -1, 4, 5, 4,-1,
         0,-1, 0,-1, 0,
         0, 0, 0.5, 0, 0};
    static constexpr std::array<double, 25> oppositeAtRedBlue{
         0, 0,-1.5, 0, 0,
         0, 2, 0, 2, 0,
        -1.5,0, 6, 0,-1.5,
         0, 2, 0, 2, 0,
         0, 0,-1.5, 0, 0};
    static const auto colorAtGreenVertical =
        transpose5(colorAtGreenHorizontal);

    const double center = source.value(x, y);
    if (!std::isfinite(center)) {
        return {};
    }
    RawRgb result;
    const auto channel = source.channel(x, y);
    if (channel == domain::BayerChannel::R) {
        result.red = center;
        result.green = convolve5(source, x, y, greenAtRedBlue);
        result.blue = convolve5(source, x, y, oppositeAtRedBlue);
    } else if (channel == domain::BayerChannel::B) {
        result.blue = center;
        result.green = convolve5(source, x, y, greenAtRedBlue);
        result.red = convolve5(source, x, y, oppositeAtRedBlue);
    } else if (isGreen(channel)) {
        result.green = center;
        const bool redHorizontal =
            source.channel(x - 1, y) == domain::BayerChannel::R ||
            source.channel(x + 1, y) == domain::BayerChannel::R;
        result.red = convolve5(source, x, y,
            redHorizontal ? colorAtGreenHorizontal : colorAtGreenVertical);
        result.blue = convolve5(source, x, y,
            redHorizontal ? colorAtGreenVertical : colorAtGreenHorizontal);
    }
    result.valid = std::isfinite(result.red) &&
        std::isfinite(result.green) && std::isfinite(result.blue);
    return result.valid ? result : bilinearAt(source, x, y);
}

template <typename Accessor>
double hamiltonGreen(const Accessor& source,
                     std::int64_t x,
                     std::int64_t y) {
    if (isGreen(source.channel(x, y))) {
        return source.value(x, y);
    }
    const double center = source.value(x, y);
    const double leftGreen = source.value(x - 1, y);
    const double rightGreen = source.value(x + 1, y);
    const double topGreen = source.value(x, y - 1);
    const double bottomGreen = source.value(x, y + 1);
    const double leftColor = source.value(x - 2, y);
    const double rightColor = source.value(x + 2, y);
    const double topColor = source.value(x, y - 2);
    const double bottomColor = source.value(x, y + 2);
    const std::array values{center, leftGreen, rightGreen, topGreen,
        bottomGreen, leftColor, rightColor, topColor, bottomColor};
    if (std::any_of(values.begin(), values.end(), [](double value) {
            return !std::isfinite(value);
        })) {
        return bilinearAt(source, x, y).green;
    }
    const double horizontalCorrection =
        2.0 * center - leftColor - rightColor;
    const double verticalCorrection =
        2.0 * center - topColor - bottomColor;
    const double horizontal = (leftGreen + rightGreen) * 0.5 +
        horizontalCorrection * 0.25;
    const double vertical = (topGreen + bottomGreen) * 0.5 +
        verticalCorrection * 0.25;
    const double horizontalGradient = std::abs(leftGreen - rightGreen) +
        std::abs(horizontalCorrection);
    const double verticalGradient = std::abs(topGreen - bottomGreen) +
        std::abs(verticalCorrection);
    if (horizontalGradient < verticalGradient) {
        return horizontal;
    }
    if (verticalGradient < horizontalGradient) {
        return vertical;
    }
    return (horizontal + vertical) * 0.5;
}

template <typename Accessor, std::size_t Count>
double colorDifference(const Accessor& source,
                       std::int64_t x,
                       std::int64_t y,
                       domain::BayerChannel desired,
                       double centerGreen,
                       const std::array<std::pair<int, int>, Count>& offsets) {
    double sum = 0.0;
    int count = 0;
    for (const auto [dx, dy] : offsets) {
        if (source.channel(x + dx, y + dy) != desired) {
            continue;
        }
        const double color = source.value(x + dx, y + dy);
        const double green = hamiltonGreen(source, x + dx, y + dy);
        if (std::isfinite(color) && std::isfinite(green)) {
            sum += color - green;
            ++count;
        }
    }
    return count > 0 ? centerGreen + sum / count
        : std::numeric_limits<double>::quiet_NaN();
}

template <typename Accessor>
RawRgb hamiltonAt(const Accessor& source,
                  std::int64_t x,
                  std::int64_t y) {
    static constexpr std::array horizontal{
        std::pair{-1, 0}, std::pair{1, 0}};
    static constexpr std::array vertical{
        std::pair{0, -1}, std::pair{0, 1}};
    static constexpr std::array diagonal{
        std::pair{-1, -1}, std::pair{1, -1},
        std::pair{-1, 1}, std::pair{1, 1}};

    const double center = source.value(x, y);
    const double green = hamiltonGreen(source, x, y);
    if (!std::isfinite(center) || !std::isfinite(green)) {
        return {};
    }
    RawRgb result;
    result.green = green;
    const auto channel = source.channel(x, y);
    if (channel == domain::BayerChannel::R) {
        result.red = center;
        result.blue = colorDifference(source, x, y,
            domain::BayerChannel::B, green, diagonal);
    } else if (channel == domain::BayerChannel::B) {
        result.blue = center;
        result.red = colorDifference(source, x, y,
            domain::BayerChannel::R, green, diagonal);
    } else if (isGreen(channel)) {
        const bool redHorizontal =
            source.channel(x - 1, y) == domain::BayerChannel::R ||
            source.channel(x + 1, y) == domain::BayerChannel::R;
        result.red = colorDifference(source, x, y,
            domain::BayerChannel::R, green,
            redHorizontal ? horizontal : vertical);
        result.blue = colorDifference(source, x, y,
            domain::BayerChannel::B, green,
            redHorizontal ? vertical : horizontal);
    }
    result.valid = std::isfinite(result.red) &&
        std::isfinite(result.green) && std::isfinite(result.blue);
    return result.valid ? result : bilinearAt(source, x, y);
}

template <typename Accessor>
RawRgb demosaicAt(const Accessor& source,
                  DemosaicAlgorithm algorithm,
                  std::int64_t x,
                  std::int64_t y) {
    switch (algorithm) {
    case DemosaicAlgorithm::Bilinear:
        return bilinearAt(source, x, y);
    case DemosaicAlgorithm::MalvarHeCutler:
        return malvarAt(source, x, y);
    case DemosaicAlgorithm::HamiltonAdams:
        return hamiltonAt(source, x, y);
    }
    return {};
}

std::uint8_t mapByte(double value,
                     const domain::DisplayMapping& mapping) noexcept {
    return static_cast<std::uint8_t>(std::round(
        domain::mapDisplayValue(value, mapping) * 255.0));
}

class DemosaicedPixelSource final : public IPixelSource {
public:
    DemosaicedPixelSource(std::shared_ptr<const IPixelSource> source,
                          domain::BayerPattern pattern,
                          DemosaicAlgorithm algorithm,
                          domain::DisplayMapping mapping)
        : source_(std::move(source)), pattern_(pattern), algorithm_(algorithm),
          mapping_(mapping), tileColumns_(ceilingDivide(width(), tileSide)) {}

    std::uint64_t width() const noexcept override { return source_->width(); }
    std::uint64_t height() const noexcept override { return source_->height(); }

    PixelSample sample(std::uint64_t x,
                       std::uint64_t y) const noexcept override {
        if (x >= width() || y >= height()) {
            return {};
        }
        const auto tile = tileFor(x / tileSide, y / tileSide);
        if (!tile) {
            return {};
        }
        const auto localX = static_cast<std::size_t>(x - tile->x);
        const auto localY = static_cast<std::size_t>(y - tile->y);
        const auto index = localY * tile->width + localX;
        if (!tile->valid[index]) {
            return {};
        }
        const auto offset = index * 3;
        return {true, 0.0, true,
                tile->rgb[offset], tile->rgb[offset + 1],
                tile->rgb[offset + 2]};
    }

private:
    struct Tile {
        std::uint64_t x = 0;
        std::uint64_t y = 0;
        std::size_t width = 0;
        std::size_t height = 0;
        std::vector<std::uint8_t> rgb;
        std::vector<std::uint8_t> valid;
    };
    struct CacheEntry {
        std::uint64_t key = 0;
        std::shared_ptr<const Tile> tile;
    };

    std::shared_ptr<const Tile> tileFor(std::uint64_t tileX,
                                        std::uint64_t tileY) const noexcept {
        const std::uint64_t key = tileY * tileColumns_ + tileX;
        {
            std::scoped_lock lock(cacheMutex_);
            const auto found = cacheIndex_.find(key);
            if (found != cacheIndex_.end()) {
                cache_.splice(cache_.begin(), cache_, found->second);
                return found->second->tile;
            }
        }
        auto computed = computeTile(tileX, tileY);
        if (!computed) {
            return {};
        }
        std::scoped_lock lock(cacheMutex_);
        const auto raced = cacheIndex_.find(key);
        if (raced != cacheIndex_.end()) {
            cache_.splice(cache_.begin(), cache_, raced->second);
            return raced->second->tile;
        }
        cache_.push_front({key, computed});
        cacheIndex_[key] = cache_.begin();
        while (cache_.size() > maximumCachedTiles) {
            cacheIndex_.erase(cache_.back().key);
            cache_.pop_back();
        }
        return computed;
    }

    std::shared_ptr<const Tile> computeTile(std::uint64_t tileX,
                                            std::uint64_t tileY) const noexcept {
        const std::uint64_t startX = tileX * tileSide;
        const std::uint64_t startY = tileY * tileSide;
        if (startX >= width() || startY >= height()) {
            return {};
        }
        const int outputWidth = static_cast<int>(std::min(
            tileSide, width() - startX));
        const int outputHeight = static_cast<int>(std::min(
            tileSide, height() - startY));
        const int expandedWidth = outputWidth + demosaicRadius * 2;
        const int expandedHeight = outputHeight + demosaicRadius * 2;
        std::vector<double> values(
            static_cast<std::size_t>(expandedWidth) * expandedHeight);
        std::vector<std::uint8_t> valid(values.size(), 0);
        for (int y = 0; y < expandedHeight; ++y) {
            const auto sourceY = static_cast<std::uint64_t>(reflect(
                static_cast<std::int64_t>(startY) + y - demosaicRadius,
                static_cast<std::int64_t>(height())));
            for (int x = 0; x < expandedWidth; ++x) {
                const auto sourceX = static_cast<std::uint64_t>(reflect(
                    static_cast<std::int64_t>(startX) + x - demosaicRadius,
                    static_cast<std::int64_t>(width())));
                const auto sample = source_->sample(sourceX, sourceY);
                const auto index = static_cast<std::size_t>(y) *
                    expandedWidth + x;
                if (sample.valid && std::isfinite(sample.value)) {
                    values[index] = sample.value;
                    valid[index] = 1;
                }
            }
        }
        BufferedAccessor accessor(
            static_cast<std::int64_t>(startX) - demosaicRadius,
            static_cast<std::int64_t>(startY) - demosaicRadius,
            expandedWidth, expandedHeight, width(), height(), pattern_,
            values, valid);
        auto tile = std::make_shared<Tile>();
        tile->x = startX;
        tile->y = startY;
        tile->width = static_cast<std::size_t>(outputWidth);
        tile->height = static_cast<std::size_t>(outputHeight);
        tile->rgb.resize(tile->width * tile->height * 3);
        tile->valid.resize(tile->width * tile->height, 0);
        for (int y = 0; y < outputHeight; ++y) {
            for (int x = 0; x < outputWidth; ++x) {
                const auto rgb = demosaicAt(
                    accessor, algorithm_,
                    static_cast<std::int64_t>(startX) + x,
                    static_cast<std::int64_t>(startY) + y);
                const auto index = static_cast<std::size_t>(y) *
                    outputWidth + x;
                if (!rgb.valid) {
                    continue;
                }
                tile->valid[index] = 1;
                tile->rgb[index * 3] = mapByte(rgb.red, mapping_);
                tile->rgb[index * 3 + 1] = mapByte(rgb.green, mapping_);
                tile->rgb[index * 3 + 2] = mapByte(rgb.blue, mapping_);
            }
        }
        return tile;
    }

    std::shared_ptr<const IPixelSource> source_;
    domain::BayerPattern pattern_ = domain::BayerPattern::None;
    DemosaicAlgorithm algorithm_ = DemosaicAlgorithm::MalvarHeCutler;
    domain::DisplayMapping mapping_;
    std::uint64_t tileColumns_ = 0;
    mutable std::mutex cacheMutex_;
    mutable std::list<CacheEntry> cache_;
    mutable std::unordered_map<
        std::uint64_t, std::list<CacheEntry>::iterator> cacheIndex_;
};

std::pair<int, int> boundedPreviewSize(std::uint64_t width,
                                       std::uint64_t height) {
    const double scale = std::min(
        1.0, static_cast<double>(previewMaximumSide) /
            static_cast<double>(std::max(width, height)));
    return {
        std::max(1, static_cast<int>(std::round(width * scale))),
        std::max(1, static_cast<int>(std::round(height * scale)))
    };
}

} // namespace

const char* toString(DemosaicAlgorithm algorithm) noexcept {
    switch (algorithm) {
    case DemosaicAlgorithm::Bilinear: return "Bilinear";
    case DemosaicAlgorithm::MalvarHeCutler: return "Malvar-He-Cutler";
    case DemosaicAlgorithm::HamiltonAdams: return "Hamilton-Adams";
    }
    return "Unknown";
}

DemosaicResult DemosaicService::execute(
    const DemosaicRequest& request) const {
    if (!request.source || !request.source->pixels ||
        request.source->metadata.width == 0 ||
        request.source->metadata.height == 0) {
        return {nullptr, "demosaic.no_source",
                "A decoded Bayer RAW pixel source is required."};
    }
    if (request.source->metadata.kind == ImageKind::Standard ||
        request.source->metadata.bayerPattern == domain::BayerPattern::None) {
        return {nullptr, "demosaic.unsupported_source",
                "Demosaic requires a regular RGGB, BGGR, GRBG, or GBRG RAW source."};
    }
    if (request.source->metadata.width < 2 ||
        request.source->metadata.height < 2) {
        return {nullptr, "demosaic.image_too_small",
                "A Bayer source must be at least 2 by 2 pixels."};
    }
    if (request.source->metadata.width >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        request.source->metadata.height >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return {nullptr, "demosaic.dimension_overflow",
                "Image dimensions exceed the supported coordinate range."};
    }
    const auto mappingValidation =
        domain::validateDisplayMapping(request.displayMapping);
    if (!mappingValidation.valid) {
        return {nullptr, mappingValidation.errorCode, mappingValidation.message};
    }
    if (cancelled(request.cancellation)) {
        return {nullptr, "task.cancelled", "The demosaic task was cancelled."};
    }

    const bool phasePreviewAvailable = request.source->signalPreview &&
        request.source->signalPreview->preservesBayerPhase &&
        request.source->signalPreview->bayerPattern ==
            request.source->metadata.bayerPattern &&
        request.source->signalPreview->width > 1 &&
        request.source->signalPreview->height > 1 &&
        request.source->signalPreview->values.size() ==
            static_cast<std::size_t>(request.source->signalPreview->width) *
                request.source->signalPreview->height;
    const auto boundedSize = boundedPreviewSize(
        request.source->metadata.width, request.source->metadata.height);
    const int previewWidth = phasePreviewAvailable
        ? request.source->signalPreview->width : boundedSize.first;
    const int previewHeight = phasePreviewAvailable
        ? request.source->signalPreview->height : boundedSize.second;
    DisplayImage preview;
    preview.width = previewWidth;
    preview.height = previewHeight;
    preview.rgba.resize(
        static_cast<std::size_t>(previewWidth) * previewHeight * 4);
    const auto writePixel = [&](int x, int y, const RawRgb& rgb) {
        const auto index =
            (static_cast<std::size_t>(y) * previewWidth + x) * 4;
        preview.rgba[index] = rgb.valid
            ? mapByte(rgb.red, request.displayMapping) : 0;
        preview.rgba[index + 1] = rgb.valid
            ? mapByte(rgb.green, request.displayMapping) : 0;
        preview.rgba[index + 2] = rgb.valid
            ? mapByte(rgb.blue, request.displayMapping) : 0;
        preview.rgba[index + 3] = 255;
    };
    if (phasePreviewAvailable) {
        std::vector<double> values(
            request.source->signalPreview->values.begin(),
            request.source->signalPreview->values.end());
        std::vector<std::uint8_t> valid(values.size(), 0);
        for (std::size_t index = 0; index < values.size(); ++index) {
            valid[index] = std::isfinite(values[index]) ? 1 : 0;
        }
        BufferedAccessor accessor(
            0, 0, previewWidth, previewHeight,
            static_cast<std::uint64_t>(previewWidth),
            static_cast<std::uint64_t>(previewHeight),
            request.source->metadata.bayerPattern, values, valid);
        for (int y = 0; y < previewHeight; ++y) {
            if (cancelled(request.cancellation)) {
                return {nullptr, "task.cancelled",
                        "The demosaic task was cancelled."};
            }
            for (int x = 0; x < previewWidth; ++x) {
                writePixel(x, y, demosaicAt(
                    accessor, request.algorithm, x, y));
            }
        }
    } else {
        DirectAccessor accessor(
            *request.source->pixels, request.source->metadata.bayerPattern);
        for (int y = 0; y < previewHeight; ++y) {
            if (cancelled(request.cancellation)) {
                return {nullptr, "task.cancelled",
                        "The demosaic task was cancelled."};
            }
            const auto sourceY = static_cast<std::int64_t>(std::min(
                request.source->metadata.height - 1,
                static_cast<std::uint64_t>(
                    (static_cast<long double>(y) + 0.5L) *
                    request.source->metadata.height / previewHeight)));
            for (int x = 0; x < previewWidth; ++x) {
                const auto sourceX = static_cast<std::int64_t>(std::min(
                    request.source->metadata.width - 1,
                    static_cast<std::uint64_t>(
                        (static_cast<long double>(x) + 0.5L) *
                        request.source->metadata.width / previewWidth)));
                writePixel(x, y, demosaicAt(
                    accessor, request.algorithm, sourceX, sourceY));
            }
        }
    }

    auto image = std::make_shared<DecodedImage>();
    image->metadata = request.source->metadata;
    image->metadata.kind = ImageKind::Standard;
    image->metadata.bayerPattern = domain::BayerPattern::None;
    image->metadata.scalarType = domain::ScalarType::UInt8;
    image->metadata.sensorBlackLevel = 0.0;
    image->metadata.whiteLevel = 255.0;
    image->metadata.format += std::string(" / ") +
        toString(request.algorithm) + " demosaic";
    std::ostringstream details;
    details << request.source->metadata.details;
    if (!request.source->metadata.details.empty()) {
        details << "; ";
    }
    details << toString(request.algorithm)
            << " Bayer demosaic; RGB mapped from current display range";
    image->metadata.details = details.str();
    image->preview = std::move(preview);
    image->pixels = std::make_shared<DemosaicedPixelSource>(
        request.source->pixels, request.source->metadata.bayerPattern,
        request.algorithm, request.displayMapping);
    image->displayReadyRgb = true;
    return {std::move(image), {}, {}};
}

} // namespace rawviewer::application
