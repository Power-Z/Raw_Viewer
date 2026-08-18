#include "application/pixel_statistics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <span>
#include <utility>

namespace rawviewer::application {
namespace {

constexpr std::size_t maximumChannelResults = 4;

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
    const auto value = static_cast<std::uint32_t>(std::min<long double>(
        1000.0L,
        static_cast<long double>(completed) * 1000.0L /
            static_cast<long double>(total)));
    request.progressPermille->store(value, std::memory_order_relaxed);
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

std::vector<PixelStatisticsResult> failures(
    const PixelStatisticsRequest& request,
    std::string code,
    std::string message) {
    return {failure(request, std::move(code), std::move(message))};
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

bool channelMatches(domain::BayerChannel requested,
                    domain::BayerChannel actual) noexcept {
    return requested == domain::BayerChannel::None || requested == actual;
}

std::vector<PixelStatisticsResult> executeForChannels(
    const PixelStatisticsRequest& request,
    std::span<const domain::BayerChannel> channels) {
    if (!request.source || !request.source->pixels) {
        return failures(request,
                        "statistics.missing_source",
                        "The source image has no readable pixel data.");
    }
    if (request.source->metadata.kind == ImageKind::Standard) {
        return failures(request,
                        "statistics.unsupported_source",
                        "Pixel Statistics only supports RAW pipeline data.");
    }
    const bool needsBayerLayout = std::any_of(
        channels.begin(), channels.end(), [](domain::BayerChannel channel) {
            return channel != domain::BayerChannel::None;
        });
    if (needsBayerLayout &&
        request.source->metadata.bayerPattern == domain::BayerPattern::None) {
        return failures(
            request,
            "statistics.channel_unavailable",
            "The displayed RAW has no regular Bayer channel layout; use All samples.");
    }
    if (channels.empty() || channels.size() > maximumChannelResults) {
        return failures(request,
                        "statistics.invalid_channels",
                        "One to four statistics channels are required.");
    }
    if (request.mode == PixelStatisticsMode::WhiteBalance) {
        return failures(request,
                        "statistics.wb_reserved",
                        "White balance statistics are reserved for a later version.");
    }
    if (request.histogramBins < 16 || request.histogramBins > 4096) {
        return failures(request,
                        "statistics.invalid_bins",
                        "Histogram bin count must be between 16 and 4096.");
    }

    const auto selection = normalized(request.selection);
    if (selection.x1 >= request.source->pixels->width() ||
        selection.y1 >= request.source->pixels->height()) {
        return failures(request,
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
        return failures(request,
                        "statistics.selection_too_large",
                        "The statistics selection exceeds supported coordinate limits.");
    }
    if (cancelled(request)) {
        return failures(request, "task.cancelled", "Statistics cancelled.");
    }

    std::vector<PixelStatisticsResult> results(channels.size());
    for (std::size_t index = 0; index < results.size(); ++index) {
        results[index].mode = request.mode;
        results[index].channel = channels[index];
        results[index].selection = selection;
    }
    std::array<OnlineStatistics, maximumChannelResults> summaries;
    const auto sampleChannelAt = [&](std::uint64_t x, std::uint64_t y) {
        return domain::bayerChannelAt(request.source->metadata.bayerPattern,
                                      x,
                                      y);
    };

    if (request.mode == PixelStatisticsMode::Status) {
        const auto [rangeMinimum, rangeMaximum] =
            histogramRange(request.source->metadata);
        const double binWidth =
            (rangeMaximum - rangeMinimum) / request.histogramBins;
        for (auto& result : results) {
            result.plot.histogram = true;
            result.plot.x.resize(request.histogramBins);
            result.plot.y.assign(request.histogramBins, 0.0);
            for (std::uint32_t bin = 0; bin < request.histogramBins; ++bin) {
                result.plot.x[bin] = rangeMinimum +
                    (static_cast<double>(bin) + 0.5) * binWidth;
            }
        }

        const std::uint64_t rows = selection.y1 - selection.y0 + 1;
        std::uint64_t inspected = 0;
        for (std::uint64_t y = selection.y0;; ++y) {
            for (std::uint64_t x = selection.x0;; ++x, ++inspected) {
                if ((inspected & 0x0FFFU) == 0 && cancelled(request)) {
                    return failures(request,
                                    "task.cancelled",
                                    "Statistics cancelled.");
                }
                const auto sample = request.source->pixels->sample(x, y);
                if (sample.valid && std::isfinite(sample.value)) {
                    const auto actualChannel = sampleChannelAt(x, y);
                    for (std::size_t index = 0; index < results.size(); ++index) {
                        if (!channelMatches(channels[index], actualChannel)) {
                            continue;
                        }
                        summaries[index].add(sample.value);
                        const double normalizedValue =
                            (sample.value - rangeMinimum) /
                            (rangeMaximum - rangeMinimum);
                        const auto bin = static_cast<std::uint32_t>(std::clamp(
                            normalizedValue * request.histogramBins,
                            0.0,
                            static_cast<double>(request.histogramBins - 1)));
                        results[index].plot.y[bin] += 1.0;
                    }
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
        for (auto& result : results) {
            result.plot.x.reserve(reserved);
            result.plot.y.reserve(reserved);
        }
        std::array<std::uint64_t, maximumChannelResults> validProfiles{};
        std::uint64_t inspected = 0;
        for (std::uint64_t outer = outerStart;; ++outer) {
            std::array<OnlineStatistics, maximumChannelResults> lineStatistics;
            for (std::uint64_t inner = innerStart;; ++inner, ++inspected) {
                if ((inspected & 0x0FFFU) == 0 && cancelled(request)) {
                    return failures(request,
                                    "task.cancelled",
                                    "Statistics cancelled.");
                }
                const auto x = horizontal ? outer : inner;
                const auto y = horizontal ? inner : outer;
                const auto sample = request.source->pixels->sample(x, y);
                if (sample.valid && std::isfinite(sample.value)) {
                    const auto actualChannel = sampleChannelAt(x, y);
                    for (std::size_t index = 0; index < results.size(); ++index) {
                        if (channelMatches(channels[index], actualChannel)) {
                            lineStatistics[index].add(sample.value);
                        }
                    }
                }
                if (inner == innerEnd) {
                    break;
                }
            }
            for (std::size_t index = 0; index < results.size(); ++index) {
                const auto item = lineStatistics[index].summary();
                if (item.count == 0) {
                    continue;
                }
                summaries[index].add(item.mean);
                if (validProfiles[index] % plotStep == 0 ||
                    outer == outerEnd) {
                    results[index].plot.x.push_back(
                        static_cast<double>(outer - outerStart));
                    results[index].plot.y.push_back(item.mean);
                }
                ++validProfiles[index];
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
        for (auto& result : results) {
            result.plot.x.reserve(reserved);
            result.plot.y.reserve(reserved);
        }
        const double totalDistance = std::hypot(static_cast<double>(dx),
                                                static_cast<double>(dy));
        std::array<std::uint64_t, maximumChannelResults> validSamples{};
        for (std::uint64_t pointIndex = 0; pointIndex < pointCount;
             ++pointIndex) {
            if ((pointIndex & 0x0FFFU) == 0 && cancelled(request)) {
                return failures(request,
                                "task.cancelled",
                                "Statistics cancelled.");
            }
            const double t = steps == 0
                ? 0.0
                : static_cast<double>(pointIndex) / static_cast<double>(steps);
            const auto x = static_cast<std::uint64_t>(
                std::llround(static_cast<double>(x0) + t * dx));
            const auto y = static_cast<std::uint64_t>(
                std::llround(static_cast<double>(y0) + t * dy));
            const auto sample = request.source->pixels->sample(x, y);
            if (sample.valid && std::isfinite(sample.value)) {
                const auto actualChannel = sampleChannelAt(x, y);
                for (std::size_t index = 0; index < results.size(); ++index) {
                    if (!channelMatches(channels[index], actualChannel)) {
                        continue;
                    }
                    summaries[index].add(sample.value);
                    if (validSamples[index] % plotStep == 0 ||
                        pointIndex + 1 == pointCount) {
                        results[index].plot.x.push_back(t * totalDistance);
                        results[index].plot.y.push_back(sample.value);
                    }
                    ++validSamples[index];
                }
            }
            setProgress(request, pointIndex + 1, pointCount);
        }
    }

    for (std::size_t index = 0; index < results.size(); ++index) {
        results[index].summary = summaries[index].summary();
        if (results[index].summary.count == 0) {
            results[index].errorCode = "statistics.empty_selection";
            results[index].message =
                "The selection contains no valid samples for this channel.";
        }
    }
    setProgress(request, 1, 1);
    return results;
}

} // namespace

PixelStatisticsResult PixelStatisticsService::execute(
    const PixelStatisticsRequest& request) const {
    const std::array channels{request.channel};
    auto results = executeForChannels(request, channels);
    return std::move(results.front());
}

std::vector<PixelStatisticsResult> PixelStatisticsService::executeChannels(
    const PixelStatisticsRequest& request) const {
    constexpr std::array channels{domain::BayerChannel::R,
                                  domain::BayerChannel::Gr,
                                  domain::BayerChannel::Gb,
                                  domain::BayerChannel::B};
    return executeForChannels(request, channels);
}

} // namespace rawviewer::application
