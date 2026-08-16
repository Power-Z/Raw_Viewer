#include "application/pixel_statistics.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace rawviewer::application {
namespace {

class OnlineStatistics {
public:
    void add(double value) noexcept {
        if (!std::isfinite(value)) {
            return;
        }
        minimum_ = std::min(minimum_, value);
        maximum_ = std::max(maximum_, value);
        ++count_;
        const double delta = value - mean_;
        mean_ += delta / static_cast<double>(count_);
        const double delta2 = value - mean_;
        m2_ += delta * delta2;
    }

    StatisticsSummary summary() const noexcept {
        if (count_ == 0) {
            return {};
        }
        return {
            count_,
            minimum_,
            maximum_,
            mean_,
            std::sqrt(std::max(0.0, m2_ / static_cast<double>(count_)))
        };
    }

private:
    std::uint64_t count_ = 0;
    double minimum_ = std::numeric_limits<double>::infinity();
    double maximum_ = -std::numeric_limits<double>::infinity();
    double mean_ = 0.0;
    double m2_ = 0.0;
};

bool cancelled(const PixelStatisticsRequest& request) noexcept {
    return request.cancellation &&
           request.cancellation->load(std::memory_order_relaxed);
}

void setProgress(const PixelStatisticsRequest& request,
                 std::uint64_t completed,
                 std::uint64_t total) noexcept {
    if (!request.progressPermille || total == 0) {
        return;
    }
    const auto value = static_cast<std::uint32_t>(
        std::min<long double>(
            1000.0L,
            static_cast<long double>(completed) * 1000.0L /
                static_cast<long double>(total)));
    request.progressPermille->store(value, std::memory_order_relaxed);
}

bool channelMatches(const PixelStatisticsRequest& request,
                    std::uint64_t x,
                    std::uint64_t y) noexcept {
    return request.channel == domain::BayerChannel::None ||
           domain::bayerChannelAt(request.source->metadata.bayerPattern,
                                  x,
                                  y) == request.channel;
}

bool addSourceSample(const PixelStatisticsRequest& request,
                     std::uint64_t x,
                     std::uint64_t y,
                     OnlineStatistics& statistics,
                     double* acceptedValue = nullptr) noexcept {
    if (!channelMatches(request, x, y)) {
        return false;
    }
    const auto sample = request.source->pixels->sample(x, y);
    if (!sample.valid || !std::isfinite(sample.value)) {
        return false;
    }
    statistics.add(sample.value);
    if (acceptedValue) {
        *acceptedValue = sample.value;
    }
    return true;
}

PixelStatisticsResult failure(const PixelStatisticsRequest& request,
                              std::string code,
                              std::string message) {
    PixelStatisticsResult result;
    result.mode = request.mode;
    result.channel = request.channel;
    result.selection = request.selection;
    result.errorCode = std::move(code);
    result.message = std::move(message);
    return result;
}

StatisticsSelection normalized(
    const StatisticsSelection& selection) noexcept {
    return {
        std::min(selection.x0, selection.x1),
        std::min(selection.y0, selection.y1),
        std::max(selection.x0, selection.x1),
        std::max(selection.y0, selection.y1)
    };
}

std::pair<double, double> histogramRange(const ImageMetadata& metadata) {
    switch (metadata.scalarType) {
    case domain::ScalarType::UInt8:
        return {0.0, 256.0};
    case domain::ScalarType::UInt16:
        return {0.0, 65536.0};
    case domain::ScalarType::UInt32:
        return {0.0, 4294967296.0};
    case domain::ScalarType::Float32:
        return {0.0, metadata.whiteLevel > 0.0 ? metadata.whiteLevel : 1.0};
    }
    return {0.0, 1.0};
}

} // namespace

PixelStatisticsResult PixelStatisticsService::execute(
    const PixelStatisticsRequest& request) const {
    if (!request.source || !request.source->pixels) {
        return failure(request,
                       "statistics.missing_source",
                       "The source image has no readable pixel data.");
    }
    if (request.source->metadata.kind == ImageKind::Standard ||
        request.source->metadata.bayerPattern == domain::BayerPattern::None) {
        return failure(request,
                       "statistics.unsupported_source",
                       "Pixel Statistics only supports original Bayer RAW data.");
    }
    if (request.mode == PixelStatisticsMode::WhiteBalance) {
        return failure(request,
                       "statistics.wb_reserved",
                       "White balance statistics are reserved for a later version.");
    }
    if (request.histogramBins < 16 || request.histogramBins > 4096) {
        return failure(request,
                       "statistics.invalid_bins",
                       "Histogram bin count must be between 16 and 4096.");
    }

    const auto selection = normalized(request.selection);
    if (selection.x1 >= request.source->pixels->width() ||
        selection.y1 >= request.source->pixels->height()) {
        return failure(request,
                       "statistics.invalid_selection",
                       "The statistics selection lies outside the source image.");
    }
    if (selection.x1 == std::numeric_limits<std::uint64_t>::max() ||
        selection.y1 == std::numeric_limits<std::uint64_t>::max() ||
        (request.mode == PixelStatisticsMode::Line &&
         (request.selection.x0 >
              static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
          request.selection.y0 >
              static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
          request.selection.x1 >
              static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
          request.selection.y1 >
              static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))) {
        return failure(request,
                       "statistics.selection_too_large",
                       "The statistics selection exceeds supported coordinate limits.");
    }
    if (cancelled(request)) {
        return failure(request, "task.cancelled", "Statistics cancelled.");
    }

    PixelStatisticsResult result;
    result.mode = request.mode;
    result.channel = request.channel;
    result.selection = selection;
    OnlineStatistics summary;

    if (request.mode == PixelStatisticsMode::Status) {
        const auto [rangeMinimum, rangeMaximum] =
            histogramRange(request.source->metadata);
        result.plot.histogram = true;
        result.plot.x.resize(request.histogramBins);
        result.plot.y.assign(request.histogramBins, 0.0);
        const double binWidth =
            (rangeMaximum - rangeMinimum) / request.histogramBins;
        for (std::uint32_t bin = 0; bin < request.histogramBins; ++bin) {
            result.plot.x[bin] = rangeMinimum +
                (static_cast<double>(bin) + 0.5) * binWidth;
        }

        const std::uint64_t rows = selection.y1 - selection.y0 + 1;
        std::uint64_t inspected = 0;
        for (std::uint64_t y = selection.y0;; ++y) {
            for (std::uint64_t x = selection.x0;; ++x, ++inspected) {
                if ((inspected & 0x0FFFU) == 0 && cancelled(request)) {
                    return failure(request, "task.cancelled", "Statistics cancelled.");
                }
                double value = 0.0;
                if (addSourceSample(request, x, y, summary, &value)) {
                    const double normalizedValue =
                        (value - rangeMinimum) / (rangeMaximum - rangeMinimum);
                    const auto bin = static_cast<std::uint32_t>(std::clamp(
                        normalizedValue * request.histogramBins,
                        0.0,
                        static_cast<double>(request.histogramBins - 1)));
                    result.plot.y[bin] += 1.0;
                }
                if (x == selection.x1) {
                    break;
                }
            }
            setProgress(request, y - selection.y0 + 1, rows);
            if (y == selection.y1) {
                break;
            }
        }
    } else if (request.mode == PixelStatisticsMode::HorizontalBox ||
               request.mode == PixelStatisticsMode::VerticalBox) {
        const bool horizontal =
            request.mode == PixelStatisticsMode::HorizontalBox;
        const std::uint64_t outerStart = horizontal ? selection.x0 : selection.y0;
        const std::uint64_t outerEnd = horizontal ? selection.x1 : selection.y1;
        const std::uint64_t innerStart = horizontal ? selection.y0 : selection.x0;
        const std::uint64_t innerEnd = horizontal ? selection.y1 : selection.x1;
        const std::uint64_t outerCount = outerEnd - outerStart + 1;
        constexpr std::uint64_t maximumPlotPoints = 65536;
        const std::uint64_t plotStep = std::max<std::uint64_t>(
            1, 1 + (outerCount - 1) / maximumPlotPoints);
        const auto reserved = static_cast<std::size_t>(
            std::min(outerCount, maximumPlotPoints + 1));
        result.plot.x.reserve(reserved);
        result.plot.y.reserve(reserved);
        std::uint64_t inspected = 0;
        std::uint64_t validProfiles = 0;
        for (std::uint64_t outer = outerStart;; ++outer) {
            OnlineStatistics columnOrRow;
            for (std::uint64_t inner = innerStart;; ++inner, ++inspected) {
                if ((inspected & 0x0FFFU) == 0 && cancelled(request)) {
                    return failure(request, "task.cancelled", "Statistics cancelled.");
                }
                const auto x = horizontal ? outer : inner;
                const auto y = horizontal ? inner : outer;
                addSourceSample(request, x, y, columnOrRow);
                if (inner == innerEnd) {
                    break;
                }
            }
            const auto item = columnOrRow.summary();
            if (item.count > 0) {
                summary.add(item.mean);
                if (validProfiles % plotStep == 0 || outer == outerEnd) {
                    result.plot.x.push_back(
                        static_cast<double>(outer - outerStart));
                    result.plot.y.push_back(item.mean);
                }
                ++validProfiles;
            }
            setProgress(request, outer - outerStart + 1, outerCount);
            if (outer == outerEnd) {
                break;
            }
        }
    } else if (request.mode == PixelStatisticsMode::Line) {
        const auto x0 = static_cast<std::int64_t>(request.selection.x0);
        const auto y0 = static_cast<std::int64_t>(request.selection.y0);
        const auto x1 = static_cast<std::int64_t>(request.selection.x1);
        const auto y1 = static_cast<std::int64_t>(request.selection.y1);
        const auto dx = x1 - x0;
        const auto dy = y1 - y0;
        const auto steps = static_cast<std::uint64_t>(
            std::max(std::llabs(dx), std::llabs(dy)));
        const auto pointCount = steps + 1;
        constexpr std::uint64_t maximumPlotPoints = 65536;
        const std::uint64_t plotStep = std::max<std::uint64_t>(
            1, 1 + (pointCount - 1) / maximumPlotPoints);
        const auto reserved = static_cast<std::size_t>(
            std::min(pointCount, maximumPlotPoints + 1));
        result.plot.x.reserve(reserved);
        result.plot.y.reserve(reserved);
        const double totalDistance = std::hypot(static_cast<double>(dx),
                                                static_cast<double>(dy));
        std::uint64_t validLineSamples = 0;
        for (std::uint64_t index = 0; index < pointCount; ++index) {
            if ((index & 0x0FFFU) == 0 && cancelled(request)) {
                return failure(request, "task.cancelled", "Statistics cancelled.");
            }
            const double t = steps == 0
                ? 0.0
                : static_cast<double>(index) / static_cast<double>(steps);
            const auto x = static_cast<std::uint64_t>(
                std::llround(static_cast<double>(x0) + t * dx));
            const auto y = static_cast<std::uint64_t>(
                std::llround(static_cast<double>(y0) + t * dy));
            double value = 0.0;
            if (addSourceSample(request, x, y, summary, &value)) {
                if (validLineSamples % plotStep == 0 ||
                    index + 1 == pointCount) {
                    result.plot.x.push_back(t * totalDistance);
                    result.plot.y.push_back(value);
                }
                ++validLineSamples;
            }
            setProgress(request, index + 1, pointCount);
        }
    }

    result.summary = summary.summary();
    if (result.summary.count == 0) {
        return failure(request,
                       "statistics.empty_selection",
                       "The selection contains no valid samples for the chosen channel.");
    }
    setProgress(request, 1, 1);
    return result;
}

} // namespace rawviewer::application
