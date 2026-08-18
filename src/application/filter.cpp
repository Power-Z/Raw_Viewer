#include "application/filter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rawviewer::application {
namespace {

constexpr std::uint64_t tileSide = 64;
// A 20K-wide image needs 313 tiles per scanline. Keeping one full tile row
// prevents row-major statistics and exports from recomputing every tile for
// each of its 64 rows, while remaining bounded to roughly 12 MiB.
constexpr std::size_t maximumCachedTiles = 320;
constexpr int previewMaximumSide = 1024;

struct ScalarBuffer {
    int width = 0;
    int height = 0;
    std::vector<double> values;
    std::vector<std::uint8_t> valid;
};

bool cancelled(const std::shared_ptr<std::atomic_bool>& token) noexcept {
    return token && token->load(std::memory_order_relaxed);
}

std::uint64_t ceilingDivide(std::uint64_t value,
                            std::uint64_t divisor) noexcept {
    return value / divisor + (value % divisor != 0 ? 1U : 0U);
}

std::vector<double> filterWeights(const FilterParameters& parameters) {
    std::vector<double> weights(parameters.kernelSize, 1.0);
    if (parameters.type != FilterType::Gaussian) {
        return weights;
    }
    const int radius = static_cast<int>(parameters.kernelSize / 2);
    const double denominator = 2.0 * parameters.sigma * parameters.sigma;
    for (int index = -radius; index <= radius; ++index) {
        weights[static_cast<std::size_t>(index + radius)] =
            std::exp(-(static_cast<double>(index * index)) / denominator);
    }
    return weights;
}

class SlidingMedian {
public:
    void add(double value) {
        if (lower_.empty() || value <= *lower_.rbegin()) {
            lower_.insert(value);
        } else {
            upper_.insert(value);
        }
        rebalance();
    }

    void remove(double value) {
        const auto lower = lower_.find(value);
        if (lower != lower_.end()) {
            lower_.erase(lower);
        } else {
            const auto upper = upper_.find(value);
            if (upper != upper_.end()) {
                upper_.erase(upper);
            }
        }
        rebalance();
    }

    bool empty() const noexcept { return lower_.empty(); }

    double median() const noexcept {
        if (lower_.size() == upper_.size()) {
            return (*lower_.rbegin() + *upper_.begin()) * 0.5;
        }
        return *lower_.rbegin();
    }

private:
    void rebalance() {
        while (lower_.size() > upper_.size() + 1) {
            const auto iterator = std::prev(lower_.end());
            upper_.insert(*iterator);
            lower_.erase(iterator);
        }
        while (lower_.size() < upper_.size()) {
            const auto iterator = upper_.begin();
            lower_.insert(*iterator);
            upper_.erase(iterator);
        }
    }

    std::multiset<double> lower_;
    std::multiset<double> upper_;
};

ScalarBuffer filterMedian(const ScalarBuffer& expanded,
                          int outputWidth,
                          int outputHeight,
                          int radius,
                          const std::shared_ptr<std::atomic_bool>& token = {}) {
    ScalarBuffer output;
    output.width = outputWidth;
    output.height = outputHeight;
    output.values.resize(static_cast<std::size_t>(outputWidth) * outputHeight);
    output.valid.resize(output.values.size(), 0);
    const int kernel = radius * 2 + 1;

    for (int y = 0; y < outputHeight; ++y) {
        if (cancelled(token)) {
            return {};
        }
        SlidingMedian window;
        const auto addColumn = [&](int column) {
            for (int row = y; row < y + kernel; ++row) {
                const auto index = static_cast<std::size_t>(row) *
                    expanded.width + column;
                if (expanded.valid[index]) {
                    window.add(expanded.values[index]);
                }
            }
        };
        const auto removeColumn = [&](int column) {
            for (int row = y; row < y + kernel; ++row) {
                const auto index = static_cast<std::size_t>(row) *
                    expanded.width + column;
                if (expanded.valid[index]) {
                    window.remove(expanded.values[index]);
                }
            }
        };
        for (int column = 0; column < kernel; ++column) {
            addColumn(column);
        }
        for (int x = 0; x < outputWidth; ++x) {
            const auto outputIndex = static_cast<std::size_t>(y) *
                outputWidth + x;
            if (!window.empty()) {
                output.valid[outputIndex] = 1;
                output.values[outputIndex] = window.median();
            }
            if (x + 1 < outputWidth) {
                removeColumn(x);
                addColumn(x + kernel);
            }
        }
    }
    return output;
}

ScalarBuffer filterSeparable(const ScalarBuffer& expanded,
                             int outputWidth,
                             int outputHeight,
                             const std::vector<double>& weights,
                             const std::shared_ptr<std::atomic_bool>& token = {}) {
    const int radius = static_cast<int>(weights.size() / 2);
    std::vector<double> horizontalValues(
        static_cast<std::size_t>(expanded.height) * outputWidth, 0.0);
    std::vector<double> horizontalWeights(horizontalValues.size(), 0.0);
    for (int y = 0; y < expanded.height; ++y) {
        if (cancelled(token)) {
            return {};
        }
        for (int x = 0; x < outputWidth; ++x) {
            double numerator = 0.0;
            double denominator = 0.0;
            for (int offset = -radius; offset <= radius; ++offset) {
                const auto sourceIndex = static_cast<std::size_t>(y) *
                    expanded.width + x + offset + radius;
                if (!expanded.valid[sourceIndex]) {
                    continue;
                }
                const double weight = weights[
                    static_cast<std::size_t>(offset + radius)];
                numerator += expanded.values[sourceIndex] * weight;
                denominator += weight;
            }
            const auto index = static_cast<std::size_t>(y) * outputWidth + x;
            horizontalValues[index] = numerator;
            horizontalWeights[index] = denominator;
        }
    }

    ScalarBuffer output;
    output.width = outputWidth;
    output.height = outputHeight;
    output.values.resize(static_cast<std::size_t>(outputWidth) * outputHeight);
    output.valid.resize(output.values.size(), 0);
    for (int y = 0; y < outputHeight; ++y) {
        if (cancelled(token)) {
            return {};
        }
        for (int x = 0; x < outputWidth; ++x) {
            double numerator = 0.0;
            double denominator = 0.0;
            for (int offset = -radius; offset <= radius; ++offset) {
                const auto horizontalIndex = static_cast<std::size_t>(
                    y + offset + radius) * outputWidth + x;
                const double weight = weights[
                    static_cast<std::size_t>(offset + radius)];
                numerator += horizontalValues[horizontalIndex] * weight;
                denominator += horizontalWeights[horizontalIndex] * weight;
            }
            const auto outputIndex = static_cast<std::size_t>(y) *
                outputWidth + x;
            if (denominator > 0.0) {
                output.valid[outputIndex] = 1;
                output.values[outputIndex] = numerator / denominator;
            }
        }
    }
    return output;
}

ScalarBuffer filterBuffer(const ScalarBuffer& source,
                          const FilterParameters& parameters,
                          const std::shared_ptr<std::atomic_bool>& token = {}) {
    if (source.width <= 0 || source.height <= 0) {
        return {};
    }
    const int radius = static_cast<int>(parameters.kernelSize / 2);
    ScalarBuffer expanded;
    expanded.width = source.width + radius * 2;
    expanded.height = source.height + radius * 2;
    expanded.values.resize(
        static_cast<std::size_t>(expanded.width) * expanded.height);
    expanded.valid.resize(expanded.values.size(), 0);
    for (int y = 0; y < expanded.height; ++y) {
        const int sourceY = std::clamp(y - radius, 0, source.height - 1);
        for (int x = 0; x < expanded.width; ++x) {
            const int sourceX = std::clamp(x - radius, 0, source.width - 1);
            const auto sourceIndex = static_cast<std::size_t>(sourceY) *
                source.width + sourceX;
            const auto index = static_cast<std::size_t>(y) *
                expanded.width + x;
            expanded.values[index] = source.values[sourceIndex];
            expanded.valid[index] = source.valid[sourceIndex];
        }
    }
    if (parameters.type == FilterType::Median) {
        return filterMedian(
            expanded, source.width, source.height, radius, token);
    }
    return filterSeparable(
        expanded, source.width, source.height,
        filterWeights(parameters), token);
}

class FilteredPixelSource final : public IPixelSource {
public:
    FilteredPixelSource(std::shared_ptr<const IPixelSource> source,
                        FilterParameters parameters)
        : source_(std::move(source)), parameters_(parameters),
          tileColumns_(ceilingDivide(source_->width(), tileSide)) {}

    std::uint64_t width() const noexcept override { return source_->width(); }
    std::uint64_t height() const noexcept override { return source_->height(); }

    PixelSample sample(std::uint64_t x,
                       std::uint64_t y) const noexcept override {
        if (x >= width() || y >= height()) {
            return {};
        }
        const auto tileX = x / tileSide;
        const auto tileY = y / tileSide;
        const auto tile = tileFor(tileX, tileY);
        if (!tile) {
            return {};
        }
        const auto localX = static_cast<std::size_t>(x - tile->x);
        const auto localY = static_cast<std::size_t>(y - tile->y);
        const auto index = localY * tile->width + localX;
        return tile->valid[index]
            ? PixelSample{true, tile->values[index]}
            : PixelSample{};
    }

    domain::BayerChannel bayerChannel(
        std::uint64_t x,
        std::uint64_t y) const noexcept override {
        return source_->bayerChannel(x, y);
    }

private:
    struct Tile {
        std::uint64_t x = 0;
        std::uint64_t y = 0;
        std::size_t width = 0;
        std::size_t height = 0;
        std::vector<double> values;
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
        const int radius = static_cast<int>(parameters_.kernelSize / 2);
        ScalarBuffer expanded;
        expanded.width = outputWidth + radius * 2;
        expanded.height = outputHeight + radius * 2;
        expanded.values.resize(
            static_cast<std::size_t>(expanded.width) * expanded.height);
        expanded.valid.resize(expanded.values.size(), 0);
        for (int y = 0; y < expanded.height; ++y) {
            const auto sourceY = static_cast<std::uint64_t>(
                std::clamp<std::int64_t>(
                    static_cast<std::int64_t>(startY) + y - radius,
                    0, static_cast<std::int64_t>(height() - 1)));
            for (int x = 0; x < expanded.width; ++x) {
                const auto sourceX = static_cast<std::uint64_t>(
                    std::clamp<std::int64_t>(
                        static_cast<std::int64_t>(startX) + x - radius,
                        0, static_cast<std::int64_t>(width() - 1)));
                const auto sample = source_->sample(sourceX, sourceY);
                const auto index = static_cast<std::size_t>(y) *
                    expanded.width + x;
                if (sample.valid && std::isfinite(sample.value)) {
                    expanded.valid[index] = 1;
                    expanded.values[index] = sample.value;
                }
            }
        }
        ScalarBuffer filtered = parameters_.type == FilterType::Median
            ? filterMedian(expanded, outputWidth, outputHeight, radius)
            : filterSeparable(expanded, outputWidth, outputHeight,
                              filterWeights(parameters_));
        if (filtered.values.empty()) {
            return {};
        }
        auto tile = std::make_shared<Tile>();
        tile->x = startX;
        tile->y = startY;
        tile->width = static_cast<std::size_t>(outputWidth);
        tile->height = static_cast<std::size_t>(outputHeight);
        tile->values = std::move(filtered.values);
        tile->valid = std::move(filtered.valid);
        return tile;
    }

    std::shared_ptr<const IPixelSource> source_;
    FilterParameters parameters_;
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

ScalarBuffer basePreview(const FilterRequest& request) {
    const auto& source = *request.source;
    const bool regularBayer =
        source.metadata.bayerPattern != domain::BayerPattern::None;
    if (source.signalPreview && source.signalPreview->width > 0 &&
        source.signalPreview->height > 0 &&
        source.signalPreview->values.size() ==
            static_cast<std::size_t>(source.signalPreview->width) *
                source.signalPreview->height &&
        (!regularBayer ||
         (source.signalPreview->preservesBayerPhase &&
          source.signalPreview->bayerPattern ==
              source.metadata.bayerPattern))) {
        const auto [width, height] = boundedPreviewSize(
            static_cast<std::uint64_t>(source.signalPreview->width),
            static_cast<std::uint64_t>(source.signalPreview->height));
        ScalarBuffer result;
        result.width = width;
        result.height = height;
        result.values.resize(static_cast<std::size_t>(width) * height);
        result.valid.resize(result.values.size(), 0);
        for (int y = 0; y < height; ++y) {
            if (cancelled(request.cancellation)) {
                return {};
            }
            const int sourceY = std::min(
                source.signalPreview->height - 1,
                static_cast<int>(static_cast<long long>(y) *
                    source.signalPreview->height / height));
            for (int x = 0; x < width; ++x) {
                const int sourceX = std::min(
                    source.signalPreview->width - 1,
                    static_cast<int>(static_cast<long long>(x) *
                        source.signalPreview->width / width));
                const float value = source.signalPreview->values[
                    static_cast<std::size_t>(sourceY) *
                        source.signalPreview->width + sourceX];
                const auto index = static_cast<std::size_t>(y) * width + x;
                if (std::isfinite(value)) {
                    result.values[index] = value;
                    result.valid[index] = 1;
                }
            }
        }
        return result;
    }

    const auto [width, height] = boundedPreviewSize(
        source.metadata.width, source.metadata.height);
    ScalarBuffer result;
    result.width = width;
    result.height = height;
    result.values.resize(static_cast<std::size_t>(width) * height);
    result.valid.resize(result.values.size(), 0);
    for (int y = 0; y < height; ++y) {
        if (cancelled(request.cancellation)) {
            return {};
        }
        const auto sourceY = std::min(
            source.metadata.height - 1,
            static_cast<std::uint64_t>(
                static_cast<long double>(y) * source.metadata.height / height));
        auto phaseY = sourceY;
        if (regularBayer && (phaseY & 1U) !=
                (static_cast<std::uint64_t>(y) & 1U)) {
            phaseY = phaseY + 1 < source.metadata.height
                ? phaseY + 1 : phaseY - 1;
        }
        for (int x = 0; x < width; ++x) {
            const auto sourceX = std::min(
                source.metadata.width - 1,
                static_cast<std::uint64_t>(
                    static_cast<long double>(x) * source.metadata.width / width));
            auto phaseX = sourceX;
            if (regularBayer && (phaseX & 1U) !=
                    (static_cast<std::uint64_t>(x) & 1U)) {
                phaseX = phaseX + 1 < source.metadata.width
                    ? phaseX + 1 : phaseX - 1;
            }
            const auto sample = source.pixels->sample(phaseX, phaseY);
            const auto index = static_cast<std::size_t>(y) * width + x;
            if (sample.valid && std::isfinite(sample.value)) {
                result.valid[index] = 1;
                result.values[index] = sample.value;
            }
        }
    }
    return result;
}

} // namespace

const char* toString(FilterType type) noexcept {
    switch (type) {
    case FilterType::Mean: return "Mean";
    case FilterType::Gaussian: return "Gaussian";
    case FilterType::Median: return "Median";
    }
    return "Unknown";
}

bool FilterParameters::isValid() const noexcept {
    return (kernelSize == 3 || kernelSize == 5 || kernelSize == 7) &&
        std::isfinite(sigma) && sigma > 0.0 && sigma <= 10.0;
}

FilterResult FilterService::execute(const FilterRequest& request) const {
    if (!request.source || !request.source->pixels ||
        request.source->metadata.width == 0 ||
        request.source->metadata.height == 0) {
        return {nullptr, "filter.no_source",
                "A decoded RAW pixel source is required for filtering."};
    }
    if (request.source->metadata.kind == ImageKind::Standard) {
        return {nullptr, "filter.unsupported_source",
                "Filter currently supports scalar RAW images only."};
    }
    if (!request.parameters.isValid()) {
        return {nullptr, "filter.invalid_parameters",
                "Kernel size must be 3, 5, or 7 and sigma must be in (0, 10]."};
    }
    if (cancelled(request.cancellation)) {
        return {nullptr, "task.cancelled", "The filter task was cancelled."};
    }

    ScalarBuffer preview = basePreview(request);
    if (preview.values.empty() || cancelled(request.cancellation)) {
        return {nullptr, "task.cancelled", "The filter task was cancelled."};
    }
    const double previewScale = std::min(
        static_cast<double>(preview.width) / request.source->metadata.width,
        static_cast<double>(preview.height) / request.source->metadata.height);
    const int sourceRadius = static_cast<int>(request.parameters.kernelSize / 2);
    const int previewRadius = static_cast<int>(std::lround(
        sourceRadius * previewScale));
    if (previewRadius > 0) {
        FilterParameters previewParameters = request.parameters;
        previewParameters.kernelSize = static_cast<std::uint32_t>(
            previewRadius * 2 + 1);
        if (previewParameters.type == FilterType::Gaussian) {
            previewParameters.sigma = std::max(
                0.1, request.parameters.sigma * previewScale);
        }
        preview = filterBuffer(preview, previewParameters, request.cancellation);
        if (preview.values.empty()) {
            return {nullptr, "task.cancelled", "The filter task was cancelled."};
        }
    }

    auto image = std::make_shared<DecodedImage>();
    image->metadata = request.source->metadata;
    image->metadata.format += std::string(" / ") +
        toString(request.parameters.type) + " filter";
    std::ostringstream details;
    details << request.source->metadata.details;
    if (!request.source->metadata.details.empty()) {
        details << "; ";
    }
    details << toString(request.parameters.type) << " filter "
            << request.parameters.kernelSize << 'x'
            << request.parameters.kernelSize;
    if (request.parameters.type == FilterType::Gaussian) {
        details << ", sigma " << request.parameters.sigma;
    }
    image->metadata.details = details.str();
    image->pixels = std::make_shared<FilteredPixelSource>(
        request.source->pixels, request.parameters);
    auto signal = std::make_shared<SignalPreview>();
    signal->width = preview.width;
    signal->height = preview.height;
    signal->values.resize(preview.values.size());
    signal->preservesBayerPhase =
        request.source->metadata.bayerPattern != domain::BayerPattern::None;
    signal->bayerPattern = request.source->metadata.bayerPattern;
    for (std::size_t index = 0; index < preview.values.size(); ++index) {
        signal->values[index] = preview.valid[index]
            ? static_cast<float>(preview.values[index]) : 0.0F;
    }
    image->signalPreview = std::move(signal);
    image->preview.width = preview.width;
    image->preview.height = preview.height;
    return {std::move(image), {}, {}};
}

} // namespace rawviewer::application
